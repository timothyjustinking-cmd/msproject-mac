// round 4 helpers; load after chain.js and chain2.js: .scriptload chain3.js
// vchain(p, n): at a setter entry position p, the last write to the value pointer r9 points at; if it is a memcpy, follow hops
function vchain(p, n) {
    run("!tt " + p);
    var rg = host.currentThread.Registers.User; var pv = rg.r9;
    var val = host.memory.readMemoryValues(pv, 1, 4)[0];
    var pp = p.split(":"); var bs = parseInt(pp[0], 16), bt = parseInt(pp[1], 16);
    var best = null;
    for (var e of host.currentSession.TTD.Memory(pv, pv.add(4), "w")) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es > bs || (es == bs && et > bt)) { continue; }
        if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
    }
    if (best == null) { out("V " + p + " v " + hx(val) + " nowrite"); return; }
    best.e.TimeStart.SeekTo();
    var w = where(best.e.IP).replace("WINPROJ.EXE+0x", "W+");
    var ism = (w.indexOf("VCRUNTIME") >= 0 || w.indexOf("ucrtbase") >= 0 || w.indexOf("memcpy") >= 0 || w.indexOf("msvcr") >= 0);
    var ins = "";
    for (var l of run("u @rip L1")) { if (l.trim().length && l.indexOf("`") >= 0) { ins = l.trim().replace(/\s+/g, " ").replace(/^[0-9a-f`]+ [0-9a-f]+ /, ""); } }
    out("V " + p + " v " + hx(val) + " w " + pos(best.e.TimeStart) + " " + (ism ? "memcpy" : "STORE " + w + " " + ins) + " K " + stackOf(6));
    if (ism) { hops(pos(best.e.TimeStart), n); }
}
// steps(p, n): from position p, single-step n instructions (t) and print the WINPROJ RVA of each rip (compressed: only when not rip+len)
function steps(p, n) {
    run("!tt " + p);
    var bm = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { bm = m.BaseAddress; } }
    var s = [];
    for (var i = 0; i < n; i++) {
        var rg = host.currentThread.Registers.User;
        var rip = rg.rip;
        var rv = rip.subtract(bm);
        s.push(rv.toString(16));
        run("t");
    }
    out("S " + p + " " + s.join(" "));
}
// uc(): every call of rpcrt4!UuidCreate*: start position, function, WINPROJ return RVA, the 16 bytes written to [rcx] at the end of the call
function uc() {
    var bm = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { bm = m.BaseAddress; } }
    for (var c of host.currentSession.TTD.Calls("rpcrt4!UuidCreate*")) {
        var ts = c.TimeStart; var te = c.TimeEnd;
        ts.SeekTo();
        var rg = host.currentThread.Registers.User; var op = rg.rcx; var ra = host.memory.readMemoryValues(rg.rsp, 1, 8)[0];
        te.SeekTo();
        var b = host.memory.readMemoryValues(op, 16, 1); var s = [];
        for (var i = 0; i < 16; i++) { s.push(("0" + b[i].toString(16)).slice(-2)); }
        var rv = ra.subtract(bm);
        out("G " + pos(ts) + " " + c.FunctionName + " ra " + rv.toString(16) + " guid " + s.join(""));
    }
}
// tcall(pat, fromSeqHex, toSeqHex): calls matching pat (module!func wildcard) in a sequence window: position, WINPROJ-relative return address (if in WINPROJ)
function tcall(pat, fs, ts) {
    var f = parseInt(fs, 16), t = parseInt(ts, 16);
    var bm = null, bsz = 0;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { bm = m.BaseAddress; bsz = m.Size; } }
    var n = 0;
    for (var c of host.currentSession.TTD.Calls(pat)) {
        var s = num(c.TimeStart.Sequence);
        if (s < f || s > t) { continue; }
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User; var ra = host.memory.readMemoryValues(rg.rsp, 1, 8)[0];
        var rv = ra.subtract(bm);
        out("C " + pos(c.TimeStart) + " ra " + rv.toString(16) + " rcx " + hx(rg.rcx) + " rax_end");
        if (++n > 40) { break; }
    }
    out("C total " + n);
}
// rd(addrHex, len, fromPos, maxn, mode): memory events (mode "r" or "w" or "rw") on [addr, addr+len) from a position: one E line each with ip module+offset, size, and a 4-deep stack
function rd(addrHex, len, fromPos, maxn, mode) {
    var A = host.parseInt64(addrHex, 16);
    var fp = fromPos.split(":"); var fs = parseInt(fp[0], 16), ft = parseInt(fp[1], 16);
    var n = 0, tot = 0;
    for (var e of host.currentSession.TTD.Memory(A, A.add(len), mode)) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es < fs || (es == fs && et < ft)) { continue; }
        tot++;
        if (n >= maxn) { continue; }
        n++;
        e.TimeStart.SeekTo();
        out("E " + pos(e.TimeStart) + " " + e.Type + " off " + hx(num(e.Address) - num(A)) + " sz " + e.Size + " ip " + where(e.IP).replace("WINPROJ.EXE+0x", "W+") + " K " + stackOf(5));
    }
    out("E total " + tot);
}
