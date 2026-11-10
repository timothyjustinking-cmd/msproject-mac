"use strict";
// round 3 helpers; load after chain.js:  .scriptload C:\trace\cmd\chain2.js
// hh(pos, pat): at a position search the 1 GB heap neighbourhood of the first setter call's table for pat; prints the hit addresses
function hh(p, pat) {
    var t = host.namespace.Debugger.Utility.Control.ExecuteCommand;
    var tb = null;
    var bm = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { bm = m.BaseAddress; } }
    var ea = bm.add(0x3dd790);
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) { c.TimeStart.SeekTo(); tb = host.currentThread.Registers.User.rcx; break; }
    var base = tb.subtract(0x20000000);
    host.namespace.Debugger.Utility.Control.ExecuteCommand("!tt " + p);
    var n = 0;
    for (var l of host.namespace.Debugger.Utility.Control.ExecuteCommand("s -b 0x" + base.toString(16) + " L?0x40000000 " + pat)) {
        var s = l.toString().trim().split(/\s+/)[0];
        if (/^[0-9a-f]{8}`?[0-9a-f]{8}$/i.test(s)) { host.diagnostics.debugLog("HIT " + p + " " + s.replace("`", "") + "\n"); n++; if (n >= 8) { break; } }
    }
    host.diagnostics.debugLog("hits " + p + " " + n + "\n");
}
// pend(p): at position p (entry of a call whose rcx is a table object) dump the pending list at table+0xa8 and what its qword pointers point at
function pend(p) {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("!tt " + p);
    var t = host.currentThread.Registers.User.rcx;
    var L = t.add(0xa8);
    var q = host.memory.readMemoryValues(L, 4, 8);
    var s = "";
    for (var i = 0; i < 4; i++) { s += q[i].toString(16) + " "; }
    host.diagnostics.debugLog("P tbl " + t.toString(16) + " list " + s + "\n");
    for (var i = 3; i < 4; i++) {
        try {
            var a = q[i];
            var b = host.memory.readMemoryValues(a, 60, 4);
            var h = "";
            for (var k = 0; k < 60; k++) { h += ("00000000" + b[k].toString(16)).slice(-8) + " "; }
            host.diagnostics.debugLog("P q" + i + " " + a.toString(16) + ": " + h + "\n");
        } catch (e) { host.diagnostics.debugLog("P q" + i + " unreadable\n"); }
    }
}
// ent(rva, fromSeq, toSeq, max): every call of an RVA: position, rcx, edx, r8d, r9d, 5th arg, return address
function ent(rvaHex, fromSeq, toSeq, max) {
    var bm = null;
    for (var m of host.currentProcess.Modules) { if (m.Name.toLowerCase().indexOf("winproj") >= 0) { bm = m.BaseAddress; } }
    var ea = bm.add(parseInt(rvaHex, 16));
    var fs0 = parseInt(fromSeq, 16), ts0 = parseInt(toSeq, 16);
    var n = 0, tot = 0;
    for (var c of host.currentSession.TTD.Memory(ea, ea.add(1), "e")) {
        var sq = parseInt(c.TimeStart.Sequence.toString(16), 16); if (sq < fs0 || sq > ts0) { continue; }
        tot++; if (n >= max) { continue; } n++;
        c.TimeStart.SeekTo();
        var rg = host.currentThread.Registers.User;
        var a5 = host.memory.readMemoryValues(rg.rsp.add(0x28), 1, 4)[0];
        var ra = host.memory.readMemoryValues(rg.rsp, 1, 8)[0];
        var u32 = function (v) { return (parseInt(v.toString(16).slice(-8), 16)).toString(16); };
        host.diagnostics.debugLog("E " + c.TimeStart.Sequence.toString(16) + ":" + c.TimeStart.Steps.toString(16) + " rcx " + rg.rcx.toString(16).slice(-6) + " edx " + u32(rg.rdx) + " r8 " + u32(rg.r8) + " r9 " + u32(rg.r9) + " rdxf " + rg.rdx.toString(16) + " a5 " + a5.toString(16) + " ra " + ra.subtract(bm).toString(16) + "\n");
    }
    host.diagnostics.debugLog("E total " + tot + "\n");
}
// wrsrc(p, destHex, len, maxn): at an ntdll memmove store position p (rdx = src - dst), the writers of the source of destHex..+len before p
function wrsrc(p, destHex, len, maxn) {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("!tt " + p);
    var rg = host.currentThread.Registers.User;
    var src = host.parseInt64(destHex, 16).add(rg.rdx);
    host.diagnostics.debugLog("S src " + src.toString(16) + "\n");
    wr(src.toString(16), len, "0:0", p, maxn);
}
// writers of an address range between two positions (all writes), compact; K = stack
function wr(addrHex, len, fromPos, toPos, maxn) {
    var A = host.parseInt64(addrHex, 16);
    var fp = fromPos.split(":"), tp = toPos.split(":");
    var fs = parseInt(fp[0], 16), ft = parseInt(fp[1], 16), ts = parseInt(tp[0], 16), tt = parseInt(tp[1], 16);
    var evs = [];
    for (var e of host.currentSession.TTD.Memory(A, A.add(len), "w")) {
        var es = parseInt(e.TimeStart.Sequence.toString(16), 16), et = parseInt(e.TimeStart.Steps.toString(16), 16);
        if (es < fs || (es == fs && et < ft) || es > ts || (es == ts && et > tt)) { continue; }
        evs.push(e);
    }
    host.diagnostics.debugLog("N writes " + evs.length + "\n");
    var k = 0;
    for (var e of evs) {
        if (k++ >= maxn) { break; }
        e.TimeStart.SeekTo();
        var w = "?";
        for (var m of host.currentProcess.Modules) {
            var b = m.BaseAddress;
            if (e.IP.compareTo(b) >= 0 && e.IP.compareTo(b.add(m.Size)) < 0) { var nm = m.Name; w = nm.substring(nm.lastIndexOf("\\") + 1) + "+0x" + e.IP.subtract(b).toString(16); }
        }
        host.diagnostics.debugLog("W " + e.TimeStart.Sequence.toString(16) + ":" + e.TimeStart.Steps.toString(16) + " off " + e.Address.subtract(A).toString(16) + " sz " + e.Size + " v " + e.Value.toString(16) + " ip " + w + "\n");
    }
}

