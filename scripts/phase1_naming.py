"""
Phase 1: Advanced function naming through call-graph propagation,
string analysis, and import-based inference.

Reads:
  analysis/named_functions.tsv    (Phase 0 names)
  analysis/meta/functions.tsv     (all functions)
  analysis/meta/calls.tsv         (call graph)
  analysis/meta/strings.tsv       (string references)
  analysis/meta/imports.tsv       (import references)
  analysis/module_map.tsv         (module assignments)

Produces:
  analysis/named_functions_p1.tsv (all named functions, Phase 0 + Phase 1)
  analysis/phase1_summary.txt     (human-readable summary)
"""

import csv
import os
import re
import collections

BASE = "C:/Users/timot/SFM Staff Dropbox/Tim King/Claude/Projects/MS Project Decompile"


def load_functions():
    funcs = {}
    with open(os.path.join(BASE, "analysis/meta/functions.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            entry = row["entry"]
            funcs[entry] = {
                "name": row["name"],
                "bytes": int(row["bytes"]),
                "callers": int(row["callers"]),
                "callees": int(row["callees"]),
                "sig": row["signature"],
                "thunk": row["thunk"] == "true",
            }
    return funcs


def load_phase0_names():
    names = {}
    with open(os.path.join(BASE, "analysis/named_functions.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            addr = row["func_addr"]
            names[addr] = {
                "assigned_name": row["assigned_name"],
                "source": row["source"],
                "confidence": int(row["confidence"]),
            }
    return names


def load_call_graph():
    callers_of = collections.defaultdict(set)
    callees_of = collections.defaultdict(set)
    with open(os.path.join(BASE, "analysis/meta/calls.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            caller = row["caller"]
            callee = row["callee"]
            callers_of[callee].add(caller)
            callees_of[caller].add(callee)
    return callers_of, callees_of


def load_strings():
    func_strings = collections.defaultdict(list)
    with open(os.path.join(BASE, "analysis/meta/strings.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            val = row["value"]
            funcs_str = row["funcs"]
            if not funcs_str:
                continue
            for fn in funcs_str.split(","):
                fn = fn.strip()
                if fn:
                    func_strings[fn].append(val)
    return func_strings


def load_imports():
    func_imports = collections.defaultdict(list)
    with open(os.path.join(BASE, "analysis/meta/imports.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            lib = row["library"]
            name = row["name"]
            funcs_str = row["funcs"]
            if not funcs_str:
                continue
            for fn in funcs_str.split(","):
                fn = fn.strip()
                if fn:
                    func_imports[fn].append((lib, name))
    return func_imports


def load_modules():
    modules = {}
    with open(os.path.join(BASE, "analysis/module_map.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            modules[row["func_addr"]] = row["module"]
    return modules


def extract_class(name):
    if "::" in name:
        return name.split("::")[0]
    return None


# ---- Strategy 1: Direct C++ scope strings ----
def name_from_cpp_strings(func_strings, known, funcs):
    """Functions that reference strings like 'ClassName::MethodName' get named directly."""
    new_names = {}
    cpp_re = re.compile(r"^([A-Z][A-Za-z0-9_]+)::([A-Za-z_][A-Za-z0-9_]*)$")

    for addr, strings in func_strings.items():
        if addr in known:
            continue
        if addr not in funcs:
            continue
        candidates = []
        for s in strings:
            m = cpp_re.match(s.strip())
            if m:
                candidates.append(f"{m.group(1)}::{m.group(2)}")

        if len(candidates) == 1:
            new_names[addr] = {
                "assigned_name": candidates[0],
                "source": "string_cpp_scope",
                "confidence": 80,
            }
        elif len(candidates) > 1:
            common_cls = set(c.split("::")[0] for c in candidates)
            if len(common_cls) == 1:
                cls = common_cls.pop()
                methods = [c.split("::")[1] for c in candidates]
                new_names[addr] = {
                    "assigned_name": f"{cls}::{methods[0]}",
                    "source": "string_cpp_scope",
                    "confidence": 70,
                }
    return new_names


# ---- Strategy 2: Error/log string naming ----
def name_from_diagnostic_strings(func_strings, known, funcs):
    """Functions with diagnostic strings like 'CreateOATable: Starting...' get named from the prefix."""
    new_names = {}
    diag_re = re.compile(r"^([A-Z][A-Za-z0-9_]+(?:::[A-Za-z_][A-Za-z0-9_]*)?)[\s:(\-]")

    for addr, strings in func_strings.items():
        if addr in known:
            continue
        if addr not in funcs:
            continue

        candidates = collections.Counter()
        for s in strings:
            m = diag_re.match(s.strip())
            if m:
                name = m.group(1)
                if len(name) > 3 and not name.isupper():
                    candidates[name] += 1

        if candidates:
            best, count = candidates.most_common(1)[0]
            if count >= 2 or (count == 1 and len(candidates) == 1 and len(strings) <= 5):
                new_names[addr] = {
                    "assigned_name": best.replace("::", "::"),
                    "source": "string_diagnostic",
                    "confidence": 65,
                }
    return new_names


# ---- Strategy 3: Field name string clusters ----
FIELD_GROUP_MAP = {
    "task_fields": [
        "ACTUALCOST", "ACTUALWORK", "ACWP", "BCWP", "BCWS", "BASELINECOST",
        "BASELINEWORK", "CONSTRAINTTYPE", "CONSTRAINTDATE", "CRITICALTASK",
        "DURATION", "EARLYFINISH", "EARLYSTART", "LATEFINISH", "LATESTART",
        "FIXEDCOST", "FREESLACK", "TOTALSLACK", "PERCENTCOMPLETE",
        "REMAININGCOST", "REMAININGWORK", "PRIORITY", "RESUMEDATE",
        "STOPDATE", "OUTLINELEVEL", "OUTLINENUMBER", "WBS",
        "PREDECESSORS", "SUCCESSORS", "CHILDREN", "PARENT",
    ],
    "resource_fields": [
        "MAXUNITS", "PEAKUNITS", "OVERALLOCATED", "STANDARDRATE",
        "OVERTIMERATE", "COSTPERTOUSE", "ACCRUEAT", "RESOURCEGROUP",
        "EMAILADDRESS", "WINDOWSACCOUNT", "RESOURCECALENDAR",
        "RESOURCETYPE", "AVAILABLEFROM", "AVAILABLETO",
    ],
    "assignment_fields": [
        "ASSIGNMENTDELAY", "ASSIGNMENTUNITS", "TIMEPHASEDWORK",
        "ASSIGNMENTID", "TASKID", "RESOURCEID",
    ],
    "calendar_fields": [
        "WORKINGDAY", "NONWORKINGDAY", "FROMDATE", "TODATE",
        "FROMTIME", "TOTIME", "DAYOFWEEK",
    ],
}


def name_from_field_clusters(func_strings, known, funcs, modules):
    """Functions referencing MS Project field names get named by field group."""
    new_names = {}
    all_field_names = set()
    for group, fields in FIELD_GROUP_MAP.items():
        all_field_names.update(fields)

    for addr, strings in func_strings.items():
        if addr in known:
            continue
        if addr not in funcs:
            continue

        field_hits = collections.Counter()
        for s in strings:
            s_upper = s.strip().upper()
            for group, fields in FIELD_GROUP_MAP.items():
                if s_upper in fields:
                    field_hits[group] += 1

        if field_hits:
            best_group, count = field_hits.most_common(1)[0]
            if count >= 3:
                mod = modules.get(addr, "unknown")
                new_names[addr] = {
                    "assigned_name": f"{best_group}_handler",
                    "source": "string_field_cluster",
                    "confidence": 45,
                }
    return new_names


# ---- Strategy 4: Import-based naming ----
IMPORT_PATTERNS = {
    "registry_access": {"RegOpenKeyExW", "RegQueryValueExW", "RegSetValueExW",
                        "RegCloseKey", "RegCreateKeyExW", "RegDeleteValueW",
                        "RegEnumKeyExW", "RegEnumValueW"},
    "file_io": {"CreateFileW", "ReadFile", "WriteFile", "CloseHandle",
                "GetFileSize", "SetFilePointer", "SetEndOfFile",
                "GetFileAttributesW", "GetFileAttributesExW",
                "DeleteFileW", "MoveFileExW", "CopyFileW"},
    "memory_mapped_io": {"CreateFileMappingW", "MapViewOfFile",
                         "UnmapViewOfFile", "FlushViewOfFile"},
    "heap_alloc": {"HeapAlloc", "HeapFree", "HeapReAlloc",
                   "HeapCreate", "HeapDestroy", "HeapSize"},
    "dialog_ui": {"DialogBoxParamW", "EndDialog", "GetDlgItem",
                  "SetDlgItemTextW", "GetDlgItemTextW", "SendDlgItemMessageW",
                  "EnableWindow", "ShowWindow"},
    "gdi_drawing": {"CreatePen", "CreateSolidBrush", "SelectObject",
                    "DeleteObject", "BitBlt", "StretchBlt",
                    "MoveToEx", "LineTo", "Rectangle", "Ellipse",
                    "SetBkMode", "SetTextColor", "TextOutW",
                    "CreateCompatibleDC", "CreateCompatibleBitmap"},
    "window_mgmt": {"CreateWindowExW", "DestroyWindow", "SetWindowPos",
                    "GetWindowRect", "GetClientRect", "InvalidateRect",
                    "UpdateWindow", "SetWindowTextW", "GetWindowTextW",
                    "PostMessageW", "SendMessageW", "DefWindowProcW"},
    "com_ole": {"CoCreateInstance", "CoInitializeEx", "OleInitialize",
                "CoGetClassObject", "CLSIDFromProgID", "StringFromGUID2",
                "SysAllocString", "SysFreeString", "SafeArrayCreate",
                "VariantInit", "VariantClear", "VariantChangeType"},
    "string_ops": {"lstrcpyW", "lstrcmpW", "lstrcmpiW", "lstrlenW",
                   "CompareStringW", "CharUpperW", "CharLowerW"},
    "crypto": {"CryptAcquireContextW", "CryptCreateHash", "CryptHashData",
               "CryptDeriveKey", "CryptEncrypt", "CryptDecrypt",
               "CryptReleaseContext"},
    "network": {"InternetOpenW", "InternetConnectW", "HttpOpenRequestW",
                "HttpSendRequestW", "InternetReadFile", "WinHttpOpen",
                "WinHttpConnect", "WinHttpOpenRequest", "WinHttpSendRequest"},
}


def name_from_imports(func_imports, known, funcs):
    new_names = {}
    for addr, imports in func_imports.items():
        if addr in known:
            continue
        if addr not in funcs:
            continue

        import_names = set(name for lib, name in imports)
        scores = {}
        for pattern, apis in IMPORT_PATTERNS.items():
            overlap = import_names & apis
            if overlap:
                scores[pattern] = len(overlap)

        if scores:
            best = max(scores, key=scores.get)
            if scores[best] >= 2:
                new_names[addr] = {
                    "assigned_name": f"{best}_func",
                    "source": "import_pattern",
                    "confidence": 40,
                }
    return new_names


# ---- Strategy 5: Call graph propagation ----
def propagate_from_call_graph(callers_of, callees_of, known, funcs, iterations=15):
    """
    Multi-pass propagation:
    Pass A: Single-caller inheritance - if a function has exactly one caller
            and that caller is named, derive a name from the caller.
    Pass B: Dominant-class inheritance - if >70% of a function's callers
            share the same class prefix, assign that class.
    Pass C: Callee-based inference - if a function's callees are dominated
            by one class, it likely belongs to that class.
    """
    new_names = {}
    current_known = dict(known)

    for iteration in range(iterations):
        added_this_pass = 0

        # Pass A: Single-caller
        for addr in funcs:
            if addr in current_known:
                continue
            crs = callers_of.get(addr, set())
            if len(crs) != 1:
                continue
            caller = next(iter(crs))
            if caller not in current_known:
                continue

            caller_name = current_known[caller]["assigned_name"]
            caller_cls = extract_class(caller_name)
            if caller_cls:
                ces = callees_of.get(caller, set())
                unnamed_ces = [c for c in ces if c not in current_known and c != addr]
                idx = 0
                for c in sorted(ces):
                    if c == addr:
                        break
                    if c not in current_known:
                        idx += 1

                name = f"{caller_cls}::sub_{idx}"
                new_names[addr] = {
                    "assigned_name": name,
                    "source": f"callgraph_single_caller_i{iteration}",
                    "confidence": 55 - iteration,
                }
                current_known[addr] = new_names[addr]
                added_this_pass += 1

        # Pass B: Dominant-class from callers
        for addr in funcs:
            if addr in current_known:
                continue
            crs = callers_of.get(addr, set())
            if len(crs) < 2:
                continue

            classes = collections.Counter()
            named_count = 0
            for cr in crs:
                if cr in current_known:
                    named_count += 1
                    cls = extract_class(current_known[cr]["assigned_name"])
                    if cls:
                        classes[cls] += 1

            if named_count == 0:
                continue
            if not classes:
                continue

            best_cls, best_count = classes.most_common(1)[0]
            ratio = best_count / len(crs)
            if ratio >= 0.7 and best_count >= 2:
                new_names[addr] = {
                    "assigned_name": f"{best_cls}::helper",
                    "source": f"callgraph_dominant_caller_i{iteration}",
                    "confidence": max(35, int(55 * ratio) - iteration),
                }
                current_known[addr] = new_names[addr]
                added_this_pass += 1

        # Pass C: Dominant-class from callees
        for addr in funcs:
            if addr in current_known:
                continue
            ces = callees_of.get(addr, set())
            if len(ces) < 2:
                continue

            classes = collections.Counter()
            named_count = 0
            for ce in ces:
                if ce in current_known:
                    named_count += 1
                    cls = extract_class(current_known[ce]["assigned_name"])
                    if cls:
                        classes[cls] += 1

            if named_count < 2:
                continue
            if not classes:
                continue

            best_cls, best_count = classes.most_common(1)[0]
            ratio = best_count / max(len(ces), 1)
            if ratio >= 0.6 and best_count >= 2:
                new_names[addr] = {
                    "assigned_name": f"{best_cls}::caller",
                    "source": f"callgraph_dominant_callee_i{iteration}",
                    "confidence": max(30, int(50 * ratio) - iteration),
                }
                current_known[addr] = new_names[addr]
                added_this_pass += 1

        if added_this_pass == 0:
            print(f"  Call graph propagation converged at iteration {iteration}")
            break
        print(f"  Iteration {iteration}: +{added_this_pass} names")

    return new_names


# ---- Strategy 6: Thunk/wrapper detection ----
def name_thunks(callees_of, known, funcs):
    """Small functions (< 20 bytes) that call exactly one named function are wrappers."""
    new_names = {}
    for addr, info in funcs.items():
        if addr in known:
            continue
        if info["bytes"] > 20:
            continue
        if info["thunk"]:
            continue
        ces = callees_of.get(addr, set())
        if len(ces) != 1:
            continue
        target = next(iter(ces))
        if target not in known:
            continue
        target_name = known[target]["assigned_name"]
        new_names[addr] = {
            "assigned_name": f"thunk_{target_name}",
            "source": "thunk_detection",
            "confidence": 70,
        }
    return new_names


# ---- Strategy 7: Address locality class inference ----
def name_from_address_locality(funcs, known):
    """
    Functions adjacent in memory are from the same compilation unit.
    For each unnamed function, check its nearest named neighbors.
    If the nearest N named neighbors all share the same class, assign it.
    """
    new_names = {}
    sorted_addrs = sorted(funcs.keys())
    addr_to_idx = {a: i for i, a in enumerate(sorted_addrs)}

    for addr in sorted_addrs:
        if addr in known:
            continue

        idx = addr_to_idx[addr]
        neighbor_classes = collections.Counter()

        for direction in (-1, 1):
            for step in range(1, 8):
                ni = idx + direction * step
                if 0 <= ni < len(sorted_addrs):
                    na = sorted_addrs[ni]
                    if na in known:
                        cls = extract_class(known[na]["assigned_name"])
                        if cls:
                            weight = 8 - step
                            neighbor_classes[cls] += weight
                        break

        if not neighbor_classes:
            continue

        best_cls, best_score = neighbor_classes.most_common(1)[0]
        total_score = sum(neighbor_classes.values())

        if best_score >= 10 and best_score / total_score >= 0.7:
            new_names[addr] = {
                "assigned_name": f"{best_cls}::local",
                "source": "address_locality",
                "confidence": min(40, int(30 * best_score / total_score)),
            }
    return new_names


# ---- Strategy 8: Relaxed call graph (any named caller) ----
def propagate_relaxed(callers_of, callees_of, known, funcs, iterations=10):
    """
    Relaxed propagation: if ANY caller is named, use its class.
    Lower confidence than strict propagation.
    Also: if a function calls a named function and is called by another
    named function of the same class, it's a member of that class.
    """
    new_names = {}
    current_known = dict(known)

    for iteration in range(iterations):
        added = 0

        for addr in funcs:
            if addr in current_known:
                continue

            crs = callers_of.get(addr, set())
            ces = callees_of.get(addr, set())

            caller_classes = collections.Counter()
            for cr in crs:
                if cr in current_known:
                    cls = extract_class(current_known[cr]["assigned_name"])
                    if cls:
                        caller_classes[cls] += 1

            callee_classes = collections.Counter()
            for ce in ces:
                if ce in current_known:
                    cls = extract_class(current_known[ce]["assigned_name"])
                    if cls:
                        callee_classes[cls] += 1

            if not caller_classes and not callee_classes:
                continue

            combined = collections.Counter()
            for cls, cnt in caller_classes.items():
                combined[cls] += cnt * 2
            for cls, cnt in callee_classes.items():
                combined[cls] += cnt

            best_cls, best_score = combined.most_common(1)[0]
            total = sum(combined.values())
            ratio = best_score / total

            if ratio >= 0.5 and best_score >= 3:
                new_names[addr] = {
                    "assigned_name": f"{best_cls}::related",
                    "source": f"callgraph_relaxed_i{iteration}",
                    "confidence": max(25, int(40 * ratio) - iteration),
                }
                current_known[addr] = new_names[addr]
                added += 1

        if added == 0:
            print(f"  Relaxed propagation converged at iteration {iteration}")
            break
        print(f"  Relaxed iteration {iteration}: +{added} names")

    return new_names


# ---- Strategy 10: Final sweep with very low thresholds ----
def final_sweep(callers_of, callees_of, known, funcs):
    """
    Last-chance naming: if a function has ANY named neighbor with a class,
    use that class. Lowest confidence tier.
    """
    new_names = {}
    current_known = dict(known)

    for iteration in range(5):
        added = 0
        for addr in funcs:
            if addr in current_known:
                continue
            crs = callers_of.get(addr, set())
            ces = callees_of.get(addr, set())

            classes = collections.Counter()
            for n in crs:
                if n in current_known:
                    cls = extract_class(current_known[n]["assigned_name"])
                    if cls:
                        classes[cls] += 2
            for n in ces:
                if n in current_known:
                    cls = extract_class(current_known[n]["assigned_name"])
                    if cls:
                        classes[cls] += 1

            if not classes:
                continue

            best_cls, best_score = classes.most_common(1)[0]
            if best_score >= 2:
                new_names[addr] = {
                    "assigned_name": f"{best_cls}::func",
                    "source": f"final_sweep_i{iteration}",
                    "confidence": 20,
                }
                current_known[addr] = new_names[addr]
                added += 1

        if added == 0:
            print(f"  Final sweep converged at iteration {iteration}")
            break
        print(f"  Final sweep iteration {iteration}: +{added} names")

    return new_names


# ---- Strategy 11: Module-based naming for leftovers ----
def name_from_module(known, funcs, modules):
    """
    For functions that still have no name but have a module assignment,
    give them module::func_N. Better than nothing for navigation.
    """
    new_names = {}
    for addr in funcs:
        if addr in known:
            continue
        mod = modules.get(addr, "unknown")
        if mod == "unknown":
            continue
        short_mod = mod.split(".")[-1]
        new_names[addr] = {
            "assigned_name": f"{short_mod}::func",
            "source": "module_based",
            "confidence": 15,
        }
    return new_names


# ---- Strategy 9: Deduplicate helper names with ordinals ----
def deduplicate_names(all_names):
    """Add ordinals to duplicate names within the same class."""
    name_counts = collections.Counter()
    for addr, info in all_names.items():
        name_counts[info["assigned_name"]] += 1

    duplicates = {n for n, c in name_counts.items() if c > 1}
    if not duplicates:
        return all_names

    dup_ordinals = collections.defaultdict(int)
    sorted_addrs = sorted(all_names.keys())
    for addr in sorted_addrs:
        name = all_names[addr]["assigned_name"]
        if name in duplicates:
            ordinal = dup_ordinals[name]
            dup_ordinals[name] += 1
            if "::" in name:
                cls, method = name.rsplit("::", 1)
                all_names[addr]["assigned_name"] = f"{cls}::{method}_{ordinal}"
            else:
                all_names[addr]["assigned_name"] = f"{name}_{ordinal}"

    return all_names


def main():
    print("Phase 1: Advanced function naming")
    print("=" * 60)

    print("Loading data...")
    funcs = load_functions()
    phase0 = load_phase0_names()
    callers_of, callees_of = load_call_graph()
    func_strings = load_strings()
    func_imports = load_imports()
    modules = load_modules()

    orig_named = {}
    for addr, info in funcs.items():
        if not info["name"].startswith("FUN_"):
            orig_named[addr] = {
                "assigned_name": info["name"],
                "source": "original",
                "confidence": 100,
            }

    known = {}
    known.update(orig_named)
    known.update(phase0)
    print(f"  Starting known: {len(known)} ({len(orig_named)} original + {len(phase0)} phase0)")

    all_new = {}
    stats = collections.Counter()

    # Strategy 1: C++ scope strings
    print()
    print("Strategy 1: C++ scope strings...")
    s1 = name_from_cpp_strings(func_strings, known, funcs)
    print(f"  -> {len(s1)} functions named")
    all_new.update(s1)
    known.update(s1)
    stats["string_cpp_scope"] = len(s1)

    # Strategy 2: Diagnostic strings
    print("Strategy 2: Diagnostic/log strings...")
    s2 = name_from_diagnostic_strings(func_strings, known, funcs)
    print(f"  -> {len(s2)} functions named")
    all_new.update(s2)
    known.update(s2)
    stats["string_diagnostic"] = len(s2)

    # Strategy 3: Thunk detection (before call graph, to seed it)
    print("Strategy 3: Thunk/wrapper detection...")
    s3 = name_thunks(callees_of, known, funcs)
    print(f"  -> {len(s3)} functions named")
    all_new.update(s3)
    known.update(s3)
    stats["thunk_detection"] = len(s3)

    # Strategy 4: Call graph propagation (biggest contributor)
    print("Strategy 4: Call graph propagation (up to 15 iterations)...")
    s4 = propagate_from_call_graph(callers_of, callees_of, known, funcs, iterations=15)
    print(f"  -> {len(s4)} functions named")
    all_new.update(s4)
    known.update(s4)
    stats["callgraph"] = len(s4)

    # Strategy 5: Import patterns
    print("Strategy 5: Import-based naming...")
    s5 = name_from_imports(func_imports, known, funcs)
    print(f"  -> {len(s5)} functions named")
    all_new.update(s5)
    known.update(s5)
    stats["import_pattern"] = len(s5)

    # Strategy 6: Field cluster naming
    print("Strategy 6: Field cluster naming...")
    s6 = name_from_field_clusters(func_strings, known, funcs, modules)
    print(f"  -> {len(s6)} functions named")
    all_new.update(s6)
    known.update(s6)
    stats["field_cluster"] = len(s6)

    # Strategy 7: Address locality
    print("Strategy 7: Address locality class inference...")
    s7 = name_from_address_locality(funcs, known)
    print(f"  -> {len(s7)} functions named")
    all_new.update(s7)
    known.update(s7)
    stats["address_locality"] = len(s7)

    # Strategy 8: Relaxed call graph (second round with more seeds)
    print("Strategy 8: Relaxed call graph propagation...")
    s8 = propagate_relaxed(callers_of, callees_of, known, funcs, iterations=10)
    print(f"  -> {len(s8)} functions named")
    all_new.update(s8)
    known.update(s8)
    stats["callgraph_relaxed"] = len(s8)

    # Strategy 9: Second round of thunks (new targets after propagation)
    print("Strategy 9: Second-round thunk detection...")
    s9 = name_thunks(callees_of, known, funcs)
    print(f"  -> {len(s9)} functions named")
    all_new.update(s9)
    known.update(s9)
    stats["thunk_round2"] = len(s9)

    # Strategy 10: Final sweep - any named neighbor at all
    print("Strategy 10: Final sweep - any class signal from neighbors...")
    s10 = final_sweep(callers_of, callees_of, known, funcs)
    print(f"  -> {len(s10)} functions named")
    all_new.update(s10)
    known.update(s10)
    stats["final_sweep"] = len(s10)

    # Strategy 11: Module-based naming for stubborn functions
    print("Strategy 11: Module-based naming for remaining...")
    s11 = name_from_module(known, funcs, modules)
    print(f"  -> {len(s11)} functions named")
    all_new.update(s11)
    known.update(s11)
    stats["module_based"] = len(s11)

    # Deduplicate
    print()
    print("Deduplicating names...")
    all_new = deduplicate_names(all_new)

    # Merge with phase0
    merged = {}
    merged.update(phase0)
    merged.update(all_new)

    total_unnamed = sum(1 for info in funcs.values() if info["name"].startswith("FUN_"))
    total_now_named = len(merged)
    total_with_orig = total_now_named + len(orig_named)

    print()
    print("=" * 60)
    print(f"Phase 1 results:")
    print(f"  Total functions:            {len(funcs)}")
    print(f"  Originally named:           {len(orig_named)}")
    print(f"  Phase 0 named:              {len(phase0)}")
    print(f"  Phase 1 NEW names:          {len(all_new)}")
    print(f"  Total named (all sources):  {total_with_orig}")
    print(f"  Coverage:                   {total_with_orig / len(funcs) * 100:.1f}%")
    print()
    print("  Phase 1 breakdown:")
    for source, count in sorted(stats.items(), key=lambda x: -x[1]):
        print(f"    {source:<30s} {count:>6}")

    # Write output
    out_path = os.path.join(BASE, "analysis/named_functions_p1.tsv")
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(["func_addr", "assigned_name", "source", "confidence"])
        for addr in sorted(merged.keys()):
            info = merged[addr]
            writer.writerow([
                addr,
                info["assigned_name"],
                info["source"],
                info["confidence"],
            ])
    print(f"  Wrote {len(merged)} entries to {out_path}")

    # Write summary
    summary_path = os.path.join(BASE, "analysis/phase1_summary.txt")
    with open(summary_path, "w", encoding="utf-8") as f:
        f.write("Phase 1: Advanced Function Naming Summary\n")
        f.write("=" * 60 + "\n\n")
        f.write(f"Total functions:            {len(funcs)}\n")
        f.write(f"Originally named:           {len(orig_named)}\n")
        f.write(f"Phase 0 named:              {len(phase0)}\n")
        f.write(f"Phase 1 NEW names:          {len(all_new)}\n")
        f.write(f"Total named (all sources):  {total_with_orig}\n")
        f.write(f"Coverage:                   {total_with_orig / len(funcs) * 100:.1f}%\n\n")
        f.write("Phase 1 breakdown:\n")
        for source, count in sorted(stats.items(), key=lambda x: -x[1]):
            f.write(f"  {source:<30s} {count:>6}\n")

        f.write("\n\nSample names by strategy:\n")
        f.write("-" * 60 + "\n")
        for source in sorted(stats.keys()):
            f.write(f"\n{source}:\n")
            examples = [(a, i) for a, i in all_new.items() if i["source"].startswith(source.split("_i")[0])]
            for addr, info in examples[:10]:
                f.write(f"  {addr}  {info['assigned_name']:<50s} conf={info['confidence']}\n")

    print(f"  Wrote summary to {summary_path}")


if __name__ == "__main__":
    main()
