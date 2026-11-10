# Round 5b group g5: specs for the wave27 / wave28 / wave23-flags pairs.  Dot-source right after specs6.ps1 (needs $specs, Get-Names, Invoke-Named).
# Oracles: scripts/oracle_w28_privacy.py (made the committed wave27-* and wave23-flags files), oracle_w28_actuals.py, oracle_w28_new_task_mode.py,
# oracle_w28_outline_edit.py, oracle_w28_split.py, oracle_w28_update.py.  None of them sets Application.Calculation, so the app mode of the
# oracle machine applied; $G5aMode (1 = automatic, as for the wave34 and wave30 groups) is the one switch to flip if the gates say otherwise.
# Every spec has Facts = "none": the oracle's fact reads happen at points that do not line up with the runner's before/after pair
# (after the control save, before the edit), so each Edit scriptblock replays the whole chain from the committed base (Open) including the
# oracle's SaveAs of the control (to C:\trace\work\g5a-*), its fact reads, then the edit and the oracle's after-edit reads.  The runner saves last.
if ($null -eq $G5aMode) { $G5aMode = 1 }

function G5a_Neutralise($app, $proj, [string]$title) {      # scripts/oracle_privacy.py neutralise()
    $app.UserName = "Fixture Builder"; $proj.Author = "Fixture Builder"; $proj.Manager = "Fixture Builder"; $proj.Company = "Fixture Company"; $proj.Title = $title
}
function G5a_ReadProps($obj, [string[]]$names) { foreach ($p in $names) { try { [void]$obj.$p } catch { } } }

# ---- facts readers (one per oracle's task_facts()/row(); plain loops, no @() around COM objects) ----
$G5aActProps = "ID","UniqueID","Manual","Duration","DurationText","PercentComplete","PercentWorkComplete","ActualDuration","RemainingDuration","ActualStart","ActualFinish","Start","Finish","Work","ActualWork","RemainingWork"
function G5a_ActFacts($proj, [bool]$status) {               # oracle_w28_actuals.all_facts / oracle_w28_update.all_facts (status = StatusDate after the project row)
    $ps = $proj.ProjectSummaryTask; G5a_ReadProps $ps $G5aActProps
    if ($status) { try { [void]$proj.StatusDate } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $tk = $proj.Tasks($k); G5a_ReadProps $tk $G5aActProps }
}
$G5aNtmProps = "ID","UniqueID","Manual","Summary","Duration","Start","Finish","PercentComplete"
function G5a_NtmFacts($proj) {                              # oracle_w28_new_task_mode.mode_facts
    try { [void]$proj.NewTasksCreatedAsManual } catch { }
    $ps = $proj.ProjectSummaryTask; G5a_ReadProps $ps $G5aNtmProps
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $tk = $proj.Tasks($k); G5a_ReadProps $tk $G5aNtmProps }
}
function G5a_NtmSet($proj, [bool]$manual) { $proj.NewTasksCreatedAsManual = $manual; try { [void]$proj.NewTasksCreatedAsManual } catch { } }   # set_mode(): set, read back
$G5aOutProps = "ID","UniqueID","OutlineLevel","OutlineNumber","Summary","Manual","Duration","DurationText","PercentComplete","Start","Finish","ActualStart","ActualFinish","FreeSlack","TotalSlack"
function G5a_OutFacts($proj) {                              # oracle_w28_outline_edit.all_facts (project row, Tasks 1..3; the oracle reads FreeSlack and TotalSlack)
    $ps = $proj.ProjectSummaryTask; G5a_ReadProps $ps $G5aOutProps
    for ($k = 1; $k -le 3; $k++) { $tk = $proj.Tasks($k); G5a_ReadProps $tk $G5aOutProps }
}
$G5aSplitProps = "ID","UniqueID","Summary","Manual","Duration","DurationText","Start","Finish","Stop","Resume","ActualStart","ActualFinish","PercentComplete"
function G5a_SplitRow($t) {                                 # oracle_w28_split.row
    G5a_ReadProps $t $G5aSplitProps
    try { $sp = $t.SplitParts; $n = $sp.Count; for ($i = 1; $i -le $n; $i++) { $q = $sp.Item($i); G5a_ReadProps $q @("Index","Start","Finish") } } catch { }
}
function G5a_SplitFacts($proj) {
    $ps = $proj.ProjectSummaryTask; G5a_SplitRow $ps
    for ($k = 1; $k -le 3; $k++) { $tk = $proj.Tasks($k); G5a_SplitRow $tk }
}
$G5aPrivProps = "ID","UniqueID","PercentComplete","ActualDuration","RemainingDuration","Duration","ActualStart","ActualFinish","Start","Finish","Flag10"
function G5a_PrivFacts($proj) {                             # oracle_w28_privacy.task_facts (tasks only)
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $tk = $proj.Tasks($k); G5a_ReadProps $tk $G5aPrivProps }
}

