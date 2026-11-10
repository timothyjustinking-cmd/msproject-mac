# Round 5b group 5: wave29 pairs (scripts/oracle_w29_percent.py, oracle_w29_update_progress.py, oracle_w29_outline_auto.py).
# Dot-source right after specs6.ps1 (needs $specs, Get-Names, Invoke-Named).  ADDITIONS ONLY.
# Mode 1 (automatic): oracle_w29_reopen.py recorded Application.Calculation = -1 (pjAutomatic) in the same oracle era, and no w29 oracle sets a mode.
# Every pair was made inside ONE oracle session from a base file, so every spec has Open = the base stem in tests/data and replays the chain
# (shaping, SaveAs control, the oracle's fact reads, the edit).  Facts = "none": the oracle's reads happen mid-chain, so they are inside Edit,
# because the project object the runner holds is stale once the chain closes and reopens a file.  The runner's final FileSaveAs writes the edited file.

function G5b_Read($o, [string[]]$names) { foreach ($p in $names) { try { [void]$o.$p } catch { } } }

# oracle_privacy.neutralise(app, project, title): UserName, Author, Manager, Company, Title in that order.
function G5b_Identity($app, $proj, [string]$title) {
    $app.UserName = "Fixture Builder"; $proj.Author = "Fixture Builder"; $proj.Manager = "Fixture Builder"
    $proj.Company = "Fixture Company"; $proj.Title = $title
}

# ---- oracle_w29_percent.py facts: task_facts + assignment_facts, all_facts ----
function G5b_PctTask($t) {
    G5b_Read $t @("ID","UniqueID","OutlineLevel","Manual","Summary","Milestone","Duration","Start","Finish","PercentComplete","PercentWorkComplete","ActualDuration","RemainingDuration","ActualStart","ActualFinish","Work","ActualWork","RemainingWork")
    try {
        $n = $t.Assignments.Count
        for ($i = 1; $i -le $n; $i++) { $a = $t.Assignments($i); G5b_Read $a @("ResourceUniqueID","Units","Work","ActualWork","RemainingWork","Start","Finish") }
    } catch { }
}
function G5b_PctFacts($proj) {
    $pst = $proj.ProjectSummaryTask; G5b_PctTask $pst
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); if ($null -eq $t) { continue }; G5b_PctTask $t; G5b_Read $t @("ID") }
    G5b_Read $proj @("NewTasksCreatedAsManual")
}
# Open = wave25-auto.  Oracle: open a copy, neutralise, facts, add the Milestone row, facts, SaveAs shaped base, close; then open a fresh copy
# of the shaped base, neutralise, [mid: ID 4 to 25 %], facts, SaveAs control, edit, facts (the edited file is saved after those reads).
function G5b_PercentChain($app, $proj, [string]$tag, [int]$taskId, [int]$target, [bool]$mid) {
    $title = "Synthetic Percent Schedule"
    G5b_Identity $app $proj $title
    G5b_PctFacts $proj
    $added = $proj.Tasks.Add("Milestone")
    $proj.NewTasksCreatedAsManual = $false
    $added.Manual = $false
    $added.Milestone = $true
    $d = $null; try { $d = $added.Duration } catch { $d = "<unreadable>" }
    if ($d -ne 0) { $added.Duration = 0 }
    G5b_PctTask $added
    G5b_PctFacts $proj
    $base = "C:\trace\work\$tag-g5b-base.mpp"; $conf = "C:\trace\work\$tag-g5b-conf.mpp"
    [void]$app.FileSaveAs($base)
    [void]$app.FileClose(0)
    Copy-Item $base $conf -Force
    [void]$app.FileOpen($conf)
    $p = $app.ActiveProject
    G5b_Identity $app $p $title
    if ($mid) { $p.Tasks($taskId).PercentComplete = 25 }
    G5b_PctFacts $p
    [void]$app.FileSaveAs("C:\trace\work\$tag-g5b-ctl.mpp")
    $p.Tasks($taskId).PercentComplete = $target
    G5b_PctFacts $p
}

