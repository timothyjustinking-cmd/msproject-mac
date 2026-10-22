// Imports function names from named_functions_p1.tsv back into the Ghidra program.
// Renames FUN_* functions to their assigned names, sets namespaces from Class::Method,
// adds plate comments with source/confidence, and tags functions by module.
//
// Usage (headless):  -postScript ImportNames.java <tsvPath> [moduleMapPath]
// Usage (GUI):       Run from Script Manager; prompts for the TSV file.
//
// The TSV must have columns: func_addr, assigned_name, source, confidence
// The optional module map TSV: func_addr, func_name, module, assigned_name
//@category Import

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ImportNames extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();

        File tsvFile;
        File moduleFile = null;

        if (args.length >= 1) {
            tsvFile = new File(args[0]);
            if (args.length >= 2) {
                moduleFile = new File(args[1]);
            }
        } else {
            tsvFile = askFile("Select named_functions_p1.tsv", "Open");
            if (tsvFile == null) return;
            File mf = askFile("Select module_map.tsv (optional, Cancel to skip)", "Open");
            if (mf != null && mf.exists()) {
                moduleFile = mf;
            }
        }

        if (!tsvFile.exists()) {
            printerr("File not found: " + tsvFile.getAbsolutePath());
            return;
        }

        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        Listing listing = currentProgram.getListing();

        // Phase 1: Import function names
        println("Reading " + tsvFile.getName() + "...");
        int renamed = 0;
        int skipped = 0;
        int notFound = 0;
        int alreadyNamed = 0;
        int namespaced = 0;
        int plated = 0;
        int total = 0;

        int txId = currentProgram.startTransaction("Import Phase 1 names");
        try {
            try (BufferedReader br = new BufferedReader(
                    new InputStreamReader(new FileInputStream(tsvFile), StandardCharsets.UTF_8))) {

                String header = br.readLine();
                if (header == null || !header.startsWith("func_addr")) {
                    printerr("Bad header: " + header);
                    return;
                }

                String line;
                while ((line = br.readLine()) != null) {
                    monitor.checkCancelled();
                    total++;

                    String[] cols = line.split("\t", -1);
                    if (cols.length < 4) continue;

                    String addrStr = cols[0].trim();
                    String assignedName = cols[1].trim();
                    String source = cols[2].trim();
                    String confidence = cols[3].trim();

                    if (assignedName.isEmpty()) {
                        skipped++;
                        continue;
                    }

                    Address addr;
                    try {
                        addr = space.getAddress(addrStr);
                    } catch (Exception e) {
                        skipped++;
                        continue;
                    }

                    Function func = fm.getFunctionAt(addr);
                    if (func == null) {
                        notFound++;
                        continue;
                    }

                    String currentName = func.getName();
                    if (!currentName.startsWith("FUN_")) {
                        alreadyNamed++;
                        continue;
                    }

                    // Parse Class::Method into namespace + method
                    String newName;
                    String namespaceName = null;
                    if (assignedName.contains("::")) {
                        int sep = assignedName.lastIndexOf("::");
                        namespaceName = assignedName.substring(0, sep);
                        newName = assignedName.substring(sep + 2);
                    } else {
                        newName = assignedName;
                    }

                    // Sanitize: Ghidra doesn't allow certain characters
                    newName = sanitize(newName);
                    if (newName.isEmpty()) {
                        skipped++;
                        continue;
                    }

                    // Apply namespace
                    if (namespaceName != null && !namespaceName.isEmpty()) {
                        namespaceName = sanitize(namespaceName);
                        try {
                            Namespace ns = getOrCreateNamespace(st, namespaceName);
                            if (ns != null) {
                                func.setParentNamespace(ns);
                                namespaced++;
                            }
                        } catch (Exception e) {
                            // Namespace collision; continue with rename anyway
                        }
                    }

                    // Rename the function
                    try {
                        func.setName(newName, SourceType.ANALYSIS);
                        renamed++;
                    } catch (Exception e) {
                        // Duplicate name in same namespace: append address suffix
                        try {
                            String fallback = newName + "_" + addrStr.substring(Math.max(0, addrStr.length() - 4));
                            func.setName(fallback, SourceType.ANALYSIS);
                            renamed++;
                        } catch (Exception e2) {
                            skipped++;
                            continue;
                        }
                    }

                    // Add plate comment with provenance
                    String plate = func.getComment();
                    String tag = "[P1:" + source + " conf=" + confidence + "]";
                    if (plate == null || plate.isEmpty()) {
                        func.setComment(tag);
                    } else if (!plate.contains("[P1:")) {
                        func.setComment(tag + "\n" + plate);
                    }
                    plated++;

                    if (total % 5000 == 0) {
                        println("  Processed " + total + " entries (" + renamed + " renamed)...");
                    }
                }
            }

            // Phase 2: Import module tags as bookmark categories
            if (moduleFile != null && moduleFile.exists()) {
                println("Reading module map: " + moduleFile.getName() + "...");
                int moduleTagged = 0;
                BookmarkManager bm = currentProgram.getBookmarkManager();

                try (BufferedReader br = new BufferedReader(
                        new InputStreamReader(new FileInputStream(moduleFile), StandardCharsets.UTF_8))) {

                    String header = br.readLine();
                    String line;
                    while ((line = br.readLine()) != null) {
                        monitor.checkCancelled();
                        String[] cols = line.split("\t", -1);
                        if (cols.length < 3) continue;

                        String addrStr = cols[0].trim();
                        String module = cols[2].trim();

                        if (module.isEmpty() || module.equals("unknown")) continue;

                        Address addr;
                        try {
                            addr = space.getAddress(addrStr);
                        } catch (Exception e) {
                            continue;
                        }

                        Function func = fm.getFunctionAt(addr);
                        if (func == null) continue;

                        bm.setBookmark(addr, "Module", module,
                            "Module: " + module);
                        moduleTagged++;

                        if (moduleTagged % 10000 == 0) {
                            println("  Module-tagged " + moduleTagged + "...");
                        }
                    }
                }
                println("Module bookmarks set: " + moduleTagged);
            }

        } finally {
            currentProgram.endTransaction(txId, true);
        }

        println("");
        println("========================================");
        println("Import complete:");
        println("  Total entries:     " + total);
        println("  Renamed:           " + renamed);
        println("  Namespaced:        " + namespaced);
        println("  Plate comments:    " + plated);
        println("  Already named:     " + alreadyNamed);
        println("  Not found:         " + notFound);
        println("  Skipped:           " + skipped);
        println("========================================");
    }

    private String sanitize(String name) {
        // Replace characters Ghidra rejects in symbol names
        name = name.replace("<", "_").replace(">", "_")
                   .replace(" ", "_").replace(",", "_")
                   .replace("(", "_").replace(")", "_")
                   .replace("[", "_").replace("]", "_")
                   .replace("{", "_").replace("}", "_")
                   .replace("~", "dtor_")
                   .replace("*", "ptr");

        // Strip leading digits
        while (!name.isEmpty() && Character.isDigit(name.charAt(0))) {
            name = name.substring(1);
        }

        // Collapse multiple underscores
        while (name.contains("__")) {
            name = name.replace("__", "_");
        }

        // Trim trailing underscores
        while (name.endsWith("_")) {
            name = name.substring(0, name.length() - 1);
        }

        return name;
    }

    private Namespace getOrCreateNamespace(SymbolTable st, String name)
            throws Exception {
        // Handle nested namespaces: A::B::C
        Namespace parent = currentProgram.getGlobalNamespace();
        String[] parts = name.split("::");
        for (String part : parts) {
            part = sanitize(part);
            if (part.isEmpty()) continue;

            Namespace child = null;
            for (Symbol sym : st.getSymbols(part, parent)) {
                if (sym.getSymbolType() == SymbolType.NAMESPACE ||
                    sym.getSymbolType() == SymbolType.CLASS) {
                    child = (Namespace) sym.getObject();
                    break;
                }
            }
            if (child == null) {
                child = st.createNameSpace(parent, part, SourceType.ANALYSIS);
            }
            parent = child;
        }
        return parent;
    }
}