# ---- wave27 / wave23-flags: made by oracle_w28_privacy.py (C:/w28), each from a fresh copy of its base ----
$specs["wave27-percent-actuals"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "synthetic-base"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Actuals Fixture"
    $proj.Tasks(1).PercentComplete = 50
    G5a_PrivFacts $proj } }
$specs["wave27-priority-750"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave27-priority-control"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Priority Fixture"
    try { [void]$proj.ProjectSummaryTask.Priority } catch { }
    [void]$app.FileSaveAs("C:\trace\work\g5a-prio-mid1.mpp")
    $proj.ProjectSummaryTask.Priority = 750
    try { [void]$proj.ProjectSummaryTask.Priority } catch { } } }
$specs["wave27-unlink"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "wave23-outline"; Ctl = "wave27-unlink-control"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Unlink Fixture"
    [void]$app.FileSaveAs("C:\trace\work\g5a-unlink-mid1.mpp")
    [void]$app.EditGoTo(4)
    [void]$app.UnlinkTasks() } }
$specs["wave23-flags"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "wave23-outline"; Ctl = "wave23-outline"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Flags Fixture"
    $proj.Tasks(3).Flag10 = $true
    G5a_PrivFacts $proj } }

# ---- wave28 percent (oracle_w28_actuals.py): the three auto pairs are ONE session chain from synthetic-base, the manual pair a later fresh open ----
function G5a_PctChain($app, $proj, [bool]$autoFirst, [int[]]$steps, [string]$tag) {
    G5a_Neutralise $app $proj "Synthetic Percent Fixture"
    if ($autoFirst) { $proj.Tasks(1).Manual = $false }
    G5a_ActFacts $proj $false                                # auto_initial / manual_initial
    for ($i = 0; $i -lt $steps.Count; $i++) {
        [void]$app.FileSaveAs("C:\trace\work\$tag-ctl$($i + 1).mpp")     # the committed control of this step
        G5a_ActFacts $proj $false                            # entry["control"]
        $proj.Tasks(1).PercentComplete = $steps[$i]
        G5a_ActFacts $proj $false                            # entry["after_edit"]
        if ($i -lt ($steps.Count - 1)) { [void]$app.FileSaveAs("C:\trace\work\$tag-ed$($i + 1).mpp") }   # edited save of an earlier step; the last one is the runner's
    }
}
$specs["wave28-percent-auto40"]  = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-percent-auto0-control";   Null = "wave28-percent-auto0-control-null";   Edit = { param($app, $proj) G5a_PctChain $app $proj $true @(40) "g5a-pa40" } }
$specs["wave28-percent-auto100"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-percent-auto40-control";  Null = "wave28-percent-auto40-control-null";  Edit = { param($app, $proj) G5a_PctChain $app $proj $true @(40, 100) "g5a-pa100" } }
$specs["wave28-percent-auto0"]   = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-percent-auto100-control"; Null = "wave28-percent-auto100-control-null"; Edit = { param($app, $proj) G5a_PctChain $app $proj $true @(40, 100, 0) "g5a-pa0" } }
$specs["wave28-percent-manual40"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-percent-manual0-control"; Null = "wave28-percent-manual0-control-null"; Edit = { param($app, $proj) G5a_PctChain $app $proj $false @(40) "g5a-pm40" } }

# ---- wave28 update and mark on track (oracle_w28_update.py).  STATUS = 2026-01-07 00:00 as a plain COM date. ----
$specs["wave28-update"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-update-control"; Null = "wave28-update-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Update Fixture"
    [void]$app.FileSaveAs("C:\trace\work\g5a-update-mid1.mpp")
    G5a_ActFacts $proj $true
    [void]$app.UpdateProject($true, [datetime]::new(2026, 1, 7, 0, 0, 0), 1)
    G5a_ActFacts $proj $true } }
# Mark on Track has no COM call: the oracle invokes the ribbon button (AutomationId UpdateAsScheduled) through UI Automation with scripts/oracle_w28... build/w28/uiainvoke.ps1.
# That script must be pushed to C:\trace\cmd\uiainvoke.ps1 first; these two targets are UNVERIFIED and need the coordinator's say-so (UIA click on a ribbon button in the VM).
function G5a_MarkOnTrack([string]$tag) {
    Start-Sleep -Seconds 1
    $s = "C:\trace\cmd\uiainvoke.ps1"; if (-not (Test-Path $s)) { throw "push build/w28/uiainvoke.ps1 to C:\trace\cmd first" }
    $lg = "C:\trace\work\g5a-uia-$tag.log"; if (Test-Path $lg) { Remove-Item $lg -Force }
    $exe = if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh" } else { "powershell.exe" }
    & $exe -NoProfile -ExecutionPolicy Bypass -File $s -AutomationId UpdateAsScheduled -Name "Mark on Track" -Out $lg
    if ($LASTEXITCODE -ne 0) { throw "Mark on Track invocation failed ($LASTEXITCODE), see $lg" }
    Start-Sleep -Seconds 1
}
$specs["wave28-mark-auto"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-mark-auto-control"; Null = "wave28-mark-auto-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Mark Fixture"
    $proj.Tasks(1).Manual = $false
    $proj.StatusDate = [datetime]::new(2026, 1, 7, 0, 0, 0)
    Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative") @(1, "Name", $false)
    [void]$app.FileSaveAs("C:\trace\work\g5a-markauto-mid1.mpp")
    G5a_ActFacts $proj $true
    G5a_MarkOnTrack "mark-auto"
    G5a_ActFacts $proj $true } }