# ---- oracle_w29_update_progress.py facts: task_facts, all_facts (summary row, StatusDate, every task) ----
function G5b_UpdTask($t) { G5b_Read $t @("ID","UniqueID","Manual","Duration","DurationText","PercentComplete","PercentWorkComplete","ActualDuration","RemainingDuration","ActualStart","ActualFinish","Start","Finish","Work","ActualWork","RemainingWork") }
function G5b_UpdFacts($proj) {
    $pst = $proj.ProjectSummaryTask; G5b_UpdTask $pst
    G5b_Read $proj @("StatusDate")
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); if ($null -eq $t) { continue }; G5b_UpdTask $t; G5b_Read $t @("ID") }
}
# Mark on Track has no type-library member: the oracle invokes ribbon button AutomationId UpdateAsScheduled through build\w28\uiainvoke.ps1
# (sleep 1 s, run, sleep 1 s).  The script must sit beside this file in the VM as uiainvoke.ps1 (push a copy of build\w28\uiainvoke.ps1).
function G5b_MarkOnTrack([string]$tag) {
    Start-Sleep -Seconds 1
    $inv = Join-Path $PSScriptRoot "uiainvoke.ps1"
    if (-not (Test-Path $inv)) { throw "missing $inv (push build\w28\uiainvoke.ps1 beside the specs as uiainvoke.ps1)" }
    $log = "C:\trace\work\uia-$tag.log"; if (Test-Path $log) { Remove-Item $log -Force }
    $pr = Start-Process powershell.exe -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File","`"$inv`"","-AutomationId","UpdateAsScheduled","-Name","`"Mark on Track`"","-Out","`"$log`"") -Wait -PassThru -WindowStyle Hidden
    $body = ""; if (Test-Path $log) { $body = (Get-Content $log -Raw) }
    if ($pr.ExitCode -ne 0) { throw "Mark on Track invocation failed for ${tag}: rc=$($pr.ExitCode) $body" }
    Start-Sleep -Seconds 1
}
# Open = synthetic-base.  Oracle: open a copy, neutralise, ID 1 automatic and 60 %, facts, SaveAs prepared base, close; open a fresh copy of it,
# neutralise, command set-up (mark: StatusDate, select row 1; selected: select rows 1 then 2 with Add), SaveAs control, facts, command, facts, SaveAs edited.
# $command: mark | update_all | update_selected; $day: 9 (after) or 7 (before) January 2026, midnight.
function G5b_UpdateChain($app, $proj, [string]$tag, [string]$command, [int]$day) {
    $title = "Synthetic Progress Update Fixture"
    $date = [datetime]::new(2026, 1, $day, 0, 0, 0)
    G5b_Identity $app $proj $title
    $proj.Tasks(1).Manual = $false
    $proj.Tasks(1).PercentComplete = 60
    G5b_UpdFacts $proj
    $prep = "C:\trace\work\$tag-g5b-prepared.mpp"; $conf = "C:\trace\work\$tag-g5b-conf.mpp"
    [void]$app.FileSaveAs($prep)
    [void]$app.FileClose(0)
    Copy-Item $prep $conf -Force
    [void]$app.FileOpen($conf)
    $p = $app.ActiveProject
    G5b_Identity $app $p $title
    if ($command -eq "mark") {
        $p.StatusDate = $date
        Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative") @(1, "Name", $false)
    } elseif ($command -eq "update_selected") {
        Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative","Add") @(1, "Name", $false, $false)
        Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative","Add") @(2, "Name", $false, $true)
    }
    [void]$app.FileSaveAs("C:\trace\work\$tag-g5b-ctl.mpp")
    G5b_UpdFacts $p
    if ($command -eq "mark") { G5b_MarkOnTrack $tag }
    elseif ($command -eq "update_all") { [void]$app.UpdateProject($true, $date, 1) }
    else { [void]$app.UpdateProject($false, $date, 1) }
    G5b_UpdFacts $p
}

