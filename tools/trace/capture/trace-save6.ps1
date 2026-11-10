# Round 5 capture: record one Project 2021 edit + save with TTD, INSIDE THE TRACE VM ONLY.
#   powershell -NoProfile -ExecutionPolicy Bypass -File trace-save6.ps1 -Target wave35-g3-font-size
# Target = the committed pair's base name P: C:\trace\in\<P>-control.mpp is opened INSIDE the recording, the oracle's
# COM edit is repeated (calculation mode, fact reads, settle time as in the oracle script), the result saved to
# C:\trace\work\<P>.mpp and the run to C:\trace\runs\<P>\.  Based on trace-save5.ps1 (wait for "Recording has started").
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Root = "C:\trace",
    [switch]$MidSave          # fix attempt: save the control once before the edit (oracle_w35_g3 does)
)
$ErrorActionPreference = "Stop"
$model = (Get-CimInstance Win32_ComputerSystem).Model
if (-not (Test-Path (Join-Path $Root "TRACE-VM.marker")) -or $model -notmatch "Virtual") { throw "not the trace VM" }

. (Join-Path $PSScriptRoot "specs6.ps1")     # defines $specs (Target -> Mode, Facts, Settle, Edit)
$spec = $specs[$Target]
if (-not $spec) { throw "unknown target $Target" }
$nm = Get-Names $Target
$openStem = if ($spec.Open) { $spec.Open } else { $nm.Ctl }   # round 5b: some pairs were made in one session from a base file (Open)
$src = Join-Path $Root ("in\" + $openStem + ".mpp")
if (-not (Test-Path $src)) { throw "missing input $src" }
$work = Join-Path $Root "work"; $runsRoot = if (Test-Path "E:\runs") { "E:\runs" } else { Join-Path $Root "runs" }; $runs = Join-Path $runsRoot $Target
New-Item -ItemType Directory -Force $work, $runs | Out-Null
$copy = Join-Path $work "$Target-in.mpp"; $out = Join-Path $work "$Target.mpp"
Copy-Item $src $copy -Force
$tracer = Join-Path $env:WINDIR "System32\tttracer.exe"
if (Get-Process WINPROJ -ErrorAction SilentlyContinue) { throw "close every Project window first" }

$app = New-Object -ComObject MSProject.Application
try {
    $app.Visible = $true; $app.DisplayAlerts = $false; $app.UserName = "trace"
    $app.Calculation = $spec.Mode      # pjManual 0, pjAutomatic 1
    $proc = @(Get-Process WINPROJ)
    if ($proc.Count -ne 1) { throw "expected one WINPROJ process, found $($proc.Count)" }
    # round 5b: look for Project's sign-in / licence dialogs before attaching; a sign-in dialog stops the capture
    $sk = (& (Join-Path $PSScriptRoot "r5b-skip.ps1") -ProcId $proc[0].Id -WaitSec 30) -join ' '   # clicks ONLY "Skip for now" (coordinator-authorised)
    "SKIP $Target $sk" | Out-File "C:\trace\out\dialogs.log" -Append -Encoding ascii
    if ($sk -match 'NOINVOKE') { throw "SIGNIN-DIALOG (Skip not invokable): $sk" }
    Start-Sleep -Seconds 4
    $wl = @(& (Join-Path $PSScriptRoot "r5b-winlist.ps1"))
    $wl | Out-File (Join-Path $runs "windows-at-start.txt") -Encoding ascii
    $dl = @($wl | Where-Object { $_ -match '\|NUIDialog\|' })
    if ($dl.Count -gt 0) {
        $dlg = $dl -join ' ; '
        "DIALOG $Target $dlg" | Out-File "C:\trace\out\dialogs.log" -Append -Encoding ascii
        if ($dlg -match 'Sign in|product key|activate') { throw "SIGNIN-DIALOG: $dlg" }
    }
    $attLog = Join-Path $runs "tttracer-attach.log"
    $att = Start-Process $tracer -ArgumentList @("-attach", $proc[0].Id, "-out", "`"$runs`"") -PassThru -WindowStyle Hidden -RedirectStandardOutput $attLog -RedirectStandardError (Join-Path $runs "tttracer-attach.err")
    $deadline = (Get-Date).AddSeconds(90)
    while (-not (Get-ChildItem $runs -Filter *.run -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Get-ChildItem $runs -Filter *.run -ErrorAction SilentlyContinue)) { throw "no .run file appeared" }
    $deadline = (Get-Date).AddSeconds(180)
    while (-not (Select-String -Path $attLog -Pattern "Recording has started" -Quiet -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Select-String -Path $attLog -Pattern "Recording has started" -Quiet -ErrorAction SilentlyContinue)) { throw "recording did not start" }
    Start-Sleep -Seconds 4
    $stg = "C:\trace\out\$Target.stage.txt"; "recording $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Encoding ascii
    [void]$app.FileOpenEx($copy)
    "opened $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Append -Encoding ascii
    $proj = $app.ActiveProject
    $ff = $spec.Facts; if ($ff -eq "g3") { $ff = "Get-G3Facts" } elseif ($ff -eq "insert") { $ff = "Read-Facts" }
    if ($ff -and $ff -ne "none") { & $ff $proj }
    "facts1 $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Append -Encoding ascii
    if ($MidSave -or $spec.MidSave) { [void]$app.FileSaveAs((Join-Path $work "$Target-mid.mpp")) }
    & $spec.Edit $app $proj
    "edit $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Append -Encoding ascii
    if ($ff -and $ff -ne "none" -and -not $spec.NoPost) { & $ff $proj }
    "facts2 $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Append -Encoding ascii
    if ($spec.Settle -gt 0) { Start-Sleep -Seconds $spec.Settle }
    [void]$app.FileSaveAs($out)
    "saved $(Get-Date -Format HH:mm:ss)" | Out-File $stg -Append -Encoding ascii
    & $tracer -stop $proc[0].Id | Tee-Object (Join-Path $runs "tttracer-stop.log")
    [void]$att.WaitForExit(120000)
    [void]$app.FileCloseEx(0)
}
finally {
    $app.Quit(0)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($app)
}
(Get-FileHash $out -Algorithm SHA256).Hash + "  $out" | Tee-Object (Join-Path $runs "saved.sha256")