$specs["wave28-mark-manual"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-mark-manual-control"; Null = "wave28-mark-manual-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Mark Fixture"
    $proj.StatusDate = [datetime]::new(2026, 1, 7, 0, 0, 0)
    Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative") @(2, "Name", $false)
    [void]$app.FileSaveAs("C:\trace\work\g5a-markman-mid1.mpp")
    G5a_ActFacts $proj $true
    G5a_MarkOnTrack "mark-manual"
    G5a_ActFacts $proj $true } }

# ---- wave28 new task mode (oracle_w28_new_task_mode.py): per pair, open a copy, neutralise, set mode (control), save, mode_facts, set mode (edited) ----
$specs["wave28-newtask-auto"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-newtask-manual-control"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic New Task Mode Fixture"
    G5a_NtmSet $proj $true
    [void]$app.FileSaveAs("C:\trace\work\g5a-ntauto-mid1.mpp")
    G5a_NtmFacts $proj
    G5a_NtmSet $proj $false } }
$specs["wave28-newtask-manual"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-newtask-auto-control"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic New Task Mode Fixture"
    G5a_NtmSet $proj $false
    [void]$app.FileSaveAs("C:\trace\work\g5a-ntman-mid1.mpp")
    G5a_NtmFacts $proj
    G5a_NtmSet $proj $true } }

# ---- wave28 outline edit (oracle_w28_outline_edit.py): three pairs, each a fresh copy of synthetic-base ----
$specs["wave28-outline-indent"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-outline-indent-control"; Null = "wave28-outline-indent-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Outline Fixture"
    [void]$app.FileSaveAs("C:\trace\work\g5a-oind-mid1.mpp")
    G5a_OutFacts $proj
    [void]$proj.Tasks(2).OutlineIndent() } }
$specs["wave28-outline-summary"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-outline-summary-control"; Null = "wave28-outline-summary-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Outline Fixture"
    [void]$proj.Tasks(3).OutlineIndent()
    [void]$app.FileSaveAs("C:\trace\work\g5a-osum-mid1.mpp")
    G5a_OutFacts $proj
    [void]$proj.Tasks(2).OutlineIndent() } }
$specs["wave28-outline-outdent"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-outline-outdent-control"; Null = "wave28-outline-outdent-control-null"; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Outline Fixture"
    [void]$proj.Tasks(2).OutlineIndent()
    [void]$app.FileSaveAs("C:\trace\work\g5a-oout-mid1.mpp")
    G5a_OutFacts $proj
    [void]$proj.Tasks(2).OutlineOutdent() } }

# ---- wave28 split (oracle_w28_split.py): Manual False, save control, read row facts (incl. SplitParts), split ----
$specs["wave28-split"] = @{ Mode = $G5aMode; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "wave28-split-control"; Null = $null; Edit = { param($app, $proj)
    G5a_Neutralise $app $proj "Synthetic Split Fixture"
    $task = $proj.Tasks(1)
    $task.Manual = $false
    [void]$app.FileSaveAs("C:\trace\work\g5a-split-mid1.mpp")
    G5a_SplitFacts $proj
    $task.SplitParts.Item(1).Finish = [datetime]::new(2026, 1, 7, 17, 0, 0)
    [void]$task.SplitParts.Add([datetime]::new(2026, 1, 9, 8, 0, 0), [datetime]::new(2026, 1, 12, 17, 0, 0)) } }