# ---- oracle_w29_outline_auto.py: row() for the project row and Tasks 1 to 3 ----
function G5b_OutFacts($proj) {
    $names = "ID","UniqueID","OutlineLevel","OutlineNumber","Summary","Manual","Duration","DurationText","PercentComplete","Start","Finish","ActualStart","ActualFinish","FreeSlack","TotalSlack"
    $pst = $proj.ProjectSummaryTask; G5b_Read $pst $names
    for ($k = 1; $k -le 3; $k++) { $t = $proj.Tasks($k); G5b_Read $t $names }
}
# Open = synthetic-base.  Oracle: open a copy, neutralise, set_manual, [pre-indent], SaveAs control, control facts, the edit; the edited file is saved
# BEFORE its facts are read, so no reads follow the edit.
function G5b_OutlineChain($app, $proj, [string]$kind) {
    G5b_Identity $app $proj "Synthetic Outline Fixture"
    if ($kind -eq "manual-parent") { $proj.Tasks(1).Manual = $true; $proj.Tasks(2).Manual = $false }
    else { $proj.Tasks(1).Manual = $false; $proj.Tasks(2).Manual = $false; $proj.Tasks(3).Manual = $false }
    if ($kind -eq "summary") { $proj.Tasks(3).OutlineIndent() }
    elseif ($kind -ne "indent") { $proj.Tasks(2).OutlineIndent() }      # outdent and manual-parent: the indent_auto shape first
    [void]$app.FileSaveAs("C:\trace\work\w29-outline-$kind-g5b-ctl.mpp")
    G5b_OutFacts $proj
    if ($kind -eq "indent") { $proj.Tasks(2).OutlineIndent() }
    elseif ($kind -eq "summary") { $proj.Tasks(2).OutlineIndent() }
    else { $proj.Tasks(2).OutlineOutdent() }
}

# ---- targets ----
# percent (Open = wave25-auto; the controls are named ...-control, not <edited>-control)
$specs["wave29-percent-leaf40"]       = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "wave25-auto"; Ctl = "wave29-percent-leaf-control";      Null = "wave29-percent-leaf-control-null";      Edit = { param($app, $proj) G5b_PercentChain $app $proj "wave29-percent-leaf40" 3 40 $false } }
$specs["wave29-percent-work40"]       = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "wave25-auto"; Ctl = "wave29-percent-work-control";      Null = "wave29-percent-work-control-null";      Edit = { param($app, $proj) G5b_PercentChain $app $proj "wave29-percent-work40" 6 40 $false } }
$specs["wave29-percent-milestone100"] = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "wave25-auto"; Ctl = "wave29-percent-milestone-control"; Null = "wave29-percent-milestone-control-null"; Edit = { param($app, $proj) G5b_PercentChain $app $proj "wave29-percent-milestone100" 7 100 $false } }
$specs["wave29-percent-mid60"]        = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "wave25-auto"; Ctl = "wave29-percent-mid25-control";     Null = "wave29-percent-mid25-control-null";     Edit = { param($app, $proj) G5b_PercentChain $app $proj "wave29-percent-mid60" 4 60 $true } }
# update progress (Open = synthetic-base; default Ctl/Null names fit)
$specs["wave29-mark-60-after"]       = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_UpdateChain $app $proj "wave29-mark-60-after" "mark" 9 } }
$specs["wave29-mark-60-before"]      = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_UpdateChain $app $proj "wave29-mark-60-before" "mark" 7 } }
$specs["wave29-update-60-after"]     = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_UpdateChain $app $proj "wave29-update-60-after" "update_all" 9 } }
$specs["wave29-update-60-before"]    = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_UpdateChain $app $proj "wave29-update-60-before" "update_all" 7 } }
$specs["wave29-update-60-selected"]  = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_UpdateChain $app $proj "wave29-update-60-selected" "update_selected" 9 } }
# outline on automatic rows (Open = synthetic-base)
$specs["wave29-outline-auto-indent"]    = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_OutlineChain $app $proj "indent" } }
$specs["wave29-outline-auto-summary"]   = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_OutlineChain $app $proj "summary" } }
$specs["wave29-outline-auto-outdent"]   = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_OutlineChain $app $proj "outdent" } }
$specs["wave29-outline-manual-parent"]  = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Edit = { param($app, $proj) G5b_OutlineChain $app $proj "manual-parent" } }
