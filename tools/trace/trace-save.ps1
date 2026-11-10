# Record one Project 2021 save with Time Travel Debugging, INSIDE THE TRACE VM ONLY.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File trace-save.ps1 -Target calculate
#   (inside the VM, from an elevated Windows Terminal; PowerShell 7 is not installed there)
#
# Targets repeat a committed pair's edit on a copy of its committed control, so the trace
# writes the same bytes as the pair's edited file: calculate, calculate-second, baseline,
# baseline-open (the file is opened inside the recording), insert-summary, insert-milestone,
# bold, and w35-<unlinked|linked|lag|snet>-<calc|null> for the four wave 35 calculate pairs;
# "null" saves the control with no edit (the noise run).
#
# Trace round fixes, Tue 10 to Wed 11 Nov 2026 (spec/writers/evidence/trace-*.md):
# - the save waits for "Recording has started" in the attach log; saving when the .run file
#   appeared gave 0 ms traces;
# - the calculation mode is set per target (manual for null, calculate and the w35 pairs,
#   automatic for baseline and the inserts), as the pairs were made;
# - the insert targets read every task through COM before saving, as the pairs did;
# - the edit runs inside the recording, so the engine's write-back is traced.
# The gate is tools/trace/trace_gate.py; tools/trace/chain.js holds the cdb/TTD helpers.
# Inputs:  C:\trace\in\<fixture>.mpp, copied from tests/data (public synthetic fixtures only).
# Outputs: C:\trace\work\<target>.mpp (the saved file) and C:\trace\runs\<target>\*.run
#          (the trace; it holds the whole process memory and never leaves the VM).
param(
    [Parameter(Mandatory = $true)]
    [string]$Target,
    [string]$Root = "C:\trace"
)
$ErrorActionPreference = "Stop"

# Refuse to run anywhere but the marked trace VM.
$model = (Get-CimInstance Win32_ComputerSystem).Model
if (-not (Test-Path (Join-Path $Root "TRACE-VM.marker")) -or $model -notmatch "Virtual") {
    throw "not the trace VM (model '$model', no $Root\TRACE-VM.marker): refusing to trace"
}

# trace-save4 (11 Nov 2026): adds bold and the four wave 35 pairs (w35-<pair>-calc, w35-<pair>-null).
$fixtureMap = @{
    "null"             = "wave30-calculate-control.mpp"
    "calculate"        = "wave30-calculate-control.mpp"
    "calculate-second" = "wave34-calculate-second-control.mpp"
    "baseline"         = "wave30-baseline-control.mpp"
    "insert-summary"   = "wave30-insert-summary-control.mpp"
    "insert-milestone" = "wave30-insert-milestone-control.mpp"
    "bold"             = "wave30-textstyle-control.mpp"
    "baseline-open"    = "wave30-baseline-control.mpp"
}
foreach ($p in "unlinked", "linked", "lag", "snet") {
    $fixtureMap["w35-$p-calc"] = "wave35-calculate-$p-control.mpp"
    $fixtureMap["w35-$p-null"] = "wave35-calculate-$p-control.mpp"
}
$fixture = $fixtureMap[$Target]
if (-not $fixture) { throw "unknown target $Target" }
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
    # pjManual = 0, pjAutomatic = 1.  The calculate/baseline targets want manual; the insert pairs were made
    # in automatic mode (10 Nov 2026 gate failure: the 0x0b4004e4 block of the new row was missing; the
    # app-level mode sticks between runs, so set it explicitly).
    # (the baseline pair was also made in automatic mode: oracle_w30_project_pairs.py sets manual only for calculate)
    if ($Target -like "calculate*" -or $Target -eq "null" -or $Target -like "w35-*") { $app.Calculation = 0 } else { $app.Calculation = 1 }
    # trace-save5 (11 Nov 2026): the file is opened INSIDE the recording (below), so the loaded Props records and every
    # other in-memory structure have their first writes in the trace.
    # The insert pairs read every task's properties through COM before and after the edit
    # (scripts/oracle_w30_insert.py all_facts); the reads allocate var blocks that the save then
    # writes.  Repeat the reads (10 Nov 2026 gate failure: 12 bytes of Task Var2Data short).
    function Read-Facts($proj) {
        [void]$proj.ProjectSummaryTask.ID; [void]$proj.Tasks.Count
        for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
            $t = $proj.Tasks($k)
            foreach ($p in "ID","UniqueID","Summary","Milestone","Manual","Duration","PercentComplete","Start","Finish","OutlineLevel","OutlineNumber") { try { [void]$t.$p } catch { } }
            try { $n = $t.Assignments.Count; for ($a = 1; $a -le $n; $a++) { $as = $t.Assignments($a); foreach ($p in "UniqueID","ResourceUniqueID","Units","Work","Start","Finish") { try { [void]$as.$p } catch { } } } } catch { }
        }
    }
    if ($Target -like "insert-*") { Read-Facts $app.ActiveProject }
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
    # 10 Nov 2026 fix: the .run file appears at "Initializing", before recording starts; saving then left a
    # 0 ms trace.  Wait for "Recording has started" in the attach log, then a few seconds more.
    $deadline = (Get-Date).AddSeconds(180)
    while (-not (Select-String -Path $attLog -Pattern "Recording has started" -Quiet -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Select-String -Path $attLog -Pattern "Recording has started" -Quiet -ErrorAction SilentlyContinue)) { throw "recording did not start (see $attLog)" }
    Start-Sleep -Seconds 4
    [void]$app.FileOpenEx($copy)
    # trace-save3 (10 Nov 2026): the edit itself is inside the recording, so the engine's write-back
    # (calculate), the baseline copy and the inserted rows are all traced, not only the save.
    switch ($Target) {
        "calculate"        { [void]$app.CalculateProject() }
        "calculate-second" { [void]$app.CalculateProject() }
        "bold"             { [void]$app.SelectTaskField(1, "Name", $false); [void]$app.FontBold($true) }
        "w35-unlinked-calc" { [void]$app.CalculateProject() }
        "w35-linked-calc"   { [void]$app.CalculateProject() }
        "w35-lag-calc"      { [void]$app.CalculateProject() }
        "w35-snet-calc"     { [void]$app.CalculateProject() }
        "baseline-open"    { [void]$app.BaselineSave($true, 0, 0) }
        "baseline"         { [void]$app.BaselineSave($true, 0, 0) }
        "insert-summary"   { [void]$app.SelectTaskField(2, "Name", $false); [void]$app.InsertSummaryTask() }
        "insert-milestone" { [void]$app.SelectTaskField(2, "Name", $false); [void]$app.InsertMilestoneTask() }
        default            { }
    }
    if ($Target -like "insert-*") { Read-Facts $app.ActiveProject }
    # Under the recorder everything runs about ten times slower, and Project finishes some work on a
    # background thread after the command returns; saving at once gave a baseline file 27 bytes of
    # Task FixedData away from the pair's.  Let it settle (10 Nov 2026).
    if ($Target -like "baseline*") { Start-Sleep -Seconds 25 }
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
