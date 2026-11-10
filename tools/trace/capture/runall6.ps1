param([string]$List, [int]$TimeoutSec = 600)
# Runs every target in the list file (one per line) in sequence; logs to C:\trace\out\runall6.log.  Inside the VM, elevated.
# Round 5b: each target runs in its own child powershell with a timeout; a hung capture is killed, logged HUNG, and the run goes on.
$log = 'C:\trace\out\runall6.log'
foreach ($t in (Get-Content $List | ? { $_.Trim() })) {
    $free = [math]::Round((Get-PSDrive $(if (Test-Path 'E:\runs') { 'E' } else { 'C' })).Free / 1GB, 1)
    "$(Get-Date -Format s) start $t free GB $free" | Out-File $log -Append -Encoding ascii
    if ($free -lt 15) { "$(Get-Date -Format s) STOP low disk" | Out-File $log -Append -Encoding ascii; break }
    $p = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File C:\trace\cmd\runone6.ps1 $t" -PassThru -WindowStyle Minimized -RedirectStandardOutput "C:\trace\out\$t.run.out" -RedirectStandardError "C:\trace\out\$t.run.err"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        "$(Get-Date -Format s) HUNG $t (killed after $TimeoutSec s)" | Out-File $log -Append -Encoding ascii
        Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'trace-save6' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Get-Process tttracer, WINPROJ -ErrorAction SilentlyContinue | Stop-Process -Force
        Remove-Item 'HKCU:\Software\Microsoft\Office\16.0\Project\Resiliency' -Recurse -Force -ErrorAction SilentlyContinue
    }
    "$(Get-Date -Format s) done $t" | Out-File $log -Append -Encoding ascii
}
"ALLDONE" | Out-File $log -Append -Encoding ascii
