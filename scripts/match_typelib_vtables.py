"""
Match MS Project's COM type library against the binary's vtables to name functions
and map internal modules.

Reads:
  analysis/typelib.tsv        – COM type library (interfaces, methods, slots)
  analysis/meta/vtables.tsv   – vtable class→slot→func_addr from RTTI
  analysis/meta/functions.tsv – all functions (entry, name, namespace, etc.)
  analysis/meta/symbols.tsv   – non-default symbols
  analysis/meta/calls.tsv     – call graph edges
  analysis/meta/strings.tsv   – strings with referencing functions
  analysis/meta/imports.tsv   – imported APIs with referencing functions

Writes:
  analysis/vtable_matches.tsv         – typelib interface → vtable class matches
  analysis/named_functions.tsv        – func_addr → assigned name, source
  analysis/module_map.tsv             – func_addr → module assignment
  analysis/class_hierarchy.tsv        – class → parent, interface, module
  analysis/match_summary.txt          – human-readable summary
"""

import csv
import os
import re
import sys
from collections import defaultdict, Counter
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
META = BASE / "analysis" / "meta"
OUT  = BASE / "analysis"

csv.field_size_limit(10_000_000)

# ── 1. Parse typelib ──────────────────────────────────────────────────────────

def load_typelib():
    """Parse typelib.tsv into interface definitions.
    Returns:
        ifaces: {iface_name: {slot_int: {name, invkind, memid, params}}}
        dispatches: same but for TKIND_DISPATCH
        enums: {enum_name: {memid: const_name}}
        guids: {iface_name: guid}
    """
    ifaces = defaultdict(dict)
    dispatches = defaultdict(dict)
    enums = defaultdict(dict)
    guids = {}

    with open(OUT / "typelib.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            ti = row["typeinfo"]
            tk = row["typekind"]
            slot = row.get("slot", "").strip()
            name = row.get("name", "").strip()
            invkind = row.get("invkind", "").strip()
            memid = row.get("memid", "").strip()
            params = row.get("params", "").strip()
            guid = row.get("guid", "").strip()

            if guid and ti not in guids:
                guids[ti] = guid

            if tk == "ENUM":
                if memid:
                    enums[ti][memid] = name
            elif tk == "TKIND_INTERFACE":
                if slot:
                    slot_i = int(slot)
                    ifaces[ti][slot_i] = {
                        "name": name, "invkind": invkind,
                        "memid": memid, "params": params
                    }
            elif tk == "TKIND_DISPATCH":
                if slot:
                    slot_i = int(slot)
                    dispatches[ti][slot_i] = {
                        "name": name, "invkind": invkind,
                        "memid": memid, "params": params
                    }

    return ifaces, dispatches, enums, guids


# ── 2. Parse vtables ─────────────────────────────────────────────────────────

def load_vtables():
    """Parse vtables.tsv.
    Returns:
        vtables: {(vtable_addr, class_name): {slot_int: func_addr}}
        func_to_vtslots: {func_addr: [(class_name, slot_int)]}
    """
    vtables = defaultdict(dict)
    func_to_vtslots = defaultdict(list)

    with open(META / "vtables.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            vaddr = row["vtable_addr"]
            cls = row["class"]
            slot = int(row["slot"])
            func = row["func"]
            key = (vaddr, cls)
            vtables[key][slot] = func
            func_to_vtslots[func].append((cls, slot))

    return vtables, func_to_vtslots


# ── 3. Parse functions ────────────────────────────────────────────────────────

def load_functions():
    """Returns {func_addr: {name, namespace, bytes, callers, callees, signature}}"""
    funcs = {}
    with open(META / "functions.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            funcs[row["entry"]] = row
    return funcs


# ── 4. Parse call graph ──────────────────────────────────────────────────────

def load_calls():
    """Returns caller→[callee], callee→[caller]"""
    call_out = defaultdict(list)
    call_in = defaultdict(list)
    with open(META / "calls.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            caller = row["caller"]
            callee = row["callee"]
            call_out[caller].append(callee)
            call_in[callee].append(caller)
    return call_out, call_in


# ── 5. Parse strings ─────────────────────────────────────────────────────────

def load_strings():
    """Returns {func_addr: [string_values]}"""
    func_strings = defaultdict(list)
    with open(META / "strings.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            val = row.get("value", "")
            funcs_str = row.get("funcs", "")
            if funcs_str:
                for fa in funcs_str.split(","):
                    fa = fa.strip()
                    if fa:
                        func_strings[fa].append(val)
    return func_strings


# ── 6. Parse imports ─────────────────────────────────────────────────────────

def load_imports():
    """Returns {func_addr: [(library, api_name)]}"""
    func_imports = defaultdict(list)
    with open(META / "imports.tsv", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            lib = row.get("library", "")
            name = row.get("name", "")
            funcs_str = row.get("funcs", "")
            if funcs_str:
                for fa in funcs_str.split(","):
                    fa = fa.strip()
                    if fa:
                        func_imports[fa].append((lib, name))
    return func_imports


# ── 7. Match typelib interfaces to vtable classes ─────────────────────────────

def match_interfaces_to_vtables(ifaces, dispatches, vtables):
    """
    Strategy: for each typelib interface, find vtable classes whose name
    contains the interface name or its dispatch variant.

    COM dual interfaces: IUnknown (slots 0-2) + IDispatch (slots 3-6) + custom (slot 7+).
    The typelib slots start at 7 for dual interfaces.

    Matching approaches:
    1. Vtable class name contains 'I<InterfaceName>Disp' (e.g., ITaskDisp, IFilterDisp)
    2. Vtable class name contains 'C<InterfaceName>Disp' (implementation class)
    3. Vtable class name is the interface name itself (rare)
    4. CGenericUnknownWrap<...,I<Name>Disp> pattern
    """
    matches = []
    iface_names = set(ifaces.keys()) | set(dispatches.keys())

    # Build index of vtable classes by name fragments
    vtable_by_cls = defaultdict(list)
    for (vaddr, cls), slots in vtables.items():
        vtable_by_cls[cls].append((vaddr, slots))

    # Extract all unique class names
    all_cls_names = set(vtable_by_cls.keys())

    matched_ifaces = {}

    for iface_name in sorted(iface_names):
        candidates = []

        # Pattern 1: exact class name match (e.g., interface "Task" → class "Task")
        if iface_name in all_cls_names:
            candidates.append((iface_name, "exact", 100))

        # Pattern 2: C<Name>Disp (e.g., "Task" → "CTaskDisp")
        c_disp = f"C{iface_name}Disp"
        if c_disp in all_cls_names:
            candidates.append((c_disp, "CDisp", 90))

        # Pattern 3: I<Name>Disp inside CGenericUnknownWrap
        i_disp = f"I{iface_name}Disp"
        for cls in all_cls_names:
            if i_disp in cls and "CGenericUnknownWrap" in cls:
                candidates.append((cls, "GenericWrap", 85))
            elif i_disp in cls and cls != i_disp:
                candidates.append((cls, "contains_IDisp", 80))

        # Pattern 4: C<Name> without Disp
        c_name = f"C{iface_name}"
        if c_name in all_cls_names:
            candidates.append((c_name, "CName", 70))

        # Pattern 5: exact interface name I<Name>
        i_name = f"I{iface_name}"
        for cls in all_cls_names:
            if cls == i_name:
                candidates.append((cls, "IName", 75))

        # Pattern 6: IBknd<Name>Dispatch and TBknd<Name>Dispatch (backend dispatches)
        # These implement the COM automation interfaces internally
        ibknd_map = {
            "Task": "IBkndTaskDispatch",
            "Resource": "IBkndRscDispatch",
            "Assignment": "IBkndAssnDispatch",
            "Tasks": "IBkndSetsDispatch",
            "Resources": "IBkndSetsDispatch",
            "Assignments": "IBkndAssnsDispatch",
            "Calendar": "IBkndCalDispatch",
            "Calendars": "IBkndCalsDispatch",
            "Engagement": "IBkndEgmtDispatch",
            "Engagements": "IBkndEgmtsDispatch",
            "Issue": "IBkndIssueDispatch",
            "Issues": "IBkndIssuesDispatch",
            "Risk": "IBkndRiskDispatch",
            "Risks": "IBkndRisksDispatch",
            "Day": "TBkndDayDispatch",
            "Days": "TBkndDaysDispatch",
            "Month": "TBkndMonthDispatch",
            "Months": "TBkndMonthsDispatch",
            "Year": "TBkndYearDispatch",
            "Years": "TBkndYearsDispatch",
            "WeekDay": "TBkndWeekDayDispatch",
            "WeekDays": "TBkndWeekDaysDispatch",
            "WorkWeek": "TBkndWorkWeekDispatch",
            "WorkWeeks": "TBkndWorkWeeksDispatch",
            "WorkWeekDay": "TBkndWorkWeekDayDispatch",
            "WorkWeekDays": "TBkndWorkWeekDaysDispatch",
            "Exception": "TBkndExceptionDispatch",
            "Exceptions": "TBkndExceptionsDispatch",
            "Shift": "TBkndShiftDispatch",
            "Period": "TBkndPeriodDispatch",
            "StartDriver": "TBkndStartDriverDispatch",
            "TaskDependency": "TBkndTaskDependencyDispatch",
            "TaskDependencies": "TBkndTaskDependenciesDispatch",
            "TimeScaleValue": "TBkndTSValueDispatch",
            "TimeScaleValues": "TBkndTSValuesDispatch",
            "SplitPart": "IBkndTaskOnePartDispatch",
            "SplitParts": "IBkndTaskPartsDispatch",
            "PayRate": "TRscPayRateDispatch",
            "PayRates": "TRscPayRatesDispatch",
            "CostRateTable": "TRscCostRateTableDispatch",
            "CostRateTables": "TRscCostRateTablesDispatch",
            "Availability": "TRscUnitsAvailabilityDispatch",
            "Availabilities": "TRscUnitsAvailabilitiesDispatch",
            "ActualStartDrivers": "TBkndStartDriversDispatch",
            "PredecessorDrivers": "TBkndStartDriversDispatch",
            "CalendarDrivers": "TBkndStartDriversDispatch",
            "ChildDrivers": "TBkndStartDriversDispatch",
            "OverAllocatedAssignments": "IBkndAssnsDispatch",
        }
        if iface_name in ibknd_map and ibknd_map[iface_name] in all_cls_names:
            candidates.append((ibknd_map[iface_name], "IBkndDispatch", 92))

        # Pattern 7: special well-known class mappings
        special_map = {
            "_MSProject": "CAppDisp",
            "_Global": "CAppDisp",
            "_EProjectApp": "CAppDisp",
            "_EProjectApp2": "CAppDisp",
            "_EProjectDoc": "CDocDisp",
            "_IProjectDoc": "CDocDisp",
            "Window": "CDocFrm",
            "EventInfo": "CEventInfoDisp",
            "Selection": "CSelection",
            "Projects": "CDocsDisp",
            "Group": "CGroupDisp",
            "Group2": "CGroupDisp",
            "GroupCriterion": "CGroupCriterionDisp",
            "GroupCriterion2": "CGroupCriterionDisp",
            "GroupCriteria": "CGroupCriteriaDisp",
            "GroupCriteria2": "CGroupCriteriaDisp",
            "Subproject": "CSubProjDisp",
        }
        if iface_name in special_map and special_map[iface_name] in all_cls_names:
            candidates.append((special_map[iface_name], "special", 95))

        # Pattern 8: look for CGenericUnknownWrap containing the interface name
        # with any suffix pattern (not just IDisp)
        for cls in all_cls_names:
            if "CGenericUnknownWrap" in cls and iface_name in cls and cls not in [c[0] for c in candidates]:
                candidates.append((cls, "GenericWrap_name", 82))

        if candidates:
            # Pick best match by confidence
            candidates.sort(key=lambda x: -x[2])
            best_cls, match_type, confidence = candidates[0]
            matched_ifaces[iface_name] = {
                "class": best_cls,
                "match_type": match_type,
                "confidence": confidence,
                "all_candidates": candidates,
            }
            matches.append({
                "interface": iface_name,
                "class": best_cls,
                "match_type": match_type,
                "confidence": confidence,
                "n_candidates": len(candidates),
            })

    return matched_ifaces, matches


# ── 8. Name functions from vtable matches ─────────────────────────────────────

def name_functions_from_vtables(matched_ifaces, ifaces, dispatches, vtables, funcs):
    """
    For each matched interface→class, use the typelib slot numbers to
    assign method names to the vtable function addresses.
    """
    named = {}
    vtable_by_cls = defaultdict(list)
    for (vaddr, cls), slots in vtables.items():
        vtable_by_cls[cls].append((vaddr, slots))

    for iface_name, match_info in matched_ifaces.items():
        cls = match_info["class"]

        # Get the typelib methods for this interface
        methods = ifaces.get(iface_name, {})
        if not methods:
            methods = dispatches.get(iface_name, {})
        if not methods:
            continue

        # Get all vtable instances for this class
        for vaddr, slots in vtable_by_cls[cls]:
            for slot_num, method_info in methods.items():
                func_addr = slots.get(slot_num)
                if func_addr and func_addr in funcs:
                    func_info = funcs[func_addr]
                    # Only name if currently unnamed
                    is_unnamed = func_info["name"].startswith("FUN_")

                    invkind = method_info["invkind"]
                    mname = method_info["name"]

                    # Build descriptive name
                    if invkind == "INVOKE_PROPERTYGET":
                        full_name = f"get_{mname}"
                    elif invkind == "INVOKE_PROPERTYPUT":
                        full_name = f"put_{mname}"
                    elif invkind == "INVOKE_PROPERTYPUTREF":
                        full_name = f"putref_{mname}"
                    elif invkind == "INVOKE_FUNC":
                        full_name = mname
                    else:
                        full_name = mname

                    qualified = f"{iface_name}::{full_name}"

                    if func_addr not in named or named[func_addr]["confidence"] < match_info["confidence"]:
                        named[func_addr] = {
                            "func_addr": func_addr,
                            "name": qualified,
                            "interface": iface_name,
                            "method": mname,
                            "invkind": invkind,
                            "slot": slot_num,
                            "class": cls,
                            "source": f"typelib_vtable_{match_info['match_type']}",
                            "confidence": match_info["confidence"],
                            "was_unnamed": is_unnamed,
                        }

    return named


# ── 9. Name functions from vtable class membership (non-COM) ──────────────────

def name_functions_from_rtti(vtables, funcs, named_so_far):
    """
    For functions in vtables that weren't matched to typelib interfaces,
    assign names based on the RTTI class name + slot index.
    """
    named = dict(named_so_far)

    for (vaddr, cls), slots in vtables.items():
        # Skip std::, ATL:: template noise and Global
        if cls.startswith("std::") or cls.startswith("ATL::") or cls == "Global" or cls == "class":
            continue

        # Clean up template params for readable names
        clean_cls = cls.split("<")[0].strip()
        if not clean_cls:
            continue

        for slot_num, func_addr in sorted(slots.items()):
            if func_addr in named:
                continue
            if func_addr not in funcs:
                continue
            if not funcs[func_addr]["name"].startswith("FUN_"):
                continue

            named[func_addr] = {
                "func_addr": func_addr,
                "name": f"{clean_cls}::vfunc_{slot_num}",
                "interface": "",
                "method": f"vfunc_{slot_num}",
                "invkind": "",
                "slot": slot_num,
                "class": cls,
                "source": "rtti_vtable",
                "confidence": 50,
                "was_unnamed": True,
            }

    return named


# ── 10. Module mapping ────────────────────────────────────────────────────────

MODULE_PATTERNS = [
    # Core scheduling engine
    (r"^(TTask|TaskElement|CTask)", "scheduling.tasks"),
    (r"^(TRes|CRes|Resource)", "scheduling.resources"),
    (r"^(TAssign|Assignment|CAssignment)", "scheduling.assignments"),
    (r"^(TCal|CCal|Calendar)", "scheduling.calendar"),
    (r"^(TLink|CLink|TaskDependenc)", "scheduling.links"),
    (r"^(TBaseline|Baseline)", "scheduling.baselines"),
    (r"^(TTaskGraph|CScheduler|Leveling|CPath)", "scheduling.engine"),
    (r"^(CCustomField|CCustFld|CCUSTFLD)", "scheduling.custom_fields"),
    (r"^(CCodeMask|COutlineCode|CLookupTable)", "scheduling.outline_codes"),

    # Backend / data layer
    (r"^(Bknd|TBknd|CBknd)", "backend.core"),
    (r"^(TTable|TTaskTable|TResTable)", "backend.tables"),
    (r"^(BaseServer|CEntServer)", "backend.server"),
    (r"^(CDoc(Ser|11|12)|AceDoc|AceSer)", "backend.serialization"),
    (r"^(CContainerStm|CFileStream|CBufferStream|CEncryptedStream)", "backend.storage"),

    # COM / Automation
    (r"^(CAppDisp|CDocDisp|CDocsDisp)", "com.application"),
    (r"^(CGenericUnknownWrap|CGenericUnknown|CGenericDisp)", "com.dispatch"),
    (r"^(CGenericLogging)", "com.logging"),
    (r"^(CAAEnum|CCtlEnum|CEnum)", "com.enumerators"),
    (r"^(CClassFactory|CAppClassFactory|CCustomMoniker)", "com.factories"),
    (r"^(CDispEvents|CContainerEvent)", "com.events"),

    # Document management
    (r"^(CDocument|CDoc[A-Z])", "document.core"),
    (r"^(CDocFrm|CDocIP)", "document.framing"),
    (r"^(CDocOle|CDocPers|CDocHlink)", "document.ole"),
    (r"^(CDocPrint|CDocMso)", "document.mso"),

    # UI – Views
    (r"^(CGantt|CGenGantt)", "ui.gantt"),
    (r"^(CNet|CNetView|CPert)", "ui.network"),
    (r"^(CCalView|CCalendar(?!ComboBox))", "ui.calendar_view"),
    (r"^(CSheet|CGrid|CSpread)", "ui.sheet"),
    (r"^(CTimeline|Timeline)", "ui.timeline"),
    (r"^(CUsage)", "ui.usage"),
    (r"^(CReport|Report)", "ui.reports"),
    (r"^(Chart|CChart)", "ui.charts"),

    # UI – Dialogs and controls
    (r"^(CDlg|CDialog)", "ui.dialogs"),
    (r"^(CComboBox|CComboGU|CCboBox|CFlexCombo)", "ui.combos"),
    (r"^(CBar|BarColor)", "ui.bars"),
    (r"^(AutoFilter)", "ui.autofilter"),
    (r"^(CToolbar|CTB|CCmdBar)", "ui.toolbars"),

    # UI – Drawing / Art
    (r"^(Art[A-Z]|CArt|CDrawing|CDrawObj|CDrawClient)", "ui.drawing"),
    (r"^(OArt|PjDr::)", "ui.drawing.oart"),
    (r"^(Anim)", "ui.animation"),

    # Window management
    (r"^(CWnd|CFrame|CPanel|CView|CSplit|CMdi)", "ui.windows"),
    (r"^(ProjectWindow|CDesktop)", "ui.windows"),
    (r"^(Osf::)", "ui.ribbon"),

    # Import/Export
    (r"^(CImport|CExport|BaseImport)", "io.import_export"),
    (r"^(CXML|CXml|XML)", "io.xml"),
    (r"^(CEDL|CEDLDB)", "io.edl"),
    (r"^(CClip)", "io.clipboard"),

    # Web / Server / SharePoint
    (r"^(CWeb|cWeb|CUrl)", "web.browser"),
    (r"^(cProjView|cGBUI)", "web.controls"),
    (r"^(CPSI|CServer|ActiveCache)", "web.psi"),

    # VBA / Macro
    (r"^(CVBA|VBA|CMacro)", "vba.macro"),
    (r"^(CAddIn)", "vba.addins"),

    # Printing
    (r"^(CPrint|CPage|CPreview|Assign?Print)", "print"),

    # Filter / Sort / Group
    (r"^(CFilter|CFlatFilter|CFrontendFilter)", "data.filter"),
    (r"^(CSort)", "data.sort"),
    (r"^(CGroup)", "data.group"),

    # Team / Collaboration
    (r"^(CDlgTeam|CTeam)", "collab.team"),
    (r"^(CEngagement|Engagement)", "collab.engagements"),
    (r"^(CConflict)", "collab.conflicts"),

    # DDE / Interprocess
    (r"^(CDDE)", "interprocess.dde"),

    # Logging / Telemetry
    (r"^(CLogging|CGenericLogging|Telemetry)", "infrastructure.logging"),
    (r"^(CAutoRecovery)", "infrastructure.recovery"),
    (r"^(CCacheMgr)", "infrastructure.cache"),

    # Subproject
    (r"^(CSubProj|Subproject)", "scheduling.subprojects"),

    # Resource Pool
    (r"^(CPool|CSharer)", "scheduling.resource_pool"),

    # Availability / Work weeks
    (r"^(Availability|WorkWeek|StandardWeek|RecurringException)", "scheduling.work_time"),

    # Catch-all internal prefixes
    (r"^(CFUtil)", "util.field"),
    (r"^(CString|CArray)", "util.collections"),
    (r"^(Bondi::)", "util.bondi"),
]


def classify_function_module(cls_name, func_name, func_strings, func_imports):
    """Assign a module to a function based on class name, strings, imports."""
    # Try class name patterns first
    for pattern, module in MODULE_PATTERNS:
        if re.search(pattern, cls_name):
            return module

    # Try function name patterns
    for pattern, module in MODULE_PATTERNS:
        if re.search(pattern, func_name):
            return module

    # Heuristic: use string references
    if func_strings:
        for s in func_strings:
            sl = s.lower()
            if any(kw in sl for kw in ["gantt", "bar style", "timescale"]):
                return "ui.gantt"
            if any(kw in sl for kw in ["calendar", "weekday"]):
                return "scheduling.calendar"
            if any(kw in sl for kw in ["filter", "autofilter"]):
                return "data.filter"
            if any(kw in sl for kw in ["task", "milestone", "summary task"]):
                return "scheduling.tasks"
            if any(kw in sl for kw in ["resource", "resource pool"]):
                return "scheduling.resources"
            if any(kw in sl for kw in ["assignment"]):
                return "scheduling.assignments"
            if any(kw in sl for kw in ["print", "page setup", "header", "footer"]):
                return "print"
            if any(kw in sl for kw in ["xml", "mspdi"]):
                return "io.xml"
            if any(kw in sl for kw in [".mpp", ".mpt", "save", "open"]):
                return "backend.serialization"
            if any(kw in sl for kw in ["sharepoint", "project server", "pwa"]):
                return "web.psi"
            if any(kw in sl for kw in ["vba", "macro"]):
                return "vba.macro"

    # Heuristic: use import APIs
    if func_imports:
        api_names = set(name for _, name in func_imports)
        if api_names & {"CreateWindowExW", "DefWindowProcW", "RegisterClassW", "ShowWindow"}:
            return "ui.windows"
        if api_names & {"CreateFileW", "ReadFile", "WriteFile", "CloseHandle"}:
            return "backend.storage"
        if api_names & {"CoCreateInstance", "CoInitializeEx", "OleInitialize"}:
            return "com.core"
        if api_names & {"PrintDlgExW", "StartDocW", "EndDoc"}:
            return "print"

    return "unknown"


def build_module_map(named_funcs, vtables, funcs, func_strings_map, func_imports_map, call_out, call_in):
    """Assign every function to a module."""
    module_map = {}

    # 1. Functions named from vtables get module from their class
    for func_addr, info in named_funcs.items():
        cls = info.get("class", "")
        module = classify_function_module(cls, info["name"], [], [])
        module_map[func_addr] = module

    # 2. Functions in vtables but not yet in module_map
    for (vaddr, cls), slots in vtables.items():
        for slot, func_addr in slots.items():
            if func_addr not in module_map and func_addr in funcs:
                strs = func_strings_map.get(func_addr, [])
                imps = func_imports_map.get(func_addr, [])
                module = classify_function_module(cls, funcs[func_addr]["name"], strs, imps)
                module_map[func_addr] = module

    # 3. Remaining functions: use namespace, strings, imports, caller propagation
    for func_addr, func_info in funcs.items():
        if func_addr in module_map:
            continue
        ns = func_info.get("namespace", "")
        name = func_info.get("name", "")
        strs = func_strings_map.get(func_addr, [])
        imps = func_imports_map.get(func_addr, [])
        module = classify_function_module(ns, name, strs, imps)
        module_map[func_addr] = module

    # 4. Propagate modules from callees (what I call) and callers (who calls me)
    changed = True
    passes = 0
    while changed and passes < 10:
        changed = False
        passes += 1
        for func_addr in list(module_map.keys()):
            if module_map[func_addr] != "unknown":
                continue
            neighbor_modules = Counter()
            # What this function calls
            for callee in call_out.get(func_addr, []):
                m = module_map.get(callee, "unknown")
                if m != "unknown":
                    neighbor_modules[m] += 1
            # Who calls this function
            for caller in call_in.get(func_addr, []):
                m = module_map.get(caller, "unknown")
                if m != "unknown":
                    neighbor_modules[m] += 1
            if neighbor_modules:
                best = neighbor_modules.most_common(1)[0]
                threshold = 2 if passes < 5 else 1
                if best[1] >= threshold:
                    module_map[func_addr] = best[0]
                    changed = True

    # 5. Address-range propagation: functions near each other in .text are often
    # in the same module. Sort by address, then fill gaps.
    addr_list = sorted(module_map.keys(), key=lambda a: int(a, 16))
    for i, addr in enumerate(addr_list):
        if module_map[addr] != "unknown":
            continue
        # Look at nearest neighbors with known modules
        before_mod = None
        after_mod = None
        for j in range(max(0, i-5), i):
            m = module_map.get(addr_list[j], "unknown")
            if m != "unknown":
                before_mod = m
        for j in range(i+1, min(len(addr_list), i+6)):
            m = module_map.get(addr_list[j], "unknown")
            if m != "unknown":
                after_mod = m
                break
        if before_mod and before_mod == after_mod:
            module_map[addr] = before_mod

    return module_map


# ── 11. Build class hierarchy from vtable class names ─────────────────────────

def build_class_hierarchy(vtables, matched_ifaces):
    """Extract class→interface relationships from vtable names and matches."""
    classes = {}
    for (vaddr, cls), slots in vtables.items():
        if cls in classes:
            classes[cls]["vtable_count"] += 1
            classes[cls]["total_slots"] += len(slots)
            continue

        # Parse template args for parent info
        parent = ""
        iface = ""
        if "CGenericUnknownWrap" in cls:
            m = re.search(r"CGenericUnknownWrap<(\w+),.*?,(I\w+)>", cls)
            if m:
                parent = m.group(1)
                iface = m.group(2)
        elif "CComObject" in cls or "CComAggObject" in cls or "CComPolyObject" in cls:
            m = re.search(r"<(\w+(?:::\w+)*)>", cls)
            if m:
                parent = m.group(1)
        elif "CComContainedObject" in cls:
            m = re.search(r"<(\w+(?:::\w+)*)>", cls)
            if m:
                parent = m.group(1)

        # Check if matched to a typelib interface
        for iface_name, info in matched_ifaces.items():
            if info["class"] == cls:
                iface = iface_name
                break

        classes[cls] = {
            "parent": parent,
            "interface": iface,
            "vtable_count": 1,
            "total_slots": len(slots),
        }

    return classes


# ── MAIN ──────────────────────────────────────────────────────────────────────

def main():
    print("Loading data...")
    ifaces, dispatches, enums, guids = load_typelib()
    print(f"  Typelib: {len(ifaces)} interfaces, {len(dispatches)} dispatches, "
          f"{len(enums)} enums, {len(guids)} GUIDs")

    vtables, func_to_vtslots = load_vtables()
    print(f"  Vtables: {len(vtables)} tables, {len(func_to_vtslots)} unique funcs in vtables")

    funcs = load_functions()
    print(f"  Functions: {len(funcs)} total")

    call_out, call_in = load_calls()
    print(f"  Call graph: {sum(len(v) for v in call_out.values())} edges")

    func_strings = load_strings()
    print(f"  Strings: {len(func_strings)} funcs with string refs")

    func_imports = load_imports()
    print(f"  Imports: {len(func_imports)} funcs with import refs")

    # Step 1: Match typelib interfaces to vtable classes
    print("\nMatching typelib interfaces to vtable classes...")
    matched_ifaces, matches = match_interfaces_to_vtables(ifaces, dispatches, vtables)
    print(f"  Matched {len(matched_ifaces)} of {len(ifaces) + len(dispatches)} interfaces")

    # Step 2: Name functions from vtable + typelib matches
    print("\nNaming functions from typelib vtable matches...")
    named_funcs = name_functions_from_vtables(matched_ifaces, ifaces, dispatches, vtables, funcs)
    com_named = len(named_funcs)
    unnamed_com = sum(1 for v in named_funcs.values() if v["was_unnamed"])
    print(f"  Named {com_named} functions from COM typelib ({unnamed_com} were previously unnamed)")

    # Step 3: Name functions from RTTI vtable class membership
    print("\nNaming remaining vtable functions from RTTI...")
    all_named = name_functions_from_rtti(vtables, funcs, named_funcs)
    rtti_named = len(all_named) - com_named
    print(f"  Named {rtti_named} additional functions from RTTI class vtables")

    # Step 4: Module mapping
    print("\nBuilding module map...")
    module_map = build_module_map(all_named, vtables, funcs, func_strings, func_imports, call_out, call_in)
    module_counts = Counter(module_map.values())
    assigned = sum(1 for m in module_map.values() if m != "unknown")
    print(f"  Assigned {assigned} of {len(funcs)} functions to modules "
          f"({len(module_counts)} distinct modules)")

    # Step 5: Class hierarchy
    print("\nBuilding class hierarchy...")
    class_hier = build_class_hierarchy(vtables, matched_ifaces)
    print(f"  {len(class_hier)} unique classes")

    # ── Write outputs ────────────────────────────────────────────────────────

    # vtable_matches.tsv
    with open(OUT / "vtable_matches.tsv", "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["interface", "typekind", "guid", "vtable_class", "match_type",
                     "confidence", "n_typelib_methods", "n_vtable_slots"])
        for iface_name in sorted(matched_ifaces.keys()):
            info = matched_ifaces[iface_name]
            n_methods = len(ifaces.get(iface_name, {})) or len(dispatches.get(iface_name, {}))
            # Count vtable slots for matched class
            n_slots = 0
            for (vaddr, cls), slots in vtables.items():
                if cls == info["class"]:
                    n_slots = max(n_slots, len(slots))
            tk = "TKIND_INTERFACE" if iface_name in ifaces else "TKIND_DISPATCH"
            w.writerow([iface_name, tk, guids.get(iface_name, ""),
                        info["class"], info["match_type"], info["confidence"],
                        n_methods, n_slots])

    # named_functions.tsv
    with open(OUT / "named_functions.tsv", "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["func_addr", "assigned_name", "interface", "method", "invkind",
                     "slot", "class", "source", "confidence", "was_unnamed"])
        for addr in sorted(all_named.keys()):
            n = all_named[addr]
            w.writerow([n["func_addr"], n["name"], n["interface"], n["method"],
                        n["invkind"], n["slot"], n["class"], n["source"],
                        n["confidence"], n["was_unnamed"]])

    # module_map.tsv
    with open(OUT / "module_map.tsv", "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["func_addr", "func_name", "module", "assigned_name"])
        for addr in sorted(funcs.keys()):
            fname = funcs[addr]["name"]
            mod = module_map.get(addr, "unknown")
            aname = all_named[addr]["name"] if addr in all_named else ""
            w.writerow([addr, fname, mod, aname])

    # class_hierarchy.tsv
    with open(OUT / "class_hierarchy.tsv", "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["class", "parent", "interface", "vtable_count", "total_slots", "module"])
        for cls in sorted(class_hier.keys()):
            info = class_hier[cls]
            mod = classify_function_module(cls, "", [], [])
            w.writerow([cls, info["parent"], info["interface"],
                        info["vtable_count"], info["total_slots"], mod])

    # match_summary.txt
    with open(OUT / "match_summary.txt", "w", encoding="utf-8") as f:
        f.write("MS Project (WINPROJ.EXE) Type Library → Binary Analysis\n")
        f.write("=" * 60 + "\n\n")

        total_funcs = len(funcs)
        unnamed = sum(1 for fn in funcs.values() if fn["name"].startswith("FUN_"))
        f.write(f"Total functions:        {total_funcs:,}\n")
        f.write(f"Unnamed (FUN_*):        {unnamed:,} ({100*unnamed/total_funcs:.1f}%)\n")
        f.write(f"Already named:          {total_funcs - unnamed:,}\n\n")

        f.write(f"Typelib interfaces:     {len(ifaces)}\n")
        f.write(f"Typelib dispatches:     {len(dispatches)}\n")
        f.write(f"Typelib methods:        {sum(len(v) for v in ifaces.values())}\n")
        f.write(f"Typelib enum types:     {len(enums)}\n")
        f.write(f"Typelib enum values:    {sum(len(v) for v in enums.values())}\n\n")

        f.write(f"Binary vtable classes:  {len(class_hier):,}\n")
        f.write(f"Binary vtable entries:  {sum(len(s) for s in vtables.values()):,}\n")
        f.write(f"Unique funcs in vtbls:  {len(func_to_vtslots):,}\n\n")

        f.write("Matching Results\n")
        f.write("-" * 40 + "\n")
        f.write(f"Interfaces matched:     {len(matched_ifaces)} of {len(ifaces) + len(dispatches)}\n")
        f.write(f"Functions named (COM):  {com_named:,} ({unnamed_com:,} were unnamed)\n")
        f.write(f"Functions named (RTTI): {rtti_named:,}\n")
        f.write(f"Total functions named:  {len(all_named):,}\n")
        pct = 100 * len(all_named) / total_funcs if total_funcs else 0
        f.write(f"Coverage:               {pct:.1f}% of all functions\n\n")

        f.write("Module Map\n")
        f.write("-" * 40 + "\n")
        f.write(f"Functions with module:  {assigned:,} of {total_funcs:,}\n")
        f.write(f"Distinct modules:       {len(module_counts)}\n\n")

        f.write("Module breakdown:\n")
        for mod, count in sorted(module_counts.items(), key=lambda x: -x[1]):
            pct = 100 * count / total_funcs
            f.write(f"  {mod:<40s} {count:>6,} ({pct:5.1f}%)\n")

        f.write("\n\nMatched Interfaces (by confidence)\n")
        f.write("-" * 60 + "\n")
        for m in sorted(matches, key=lambda x: (-x["confidence"], x["interface"])):
            n_methods = len(ifaces.get(m["interface"], {})) or len(dispatches.get(m["interface"], {}))
            f.write(f"  {m['interface']:<35s} → {m['class']:<40s} "
                    f"[{m['match_type']}] conf={m['confidence']} methods={n_methods}\n")

        f.write("\n\nTop 50 classes by vtable size\n")
        f.write("-" * 60 + "\n")
        cls_sizes = []
        for (vaddr, cls), slots in vtables.items():
            cls_sizes.append((cls, len(slots), vaddr))
        cls_sizes.sort(key=lambda x: -x[1])
        for cls, nslots, vaddr in cls_sizes[:50]:
            iface = class_hier.get(cls, {}).get("interface", "")
            tag = f" [implements {iface}]" if iface else ""
            f.write(f"  {cls[:60]:<60s} {nslots:>4} slots @ {vaddr}{tag}\n")

    print(f"\nDone! Outputs in {OUT}/")
    print(f"  vtable_matches.tsv     - {len(matches)} interface->class matches")
    print(f"  named_functions.tsv    - {len(all_named):,} named functions")
    print(f"  module_map.tsv         - {len(module_map):,} functions mapped to modules")
    print(f"  class_hierarchy.tsv    - {len(class_hier):,} classes")
    print(f"  match_summary.txt      - human-readable summary")


if __name__ == "__main__":
    main()
