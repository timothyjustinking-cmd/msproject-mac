# Record one Project 2021 save with Time Travel Debugging, INSIDE THE TRACE VM ONLY.
#
#   pwsh -NoProfile -File trace-save.ps1 -Target calculate
#
# Targets repeat a committed pair's edit on a copy of its committed control, so the trace
# writes the same bytes as the pair's edited file: calculate, calculate-second, baseline,
# insert-summary, insert-milestone; "null" saves the control with no edit (the noise run).
# Inputs:  C:\trace\in\<fixture>.mpp, copied from tests/data (public synthetic fixtures only).
# Outputs: C:\trace\work\<target>.mpp (the saved file) and C:\trace\runs\<target>\*.run
#          (the trace; it holds the whole process memory and never leaves the VM).
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("null", "calculate", "calculate-second", "baseline", "insert-summary", "insert-milestone")]
    [string]$Target,
    [string]$Root = "C:\trace"
)
$ErrorActionPreference = "Stop"

# Refuse to run anywhere but the marked trace VM.
$model = (Get-CimInstance Win32_ComputerSystem).Model
if (-not (Test-Path (Join-Path $Root "TRACE-VM.marker")) -or $model -notmatch "Virtual") {
    throw "not the trace VM (model '$model', no $Root\TRACE-VM.marker): refusing to trace"
}

$fixture = @{
    "null"             = "wave30-calculate-control.mpp"
    "calculate"        = "wave30-calculate-control.mpp"
    "calculate-second" = "wave34-calculate-second-control.mpp"
    "baseline"         = "wave30-baseline-control.mpp"
    "insert-summary"   = "wave30-insert-summary-control.mpp"
    "insert-milestone" = "wave30-insert-milestone-control.mpp"
}[$Target]
$src = Join-Path $Root "in\$fixture"
if (-not (Test-Path $src)) { throw "missing input $src (copy it from tests/data)" }
$work = Join-Path $Root "work"
$runs = Join-Path $Root "runs\$Target"
New-Item -ItemType Directory -Force $work, $runs | Out-Null
$copy = Join-Path $work "$Target-in.mpp"
$out = Join-Path $work "$Target.mpp"
Copy-Item $src $copy -Force

$tracer = Join-Path $env:WINDIR "System32\tttracer.exe"
if (Get-Process WINPROJ -ErrorAction SilentlyContinue) { throw "close every Project window first" }

$app = New-Object -ComObject MSProject.Application
try {
    $app.Visible = $true
    $app.DisplayAlerts = $false
    $app.UserName = "trace"
    $app.Calculation = 0          # pjManual: nothing recalculates before the traced edit
    [void]$app.FileOpenEx($copy)
    switch ($Target) {
        "calculate"        { [void]$app.CalculateProject() }
        "calculate-second" { [void]$app.CalculateProject() }
        "baseline"         { [void]$app.BaselineSave($true, 0, 0) }
        "insert-summary"   { [void]$app.SelectTaskField(2, "Name", $false); [void]$app.InsertSummaryTask() }
        "insert-milestone" { [void]$app.SelectTaskField(2, "Name", $false); [void]$app.InsertMilestoneTask() }
        default            { }
    }
    $proc = @(Get-Process WINPROJ)
    if ($proc.Count -ne 1) { throw "expected one WINPROJ process, found $($proc.Count)" }
    $winproj = $proc[0].Path
    $hash = (Get-FileHash $winproj -Algorithm SHA256).Hash
    "WINPROJ $winproj $($proc[0].MainModule.FileVersionInfo.FileVersion) sha256 $hash" |
        Tee-Object (Join-Path $runs "build.txt")

    # Attach the recorder only around the save, so the trace holds the save and little else.
    # "tttracer -attach" stays in the foreground until the trace stops (checked 9 Nov 2026), so it
    # is started detached and the script waits for the .run file before it saves.
    $attLog = Join-Path $runs "tttracer-attach.log"
    $att = Start-Process $tracer -ArgumentList @("-attach", $proc[0].Id, "-out", "`"$runs`"") -PassThru -WindowStyle Hidden -RedirectStandardOutput $attLog -RedirectStandardError (Join-Path $runs "tttracer-attach.err")
    $deadline = (Get-Date).AddSeconds(90)
    while (-not (Get-ChildItem $runs -Filter *.run -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Get-ChildItem $runs -Filter *.run -ErrorAction SilentlyContinue)) { throw "no .run file appeared: attach failed (see $attLog)" }
    Start-Sleep -Seconds 2
    [void]$app.FileSaveAs($out)
    & $tracer -stop $proc[0].Id | Tee-Object (Join-Path $runs "tttracer-stop.log")
    [void]$att.WaitForExit(120000)
    [void]$app.FileCloseEx(0)     # pjDoNotSave: the save above is the only one
}
finally {
    $app.Quit(0)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($app)
}
(Get-FileHash $out -Algorithm SHA256).Hash + "  $out" | Tee-Object (Join-Path $runs "saved.sha256")
Get-ChildItem $runs -Filter *.run | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB) } }
