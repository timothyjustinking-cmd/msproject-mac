# Dumps every function of every interface in a type library with its vtable slot.
# Output TSV: typeinfo, typekind, guid, slot, memid, invkind, name, params
param([string]$Path, [string]$Out)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.IO;

public static class TlbDump {
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void LoadTypeLibEx(string file, int regKind, out ITypeLib tlb);

    public static int Dump(string path, string outPath) {
        ITypeLib tlb; LoadTypeLibEx(path, 2 /*REGKIND_NONE*/, out tlb);
        int rows = 0;
        using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) {
            w.WriteLine("typeinfo\ttypekind\tguid\tslot\tmemid\tinvkind\tname\tparams");
            int n = tlb.GetTypeInfoCount();
            for (int i = 0; i < n; i++) {
                ITypeInfo ti; tlb.GetTypeInfo(i, out ti);
                string tname, doc, hf; int hc;
                tlb.GetDocumentation(i, out tname, out doc, out hc, out hf);
                IntPtr pAttr; ti.GetTypeAttr(out pAttr);
                var attr = (TYPEATTR)Marshal.PtrToStructure(pAttr, typeof(TYPEATTR));
                ti.ReleaseTypeAttr(pAttr);
                // for dual dispinterfaces, walk the vtable-side interface
                if (attr.typekind == TYPEKIND.TKIND_DISPATCH && (attr.wTypeFlags & TYPEFLAGS.TYPEFLAG_FDUAL) != 0) {
                    try { int href; ti.GetRefTypeOfImplType(-1, out href); ITypeInfo t2; ti.GetRefTypeInfo(href, out t2); ti = t2;
                        ti.GetTypeAttr(out pAttr); attr = (TYPEATTR)Marshal.PtrToStructure(pAttr, typeof(TYPEATTR)); ti.ReleaseTypeAttr(pAttr);
                    } catch { }
                }
                if (attr.typekind == TYPEKIND.TKIND_ENUM) {
                    for (int v = 0; v < attr.cVars; v++) {
                        IntPtr pv; ti.GetVarDesc(v, out pv);
                        var vd = (VARDESC)Marshal.PtrToStructure(pv, typeof(VARDESC));
                        string[] nm = new string[1]; int cn; ti.GetNames(vd.memid, nm, 1, out cn);
                        object val = Marshal.GetObjectForNativeVariant(vd.desc.lpvarValue);
                        w.WriteLine(tname + "\tENUM\t" + attr.guid + "\t\t" + val + "\t\t" + nm[0] + "\t");
                        ti.ReleaseVarDesc(pv); rows++;
                    }
                    continue;
                }
                for (int f = 0; f < attr.cFuncs; f++) {
                    IntPtr pf; ti.GetFuncDesc(f, out pf);
                    var fd = (FUNCDESC)Marshal.PtrToStructure(pf, typeof(FUNCDESC));
                    string[] names = new string[fd.cParams + 1]; int cnt;
                    ti.GetNames(fd.memid, names, names.Length, out cnt);
                    var ps = new StringBuilder();
                    for (int p = 1; p < cnt; p++) { if (p > 1) ps.Append(","); ps.Append(names[p]); }
                    w.WriteLine(tname + "\t" + attr.typekind + "\t" + attr.guid + "\t" + (fd.oVft / IntPtr.Size) + "\t" +
                        fd.memid + "\t" + fd.invkind + "\t" + names[0] + "\t" + ps);
                    ti.ReleaseFuncDesc(pf); rows++;
                }
            }
        }
        return rows;
    }
}
"@

"rows: " + [TlbDump]::Dump($Path, $Out)
