// Decompiles every function in the current program to C, in parallel.
// Output: <outDir>/part_NNNN.c, each holding a run of functions in address order,
// plus failures.txt and summary.txt.
// Usage (headless): -postScript ExportAllC.java <outDir> [threads] [chunkSize] [timeoutSecs]
//@category Export

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.util.task.TaskMonitor;

public class ExportAllC extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args[0]);
		int threads = args.length > 1 ? Integer.parseInt(args[1]) : Runtime.getRuntime().availableProcessors();
		int chunkSize = args.length > 2 ? Integer.parseInt(args[2]) : 2000;
		int timeout = args.length > 3 ? Integer.parseInt(args[3]) : 60;
		outDir.mkdirs();

		List<Function> funcs = new ArrayList<>();
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (!f.isExternal() && !f.isThunk()) {
				funcs.add(f);
			}
		}
		int total = funcs.size();
		println("Functions to decompile: " + total + " on " + threads + " threads");

		ThreadLocal<DecompInterface> ifc = ThreadLocal.withInitial(() -> {
			DecompInterface d = new DecompInterface();
			DecompileOptions o = new DecompileOptions();
			o.grabFromProgram(currentProgram);
			d.setOptions(o);
			d.toggleCCode(true);
			d.toggleSyntaxTree(false);
			d.setSimplificationStyle("decompile");
			d.openProgram(currentProgram);
			return d;
		});

		AtomicInteger done = new AtomicInteger();
		AtomicInteger ok = new AtomicInteger();
		ConcurrentLinkedQueue<String> failures = new ConcurrentLinkedQueue<>();
		ExecutorService pool = Executors.newFixedThreadPool(threads);
		List<Future<?>> futures = new ArrayList<>();
		long start = System.currentTimeMillis();

		for (int c = 0; c * chunkSize < total; c++) {
			final int ci = c;
			final List<Function> chunk = funcs.subList(c * chunkSize, Math.min(total, (c + 1) * chunkSize));
			futures.add(pool.submit(() -> {
				File out = new File(outDir, String.format("part_%04d.c", ci));
				try (Writer w = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(out), StandardCharsets.UTF_8))) {
					DecompInterface d = ifc.get();
					for (Function f : chunk) {
						if (monitor.isCancelled()) {
							return null;
						}
						String hdr = "\n// ==== " + f.getName(true) + " @ " + f.getEntryPoint() + " ====\n";
						try {
							DecompileResults r = d.decompileFunction(f, timeout, TaskMonitor.DUMMY);
							DecompiledFunction df = r != null ? r.getDecompiledFunction() : null;
							if (df != null) {
								w.write(hdr);
								w.write(df.getC());
								ok.incrementAndGet();
							}
							else {
								String msg = r != null ? r.getErrorMessage() : "null result";
								failures.add(f.getEntryPoint() + "\t" + f.getName(true) + "\t" + msg.replace('\n', ' '));
								w.write(hdr + "// decompile failed: " + msg.replace('\n', ' ') + "\n");
							}
						}
						catch (Exception e) {
							failures.add(f.getEntryPoint() + "\t" + f.getName(true) + "\t" + e);
						}
						int n = done.incrementAndGet();
						if (n % 5000 == 0) {
							long secs = (System.currentTimeMillis() - start) / 1000;
							println(String.format("decompiled %d / %d (%d s)", n, total, secs));
						}
					}
				}
				return null;
			}));
		}
		for (Future<?> f : futures) {
			f.get();
		}
		pool.shutdown();

		try (PrintWriter p = new PrintWriter(new File(outDir, "failures.txt"), "UTF-8")) {
			failures.forEach(p::println);
		}
		long secs = (System.currentTimeMillis() - start) / 1000;
		String summary = String.format("program=%s functions=%d decompiled=%d failed=%d seconds=%d",
			currentProgram.getName(), total, ok.get(), failures.size(), secs);
		try (PrintWriter p = new PrintWriter(new File(outDir, "summary.txt"), "UTF-8")) {
			p.println(summary);
		}
		println(summary);
	}
}
