"use strict";
// Helpers for the trace analysis (cdb/WinDbg, TTD data model).  Usage inside cdb:
//   .scriptload C:\trace\cmd\chain.js
//   dx @$scriptContents.hits("f2 3b d8 27")        search user memory at the current position
//   dx @$scriptContents.lastw("0x7ff7e5a01234", 4, 20)   writes to [addr, addr+4), the last N
//   dx @$scriptContents.at("1A2B:3C")              go to a position and show rip, the instruction and the stack
var LIM = 3;
function setlim(n) { LIM = n; }
function ctl() { return host.namespace.Debugger.Utility.Control; }
function run(cmd) { var r = []; for (var l of ctl().ExecuteCommand(cmd)) { r.push(l.toString()); } return r; }
function out(s) { host.diagnostics.debugLog(s + "\n"); }
function hx(v) { return v.toString(16); }
function num(v) { return parseInt(v.toString(16), 16); }
function mods() { var a = []; for (var m of host.currentProcess.Modules) { a.push(m); } return a; }
function where(ip) {
    for (var m of mods()) {
        var b = m.BaseAddress; var e = b.add(m.Size);
        if (ip.compareTo(b) >= 0 && ip.compareTo(e) < 0) {
            var n = m.Name; var i = n.lastIndexOf("\\"); if (i >= 0) { n = n.substring(i + 1); }
            return n + "+0x" + hx(ip.subtract(b));
        }
    }
    return "?" + hx(ip);
}
function hits(pat) {
    var lines = run("s -b 0 L?0x7ffffffeffff " + pat);
    var n = 0;
    for (var l of lines) {
        var t = l.trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(t)) { out("HIT " + t.replace("`", "")); n++; if (n >= 12) { break; } }
    }
    out("hits " + n);
}
function views() {
    var i = 0;
    for (var c of host.currentSession.TTD.Calls("ntdll!NtMapViewOfSection")) {
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        var pb = rg.r8;
        var vs = host.memory.readMemoryValues(rg.rsp.add(0x38), 1, 8)[0];
        c.TimeEnd.SeekTo();
        var base = host.memory.readMemoryValues(pb, 1, 8)[0];
        var size = host.memory.readMemoryValues(vs, 1, 8)[0];
        out("VIEW " + i + " " + pos(c.TimeStart) + " base " + hx(base) + " size " + hx(size));
        i++;
    }
}
function maps(minSz, maxSz) {
    // lists MEM_MAPPED regions whose size is between minSz and maxSz (hex strings)
    var lo = parseInt(minSz, 16), hi = parseInt(maxSz, 16);
    for (var l of run("!address -f:MEM_MAPPED")) {
        var m = l.match(/([0-9a-f]{8}`?[0-9a-f]{8})\s+([0-9a-f]{8}`?[0-9a-f]{8})\s+([0-9a-f]{8}`?[0-9a-f]{8})/i);
        if (m) {
            var sz = parseInt(m[3].replace("`", ""), 16);
            if (sz >= lo && sz <= hi) { out("MAP " + m[1] + " " + m[2] + " " + m[3]); }
        }
    }
}
function hitsIn(base, len, pat) {
    // search [base, base+len) at the current position
    var lines = run("s -b " + base + " L?" + len + " " + pat);
    var n = 0;
    for (var l of lines) {
        var t = l.trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(t)) { out("HIT " + t.replace("`", "")); n++; if (n >= 12) { break; } }
    }
    out("hits " + n);
}
function pos(p) { return hx(p.Sequence) + ":" + hx(p.Steps); }
function lastw(addrStr, len, count, before) {
    var a = host.parseInt64(addrStr, 16);
    var evs = [];
    var bs = -1, bt = 0;
    if (before) { var bp = before.split(":"); bs = parseInt(bp[0], 16); bt = parseInt(bp[1], 16); }
    for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) {
        if (bs >= 0) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (es > bs || (es == bs && et > bt)) { continue; }
        }
        evs.push(e);
    }
    evs.sort(function (x, y) {
        var sx = num(x.TimeStart.Sequence), sy = num(y.TimeStart.Sequence);
        if (sx != sy) { return sx - sy; }
        return num(x.TimeStart.Steps) - num(y.TimeStart.Steps);
    });
    out("writes " + evs.length);
    var from = Math.max(0, evs.length - (count || 5));
    for (var i = from; i < evs.length; i++) {
        var e = evs[i];
        out("W " + pos(e.TimeStart) + " tid " + hx(e.ThreadId) + " ip " + where(e.IP) + " addr " + hx(e.Address) + " sz " + e.Size + " val " + hx(e.Value) + " old " + hx(e.OverwrittenValue));
    }
}
function stackOf(n) {
    var k = run("k " + n);
    var s = [];
    for (var l of k) {
        var m = l.match(/([0-9a-f]{8}`[0-9a-f]{8})\s+([0-9a-f]{8}`[0-9a-f]{8})/i);
        if (m) {
            var ip = host.parseInt64(m[2].replace("`", ""), 16);
            s.push(where(ip).replace("WINPROJ.EXE+0x", "").replace("ucrtbase.dll+", "uc+").replace("VCRUNTIME140.dll+", "vc+"));
        }
    }
    return s.join(",");
}
// one line per stream offset: the last write to base+off (len 4 by default) before a position, and its stack
function row(baseStr, offsCsv, len, before) {
    var base = host.parseInt64(baseStr, 16);
    var bp = before.split(":"); var bs = parseInt(bp[0], 16), bt = parseInt(bp[1], 16);
    for (var o of offsCsv.split(",")) {
        var off = parseInt(o, 10);
        var a = base.add(off);
        var best = null; var cnt = 0;
        for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (es > bs || (es == bs && et > bt)) { continue; }
            cnt++;
            if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
        }
        if (best == null) { out("+" + off + " none (" + cnt + ")"); continue; }
        var e = best.e;
        e.TimeStart.SeekTo();
        out("+" + off + " " + pos(e.TimeStart) + " ip " + where(e.IP) + " sz " + e.Size + " v " + hx(e.Value) + " o " + hx(e.OverwrittenValue) + " n " + cnt + " K " + stackOf(5));
    }
}
// search for pat (hex bytes) in [lo, lo+len) at the end of the trace; for each hit print the last write to hit+rel (len bytes) before 'before'
function auto(label, lo, rangeLen, pat, rel, len, before) {
    run("!tt " + before);
    var lines = run("s -b " + lo + " L?" + rangeLen + " " + pat);
    var hs = [];
    for (var l of lines) {
        var t = l.trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(t)) { hs.push(t.replace("`", "")); }
    }
    out("H " + label + " hits " + hs.length);
    var n = 0;
    for (var h of hs) {
        if (n++ >= LIM) { break; }
        var a = host.parseInt64(h, 16).add(rel);
        var bp = before.split(":"); var bs = parseInt(bp[0], 16), bt = parseInt(bp[1], 16);
        var best = null, cnt = 0;
        for (var e of host.currentSession.TTD.Memory(a, a.add(len), "w")) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (es > bs || (es == bs && et > bt)) { continue; }
            cnt++;
            if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
        }
        if (best == null) { out("R " + label + " " + h.slice(-6) + " nowrite"); continue; }
        best.e.TimeStart.SeekTo();
        out("R " + label + " " + h.slice(-6) + " " + pos(best.e.TimeStart) + " " + where(best.e.IP).replace("WINPROJ.EXE+0x", "W+") + " sz" + best.e.Size + " v" + hx(best.e.Value) + " o" + hx(best.e.OverwrittenValue) + " n" + cnt + " K " + stackOf(5));
    }
}
// calls of the generic field setter FUN_1403dd790 (rva 3dd790): args table,row,field,pValue,size; descriptor dwords; value
function fcalls(addrHex, fromPos, toPos, max) {
    var fp = fromPos.split(":"), tp = toPos.split(":");
    var fs = parseInt(fp[0], 16), ts = parseInt(tp[0], 16);
    var n = 0, tot = 0;
    var ea = host.parseInt64(addrHex, 16);
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        var s = num(c.TimeStart.Sequence);
        if (s < fs || s > ts) { continue; }
        tot++;
        if (n >= max) { continue; }
        n++;
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        var table = rg.rcx; var row = num(rg.rdx) & 0xffffffff; var fld = num(rg.r8) & 0xffffffff; var pv = rg.r9;
        var sz = host.memory.readMemoryValues(rg.rsp.add(0x28), 1, 4)[0];
        var descBase = host.memory.readMemoryValues(table.add(0x3e0), 1, 8)[0];
        var desc = descBase.add(fld * 0x1c);
        var d = host.memory.readMemoryValues(desc, 7, 4);
        var dd = [];
        for (var i = 0; i < 7; i++) { dd.push(hx(d[i])); }
        var v = "";
        try { v = hx(host.memory.readMemoryValues(pv, 1, 4)[0]); } catch (e) { v = "?"; }
        out("C " + pos(c.TimeStart) + " tbl " + hx(table).slice(-6) + " row " + row + " fld " + fld + " sz " + sz + " d " + dd.join(".") + " v " + v);
    }
    out("C total " + tot);
}
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
// the Project-side caller chain of the stream write that put pat into view 2 (the docfile stream cache)
function streamw(label, vb, vl, pat) {
    run("!tt d1f2:4c");
    var lines = run("s -b " + vb + " L?" + vl + " " + pat);
    var hs = [];
    for (var l of lines) {
        var t = l.trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(t)) { hs.push(t.replace("`", "")); }
    }
    out("S " + label + " hits " + hs.length);
    var n = 0;
    for (var h of hs) {
        if (n++ >= 2) { break; }
        var a = host.parseInt64(h, 16);
        var best = null;
        for (var e of host.currentSession.TTD.Memory(a, a.add(1), "w")) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
        }
        if (best == null) { out("S " + label + " " + h.slice(-5) + " nowrite"); continue; }
        best.e.TimeStart.SeekTo();
        out("S " + label + " " + h.slice(-5) + " " + pos(best.e.TimeStart) + " " + where(best.e.IP) + " W " + stackW(8));
    }
}
// same as fcalls but the entry is given as an RVA and the whole trace is covered
function fc(rvaHex, max) {
    var b = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { b = m.BaseAddress; } }
    var ea = b.add(parseInt(rvaHex, 16));
    fcalls(hx(ea), "0:0", "ffffff:0", max);
}
function fcr(rvaHex, fromPos, toPos, max) {
    var b = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { b = m.BaseAddress; } }
    fcalls(hx(b.add(parseInt(rvaHex, 16))), fromPos, toPos, max);
}
// value source: at a setter entry position p, the last write to the 4 bytes r9 points at, its instruction and stack
function vsrc(p) {
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
    if (best == null) { out("P " + p + " v " + hx(val) + " nowrite"); return; }
    best.e.TimeStart.SeekTo();
    var ins = "";
    for (var l of run("u @rip L1")) { if (l.trim().length && l.indexOf("`") >= 0) { ins = l.trim().replace(/\s+/g, " "); } }
    out("P " + p + " v " + hx(val) + " w " + pos(best.e.TimeStart) + " sz " + best.e.Size + " ip " + where(best.e.IP).replace("WINPROJ.EXE+0x", "W+") + " I " + ins + " K " + stackOf(6));
}
// second hop: at a memcpy store position p (inside VCRUNTIME memcpy), rdx is the source; the last write to it before p
function srcw(p) {
    run("!tt " + p);
    var rg = host.currentThread.Registers.User; var sp = rg.rdx; var dp = rg.rax;
    var pp = p.split(":"); var bs = parseInt(pp[0], 16), bt = parseInt(pp[1], 16);
    var best = null; var n = 0;
    for (var e of host.currentSession.TTD.Memory(sp, sp.add(4), "w")) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es > bs || (es == bs && et > bt)) { continue; }
        n++;
        if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
    }
    if (best == null) { out("T " + p + " src " + hx(sp) + " nowrite"); return; }
    best.e.TimeStart.SeekTo();
    var ins = "";
    for (var l of run("u @rip L1")) { if (l.trim().length && l.indexOf("`") >= 0) { ins = l.trim().replace(/\s+/g, " "); } }
    out("T " + p + " src " + hx(sp).slice(-8) + " v " + hx(best.e.Value) + " w " + pos(best.e.TimeStart) + " sz " + best.e.Size + " n " + n + " ip " + where(best.e.IP).replace("WINPROJ.EXE+0x", "W+") + " I " + ins + " K " + stackOf(7));
}
// follow a value back through memcpy hops until a non-memcpy store; one T line per hop
function hops(p, n) {
    var cur = p;
    for (var h = 0; h < n; h++) {
        run("!tt " + cur);
        var rg = host.currentThread.Registers.User; var sp = rg.rdx;
        var pp = cur.split(":"); var bs = parseInt(pp[0], 16), bt = parseInt(pp[1], 16);
        var best = null;
        for (var e of host.currentSession.TTD.Memory(sp, sp.add(4), "w")) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (es > bs || (es == bs && et > bt)) { continue; }
            if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
        }
        if (best == null) { out("T " + p + " hop" + h + " src " + hx(sp).slice(-8) + " nowrite"); return; }
        best.e.TimeStart.SeekTo();
        var w = where(best.e.IP).replace("WINPROJ.EXE+0x", "W+");
        var ins = "";
        for (var l of run("u @rip L1")) { if (l.trim().length && l.indexOf("`") >= 0) { ins = l.trim().replace(/\s+/g, " ").replace(/^[0-9a-f`]+ [0-9a-f]+ /, ""); } }
        var ism = (w.indexOf("VCRUNTIME") >= 0 || w.indexOf("ucrtbase") >= 0 || w.indexOf("memcpy") >= 0 || w.indexOf("msvcr") >= 0);
        out("T " + p + " hop" + h + " v " + hx(best.e.Value) + " w " + pos(best.e.TimeStart) + " " + (ism ? "memcpy " : "STORE " + w + " " + ins + " ") + "K " + stackOf(6));
        if (!ism) { return; }
        cur = pos(best.e.TimeStart);
    }
}
// find the source address of a copying write event e: a register (+-0x80) that points at memory equal to what was written
function srcfind(e) {
    var n = Math.min(e.Size, 16);
    e.TimeEnd.SeekTo();
    var P = [];
    var got = host.memory.readMemoryValues(e.Address, n, 1);
    for (var i = 0; i < n; i++) { P.push(got[i]); }
    e.TimeStart.SeekTo();
    var rg = host.currentThread.Registers.User;
    var names = ["rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rsp", "rbp"];
    for (var nm of names) {
        var base = rg[nm];
        if (num(base) < 0x10000) { continue; }
        for (var k = -0x80; k <= 0x80; k++) {
            var a = base.add(k);
            if (a.compareTo(e.Address) == 0) { continue; }
            var ok = true;
            try {
                var v = host.memory.readMemoryValues(a, n, 1);
                for (var i = 0; i < n; i++) { if (v[i] != P[i]) { ok = false; break; } }
            } catch (x) { ok = false; }
            if (ok) { return a; }
        }
    }
    return null;
}
// follow the byte(s) at an address back through copies to the first non-copy store (or no write at all)
function trace4(addrHex, beforeStr, depth) {
    var A = host.parseInt64(addrHex, 16);
    var bp = beforeStr.split(":"); var bs = parseInt(bp[0], 16), bt = parseInt(bp[1], 16);
    for (var d = 0; d < depth; d++) {
        var best = null;
        for (var e of host.currentSession.TTD.Memory(A, A.add(1), "w")) {
            var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
            if (es > bs || (es == bs && et > bt)) { continue; }
            if (best == null || es > best.s || (es == best.s && et > best.t)) { best = { s: es, t: et, e: e }; }
        }
        if (best == null) { out("Q " + d + " addr " + hx(A).slice(-8) + " NOWRITE before " + bs.toString(16) + ":" + bt.toString(16)); return; }
        var e = best.e;
        e.TimeStart.SeekTo();
        var w = where(e.IP).replace("WINPROJ.EXE+0x", "W+");
        var ins = "";
        for (var l of run("u @rip L1")) { if (l.trim().length && l.indexOf("`") >= 0) { ins = l.trim().replace(/\s+/g, " ").replace(/^[0-9a-f`]+ [0-9a-f]+ /, ""); } }
        var ism = (w.indexOf("VCRUNTIME") >= 0 || w.indexOf("ucrtbase") >= 0 || w.indexOf("memcpy") >= 0 || w.indexOf("memmove") >= 0 || w.indexOf("ntdll") >= 0);
        var st = stackOf(7);
        if (!ism) { out("Q " + d + " STORE pos " + pos(e.TimeStart) + " sz " + e.Size + " " + w + " " + ins + " v " + hx(e.Value) + " K " + st); return; }
        var src = srcfind(e);
        out("Q " + d + " copy pos " + pos(e.TimeStart) + " sz " + e.Size + " " + w + " v " + hx(e.Value) + " K " + st + " src " + (src ? hx(src).slice(-8) : "?"));
        if (!src) { return; }
        A = src.add(A.subtract(e.Address));
        bs = num(e.TimeStart.Sequence); bt = num(e.TimeStart.Steps);
    }
}
// search [base, base+len) at the end of the trace for pat; for each hit trace the bytes at hit+rel (comma list) back through copies
function findtrace(base, len, pat, relCsv, depth, maxhits) {
    run("!tt 100");
    var lines = run("s -b " + base + " L?" + len + " " + pat);
    var hs = [];
    for (var l of lines) {
        var t = l.trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(t)) { hs.push(t.replace("`", "")); }
    }
    out("N hits " + hs.length);
    var n = 0;
    for (var h of hs) {
        if (n++ >= maxhits) { break; }
        for (var r of relCsv.split(",")) {
            out("N hit " + h.slice(-8) + " rel " + r);
            var a = host.parseInt64(h, 16).add(parseInt(r, 10));
            run("!tt 100");
            trace4(hx(a), "ffffffff:0", depth);
        }
    }
}
// every call of an RVA: position and rcx, rdx, r8, r9 (full), for finding stream write buffers
function argcalls(rvaHex, max, fromSeq) {
    var b = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { b = m.BaseAddress; } }
    var ea = b.add(parseInt(rvaHex, 16));
    var fs0 = parseInt(fromSeq || "0", 16);
    var n = 0, tot = 0;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        if (num(c.TimeStart.Sequence) < fs0) { continue; }
        tot++;
        if (n >= max) { continue; }
        n++;
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        out("U " + pos(c.TimeStart) + " rcx " + hx(rg.rcx) + " rdx " + hx(rg.rdx) + " r8 " + hx(rg.r8) + " r9 " + hx(rg.r9));
    }
    out("U total " + tot);
}
// positions of NtUnmapViewOfSection calls with the base address (rdx)
function unmaps() {
    var i = 0;
    for (var c of host.currentSession.TTD.Calls("ntdll!NtUnmapViewOfSection")) {
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        out("U unmap " + i + " " + pos(c.TimeStart) + " base " + hx(rg.rdx));
        i++;
    }
}
// NtWriteFile calls: position, buffer pointer, length
function wfiles() {
    var n = 0;
    for (var c of host.currentSession.TTD.Calls("ntdll!NtWriteFile")) {
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        var buf = host.memory.readMemoryValues(rg.rsp.add(0x28), 1, 8)[0];
        var len = host.memory.readMemoryValues(rg.rsp.add(0x30), 1, 4)[0];
        out("U wf " + pos(c.TimeStart) + " buf " + hx(buf) + " len " + hx(len));
        n++;
    }
    out("U wf total " + n);
    var m = 0;
    for (var c of host.currentSession.TTD.Calls("ntdll!NtCreateSection")) { m++; }
    out("U NtCreateSection total " + m);
}
// like findtrace but the range is the 512 MB around the first setter call's table object (the process heap neighbourhood)
function findheap(pat, relCsv, depth, maxhits) {
    var t = tbl0("0");
    var base = host.parseInt64(t, 16).subtract(0x20000000);
    out("N heapbase " + hx(base));
    findtrace("0x" + hx(base), "0x40000000", pat, relCsv, depth, maxhits);
}
function at(p) {
    run("!tt " + p);
    var r = run("u @rip L1");
    for (var l of r) { if (l.trim().length) { out("I " + l.trim()); } }
    var reg = run("r rax,rbx,rcx,rdx,rsi,rdi,r8,r9");
    for (var l of reg) { if (l.trim().length) { out("R " + l.trim()); } }
    var k = run("k 10");
    var n = 0;
    for (var l of k) {
        var m = l.match(/([0-9a-f]{8}`[0-9a-f]{8})\s+([0-9a-f]{8}`[0-9a-f]{8})/i);
        if (m) {
            var ip = host.parseInt64(m[2].replace("`", ""), 16);
            out("K " + where(ip)); n++;
        }
    }
}
// writes to the table's row staging buffer (table+0x240, 202 bytes) between two positions; one W line each
function wbuf(tblHex, atPos, fromPos, toPos, maxn, sz) {
    run("!tt " + atPos);
    var table = host.parseInt64(tblHex, 16);
    var buf = host.memory.readMemoryValues(table.add(0x240), 1, 8)[0];
    out("N buf " + hx(buf));
    var fp = fromPos.split(":"), tp = toPos.split(":");
    var fs = parseInt(fp[0], 16), ft = parseInt(fp[1], 16), ts = parseInt(tp[0], 16), tt = parseInt(tp[1], 16);
    var evs = [];
    for (var e of host.currentSession.TTD.Memory(buf, buf.add(sz || 202), "w")) {
        var es = num(e.TimeStart.Sequence), et = num(e.TimeStart.Steps);
        if (es < fs || (es == fs && et < ft) || es > ts || (es == ts && et > tt)) { continue; }
        evs.push(e);
    }
    evs.sort(function (x, y) { var a = num(x.TimeStart.Sequence), b = num(y.TimeStart.Sequence); if (a != b) { return a - b; } return num(x.TimeStart.Steps) - num(y.TimeStart.Steps); });
    out("N writes " + evs.length);
    var k = 0;
    for (var e of evs) {
        if (k++ >= maxn) { break; }
        out("W " + pos(e.TimeStart) + " off " + hx(e.Address.subtract(buf)) + " sz " + e.Size + " v " + hx(e.Value) + " ip " + where(e.IP).replace("WINPROJ.EXE+0x", "W+"));
    }
}
// calls of any function by RVA with rcx, edx, r8d (row loader and the like)
function calls3(rvaHex, max, fromSeq, toSeq) {
    var fs0 = parseInt(fromSeq, 16), ts0 = parseInt(toSeq, 16);
    var b = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { b = m.BaseAddress; } }
    var ea = b.add(parseInt(rvaHex, 16));
    var n = 0, tot = 0;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        var sq = num(c.TimeStart.Sequence); if (sq < fs0 || sq > ts0) { continue; }
        tot++;
        if (n >= max) { continue; }
        n++;
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        out("U " + pos(c.TimeStart) + " rcx " + hx(rg.rcx).slice(-6) + " edx " + (num(rg.rdx) & 0xffffffff) + " r8 " + hx(num(rg.r8) & 0xffffffff));
    }
    out("U total " + tot);
}
// the table object (rcx) at the first entry of the setter FUN_1403dd790 after sequence fromSeq
function tbl0(fromSeq) {
    var b = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { b = m.BaseAddress; } }
    var ea = b.add(0x3dd790);
    var fs0 = parseInt(fromSeq, 16);
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        if (num(c.TimeStart.Sequence) < fs0) { continue; }
        c.TimeStart.SeekTo();
        var t = host.currentThread.Registers.User.rcx;
        out("N tbl " + hx(t) + " at " + pos(c.TimeStart));
        return "0x" + hx(t);
    }
}
