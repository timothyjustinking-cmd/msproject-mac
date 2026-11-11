"use strict";
// chain6.js (round 6): self-contained helpers.  mcp(pos): first execution of RVA 22f988 after pos.
// bufw(addrHex,len,before): every write to [addr, addr+len) before a position, with IP and Project-side stack.
function ctl() { return host.namespace.Debugger.Utility.Control; }
function run(cmd) { var r = []; for (var l of ctl().ExecuteCommand(cmd)) { r.push(l.toString()); } return r; }
function out(s) { host.diagnostics.debugLog(s + "\n"); }
function hx(v) { return v.toString(16); }
function num(v) { return parseInt(v.toString(16), 16); }
function wbase() { for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { return m.BaseAddress; } } return null; }
function where(ip) {
    for (var m of host.currentProcess.Modules) {
        var b = m.BaseAddress; var e = b.add(m.Size);
        if (ip.compareTo(b) >= 0 && ip.compareTo(e) < 0) {
            var n = m.Name; var i = n.lastIndexOf("\\"); if (i >= 0) { n = n.substring(i + 1); }
            return n + "+0x" + hx(ip.subtract(b));
        }
    }
    return "?" + hx(ip);
}
function pos(p) { return hx(p.Sequence) + ":" + hx(p.Steps); }
function stackW(n) {
    var k = run("k 40");
    var s = [];
    for (var l of k) {
        var m = l.match(/([0-9a-f]{8}`[0-9a-f]{8})\s+([0-9a-f]{8}`[0-9a-f]{8})/i);
        if (m) {
            var w = where(host.parseInt64(m[2].replace("`", ""), 16));
            if (w.indexOf("WINPROJ") == 0) { s.push(w.replace("WINPROJ.EXE+0x", "")); }
            if (s.length >= n) { break; }
        }
    }
    return s.join(",");
}
function after(e, bs, bt) { var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps); return es > bs || (es == bs && et >= bt); }
function mcp(rvaHex, fromPos, maxn) {
    var b = wbase(); var ea = b.add(parseInt(rvaHex, 16));
    var fp = fromPos.split(":"); var bs = parseInt(fp[0], 16), bt = parseInt(fp[1], 16);
    var evs = [];
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) { if (after(c, bs, bt)) { evs.push(c); } }
    evs.sort(function (x, y) { var a = num(x.TimeStart.Sequence), b2 = num(y.TimeStart.Sequence); if (a != b2) { return a - b2; } return num(x.TimeStart.Steps) - num(y.TimeStart.Steps); });
    var n = 0;
    for (var c of evs) {
        if (n++ >= maxn) { break; }
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        out("M " + pos(c.TimeStart) + " rcx " + hx(rg.rcx) + " rdx " + hx(rg.rdx) + " r8 " + hx(rg.r8) + " rsi " + hx(rg.rsi) + " rdi " + hx(rg.rdi));
        var u = run("u @rip L1"); for (var l of u) { if (l.trim().length) { out("I " + l.trim()); } }
    }
}
function bufw(addrHex, len, beforePos, maxn) {
    var a = host.parseInt64(addrHex, 16);
    var bp = beforePos.split(":"); var bs = parseInt(bp[0], 16), bt = parseInt(bp[1], 16);
    var evs = [];
    for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es > bs || (es == bs && et > bt)) { continue; }
        evs.push(e);
    }
    evs.sort(function (x, y) { var p = num(x.TimeStart.Sequence), q = num(y.TimeStart.Sequence); if (p != q) { return p - q; } return num(x.TimeStart.Steps) - num(y.TimeStart.Steps); });
    out("B writes " + evs.length);
    var from = Math.max(0, evs.length - (maxn || 40));
    for (var i = from; i < evs.length; i++) {
        var e = evs[i];
        e.TimeStart.SeekTo();
        out("W " + pos(e.TimeStart) + " off " + (num(e.Address) - num(a)) + " sz " + e.Size + " v " + hx(e.Value) + " ip " + where(e.IP).replace("WINPROJ.EXE+0x", "W+") + " K " + stackW(4));
    }
}
function sz(p) {
    run("!tt " + p);
    var rg = host.currentThread.Registers.User;
    var a = host.memory.readMemoryValues(rg.rsp.add(0x28), 4, 4);
    out("S " + p + " rcx " + hx(rg.rcx) + " rdx " + hx(rg.rdx) + " r8 " + hx(rg.r8) + " r9 " + hx(rg.r9) + " stk28 " + hx(a[0]) + " " + hx(a[1]) + " " + hx(a[2]) + " " + hx(a[3]) + " K " + stackW(6));
}
// ab(callPos): the first 22f988 execution after callPos; print its position and regs, the source bytes (r8 long) and the WINPROJ writes to them
function ab(callPos) {
    var b = wbase(); var ea = b.add(0x22f988);
    var fp = callPos.split(":"); var bs = parseInt(fp[0], 16), bt = parseInt(fp[1], 16);
    var best = null;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        if (!after(c, bs, bt)) { continue; }
        if (best == null) { best = c; continue; }
        var s1 = num(c.TimeStart.Sequence), t1 = num(c.TimeStart.Steps), s0 = num(best.TimeStart.Sequence), t0 = num(best.TimeStart.Steps);
        if (s1 < s0 || (s1 == s0 && t1 < t0)) { best = c; }
    }
    best.TimeStart.SeekTo();
    var rg = host.currentThread.Registers.User;
    var p = pos(best.TimeStart); var src = hx(rg.rdx); var len = num(rg.r8);
    out("M " + p + " src " + src + " len " + len);
    var v = host.memory.readMemoryValues(rg.rdx, len, 1);
    var s = []; for (var i = 0; i < len; i++) { var h = v[i].toString(16); s.push(h.length < 2 ? "0" + h : h); }
    out("Y " + s.join(" "));
    var a = host.parseInt64(src, 16);
    var bp = p.split(":"); var ps = parseInt(bp[0], 16), pt = parseInt(bp[1], 16);
    var evs = [];
    for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es > ps || (es == ps && et > pt)) { continue; }
        if (where(e.IP).indexOf("WINPROJ") != 0) { continue; }
        evs.push(e);
    }
    evs.sort(function (x, y) { var p1 = num(x.TimeStart.Sequence), q1 = num(y.TimeStart.Sequence); if (p1 != q1) { return p1 - q1; } return num(x.TimeStart.Steps) - num(y.TimeStart.Steps); });
    var from = Math.max(0, evs.length - 60);
    for (var i = from; i < evs.length; i++) {
        var e = evs[i];
        e.TimeStart.SeekTo();
        out("W " + pos(e.TimeStart) + " off " + (num(e.Address) - num(a)) + " sz " + e.Size + " v " + hx(e.Value) + " ip " + where(e.IP).replace("WINPROJ.EXE+0x", "W+") + " K " + stackW(3));
    }
}
function dis(startRva, n) {
    var l = run("u WINPROJ+0x" + startRva + " L" + n);
    for (var x of l) { if (x.trim().length) { out("D " + x.replace(/^\s+/, "").replace("00007ff6`", "")); } }
}
// findw(pat, len): at the end of the trace, search the 1 GB around the first table object for pat (bounded range), then list all writes to [hit, hit+len) with ip and stack
function findw(pat, len) {
    var b = wbase(); var ea = b.add(0x3dd790); var t = null;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) { c.TimeStart.SeekTo(); t = host.currentThread.Registers.User.rcx; break; }
    var base = t.subtract(0x20000000);
    run("!tt 100");
    var lines = run("s -b 0x" + hx(base) + " L?0x40000000 " + pat);
    var hs = [];
    for (var l of lines) { var tk = l.trim().split(/\s+/)[0]; if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(tk)) { hs.push(tk.replace("`", "")); } }
    out("F hits " + hs.length);
    var n = 0;
    for (var h of hs) {
        if (n++ >= 3) { break; }
        var a = host.parseInt64(h, 16);
        out("F hit " + h);
        var evs = [];
        for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) { evs.push(e); }
        evs.sort(function (x, y) { var p = num(x.TimeStart.Sequence), q = num(y.TimeStart.Sequence); if (p != q) { return p - q; } return num(x.TimeStart.Steps) - num(y.TimeStart.Steps); });
        out("F writes " + evs.length);
        var from = Math.max(0, evs.length - 30);
        for (var i = from; i < evs.length; i++) {
            var e = evs[i]; e.TimeStart.SeekTo();
            out("W " + pos(e.TimeStart) + " off " + (num(e.Address) - num(a)) + " sz " + e.Size + " v " + hx(e.Value) + " o " + hx(e.OverwrittenValue) + " ip " + where(e.IP).replace("WINPROJ.EXE+0x", "W+") + " K " + stackW(4));
        }
    }
}
// m44(rva, r8want): every execution of rva with r8 == r8want: position, rcx, rdx, and 44 bytes at rdx
function m44(rvaHex, r8want, maxn) {
    var b = wbase(); var ea = b.add(parseInt(rvaHex, 16)); var n = 0, tot = 0;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        tot++;
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        if (num(rg.r8) != r8want) { continue; }
        if (n++ >= maxn) { continue; }
        var v = host.memory.readMemoryValues(rg.rdx, r8want, 1);
        var s = []; for (var i = 0; i < r8want; i++) { var h = v[i].toString(16); s.push(h.length < 2 ? "0" + h : h); }
        out("M " + pos(c.TimeStart) + " rcx " + hx(rg.rcx) + " rdx " + hx(rg.rdx));
        out("Y " + s.join(" "));
    }
    out("M total " + tot + " matched " + n);
}
// vw(p, n): at a setter entry p, the n bytes r9 points at, and the WINPROJ writes to them before p, each with a stack of 8 Project frames
function vw(p, n) {
    run("!tt " + p);
    var rg = host.currentThread.Registers.User;
    var a = rg.r9;
    var v = host.memory.readMemoryValues(a, n, 1);
    var s = []; for (var i = 0; i < n; i++) { var h = v[i].toString(16); s.push(h.length < 2 ? "0" + h : h); }
    out("V " + p + " r9 " + hx(a) + " bytes " + s.join(" ") + " K " + stackW(10));
    bufw(hx(a), n, p, 600);
}
// fc6(max): like fc("3dd790") but a failed read skips the call instead of aborting
function fc6(max) {
    var b = wbase(); var ea = b.add(0x3dd790); var n = 0, tot = 0;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        tot++;
        if (n >= max) { continue; }
        n++;
        try {
            c.TimeStart.SeekTo();
            var rg = host.currentThread.Registers.User;
            var table = rg.rcx; var row = num(rg.rdx) & 0xffffffff; var fld = num(rg.r8) & 0xffffffff; var pv = rg.r9;
            var descBase = host.memory.readMemoryValues(table.add(0x3e0), 1, 8)[0];
            var desc = descBase.add(fld * 0x1c);
            var d = host.memory.readMemoryValues(desc, 7, 4);
            var dd = []; for (var i = 0; i < 7; i++) { dd.push(hx(d[i])); }
            var v = ""; try { v = hx(host.memory.readMemoryValues(pv, 1, 4)[0]); } catch (e) { v = "?"; }
            out("C " + pos(c.TimeStart) + " tbl " + hx(table).slice(-6) + " row " + row + " fld " + fld + " d " + dd.join(".") + " v " + v);
        } catch (e) { out("C " + pos(c.TimeStart) + " readfail"); }
    }
    out("C total " + tot);
}
function bytesAt(addrHex, len, atPos) {
    run("!tt " + atPos);
    var v = host.memory.readMemoryValues(host.parseInt64(addrHex, 16), len, 1);
    var s = []; for (var i = 0; i < len; i++) { var h = v[i].toString(16); s.push(h.length < 2 ? "0" + h : h); }
    out("Y " + addrHex + " " + s.join(" "));
}
