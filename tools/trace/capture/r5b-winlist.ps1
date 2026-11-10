# Inside the VM: list visible top-level windows of WINPROJ (pid, title, class, rect).  Exit text only.
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class WL {
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<string> All() {
    var res = new List<string>();
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      uint pid; GetWindowThreadProcessId(h, out pid);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      var c = new StringBuilder(128); GetClassName(h, c, 128);
      RECT r; GetWindowRect(h, out r);
      res.Add(pid + "|" + h.ToInt64().ToString("x") + "|" + c + "|" + t + "|" + r.L + "," + r.T + "," + r.R + "," + r.B);
      return true; }, IntPtr.Zero);
    return res;
  }
}
'@
$pids = @(Get-Process WINPROJ -ErrorAction SilentlyContinue | % Id)
[WL]::All() | ? { $p = [int](($_ -split '\|')[0]); $pids -contains $p } | % { $_ }
"pids: $($pids -join ',')"
