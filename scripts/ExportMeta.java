// Dumps structural metadata for whole-program analysis: functions, call graph,
// strings and their referencing functions, imports, exports, symbols, vtables.
// Usage (headless): -postScript ExportMeta.java <outDir>
//@category Export

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.*;
import ghidra.program.util.DefinedStringIterator;

public class ExportMeta extends GhidraScript {

	private PrintWriter open(File dir, String name) throws IOException {
		return new PrintWriter(new BufferedWriter(new OutputStreamWriter(
			new FileOutputStream(new File(dir, name)), StandardCharsets.UTF_8)));
	}

	private static String esc(String s) {
		if (s == null) {
			return "";
		}
		return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
	}

	private String funcAt(Address a) {
		Function f = getFunctionContaining(a);
		return f == null ? "" : f.getEntryPoint().toString();
	}

	@Override
	protected void run() throws Exception {
		File out = new File(getScriptArgs()[0]);
		out.mkdirs();
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		ReferenceManager rm = currentProgram.getReferenceManager();
		SymbolTable st = currentProgram.getSymbolTable();

		// functions + call graph
		int nf = 0;
		try (PrintWriter pf = open(out, "functions.tsv"); PrintWriter pc = open(out, "calls.tsv")) {
			pf.println("entry\tname\tnamespace\tbytes\tthunk\tcallers\tcallees\tsignature");
			pc.println("caller\tcallee\tcallee_name");
			for (Function f : fm.getFunctions(true)) {
				monitor.checkCancelled();
				Set<Function> callees = f.getCalledFunctions(monitor);
				Set<Function> callers = f.getCallingFunctions(monitor);
				pf.println(f.getEntryPoint() + "\t" + esc(f.getName()) + "\t" +
					esc(f.getParentNamespace().getName(true)) + "\t" + f.getBody().getNumAddresses() + "\t" +
					f.isThunk() + "\t" + callers.size() + "\t" + callees.size() + "\t" +
					esc(f.getSignature().getPrototypeString()));
				for (Function c : callees) {
					String cn = c.isExternal() ? c.getExternalLocation().getLibraryName() + "!" + c.getName()
							: c.isThunk() && c.getThunkedFunction(true).isExternal()
									? c.getThunkedFunction(true).getExternalLocation().getLibraryName() + "!" +
										c.getThunkedFunction(true).getName()
									: c.getName(true);
					pc.println(f.getEntryPoint() + "\t" + c.getEntryPoint() + "\t" + esc(cn));
				}
				nf++;
			}
		}
		println("functions: " + nf);

		// strings with referencing functions
		int ns = 0;
		try (PrintWriter ps = open(out, "strings.tsv")) {
			ps.println("addr\ttype\tfuncs\tvalue");
			DefinedStringIterator dsi = DefinedStringIterator.forProgram(currentProgram);
			while (dsi.hasNext()) {
				Data d = dsi.next();
				monitor.checkCancelled();
				StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
				String v = sdi.getStringValue();
				if (v == null || v.length() < 3) {
					continue;
				}
				Set<String> fs = new TreeSet<>();
				for (Reference r : rm.getReferencesTo(d.getAddress())) {
					String fa = funcAt(r.getFromAddress());
					if (!fa.isEmpty()) {
						fs.add(fa);
					}
				}
				ps.println(d.getAddress() + "\t" + d.getDataType().getName() + "\t" + String.join(",", fs) +
					"\t" + esc(v));
				ns++;
			}
		}
		println("strings: " + ns);

		// imports with referencing functions (via thunks and direct IAT refs)
		try (PrintWriter pi = open(out, "imports.tsv")) {
			pi.println("library\tname\tfuncs");
			for (Symbol s : st.getExternalSymbols()) {
				monitor.checkCancelled();
				ExternalLocation el = s.getObject() instanceof Function
						? ((Function) s.getObject()).getExternalLocation()
						: currentProgram.getExternalManager().getExternalLocation(s);
				String lib = el != null ? el.getLibraryName() : "?";
				Set<String> fs = new TreeSet<>();
				for (Reference r : s.getReferences()) {
					String fa = funcAt(r.getFromAddress());
					if (!fa.isEmpty()) {
						fs.add(fa);
					}
					// IAT slot: follow references to the pointer
					for (Reference r2 : rm.getReferencesTo(r.getFromAddress())) {
						Function f2 = getFunctionContaining(r2.getFromAddress());
						if (f2 != null) {
							fs.add(f2.getEntryPoint().toString());
							if (f2.isThunk()) {
								for (Function c : f2.getCallingFunctions(monitor)) {
									fs.add(c.getEntryPoint().toString());
								}
							}
						}
					}
				}
				pi.println(lib + "\t" + esc(s.getName()) + "\t" + String.join(",", fs));
			}
		}

		// exports
		try (PrintWriter pe = open(out, "exports.tsv")) {
			pe.println("addr\tname");
			AddressIterator it = st.getExternalEntryPointIterator();
			while (it.hasNext()) {
				Address a = it.next();
				Symbol s = st.getPrimarySymbol(a);
				pe.println(a + "\t" + esc(s == null ? "" : s.getName(true)));
			}
		}

		// non-default symbols (labels, RTTI, vftables, class namespaces)
		try (PrintWriter py = open(out, "symbols.tsv")) {
			py.println("addr\ttype\tsource\tname");
			for (Symbol s : st.getAllSymbols(true)) {
				monitor.checkCancelled();
				if (s.getSource() == SourceType.DEFAULT || s.isExternal()) {
					continue;
				}
				SymbolType t = s.getSymbolType();
				if (t == SymbolType.FUNCTION && s.getName().startsWith("FUN_")) {
					continue;
				}
				py.println(s.getAddress() + "\t" + t + "\t" + s.getSource() + "\t" + esc(s.getName(true)));
			}
		}

		// vtables: slots of every *vftable* symbol
		int nv = 0;
		int ptr = currentProgram.getDefaultPointerSize();
		try (PrintWriter pv = open(out, "vtables.tsv")) {
			pv.println("vtable_addr\tclass\tslot\tfunc");
			for (Symbol s : st.getAllSymbols(true)) {
				monitor.checkCancelled();
				if (!s.getName().contains("vftable")) {
					continue;
				}
				String cls = s.getParentNamespace().getName(true);
				Address a = s.getAddress();
				for (int i = 0; i < 2000; i++) {
					Address slot = a.add((long) i * ptr);
					if (i > 0 && st.getPrimarySymbol(slot) != null &&
						st.getPrimarySymbol(slot).getSource() != SourceType.DEFAULT) {
						break; // next labelled object
					}
					long v;
					try {
						v = ptr == 8 ? getLong(slot) : getInt(slot) & 0xffffffffL;
					}
					catch (MemoryAccessException e) {
						break;
					}
					Address target = toAddr(v);
					Function f = fm.getFunctionAt(target);
					if (f == null) {
						break;
					}
					pv.println(a + "\t" + esc(cls) + "\t" + i + "\t" + target);
				}
				nv++;
			}
		}
		println("vftables: " + nv);

		// memory map
		try (PrintWriter pm = open(out, "memory.tsv")) {
			pm.println("name\tstart\tend\tperms");
			for (var b : currentProgram.getMemory().getBlocks()) {
				pm.println(b.getName() + "\t" + b.getStart() + "\t" + b.getEnd() + "\t" +
					(b.isRead() ? "r" : "-") + (b.isWrite() ? "w" : "-") + (b.isExecute() ? "x" : "-"));
			}
		}
		println("done");
	}
}
