# Target specs for trace-save6.ps1 (dot-sourced).  Each entry repeats the oracle script's COM edit.
# Mode: 0 manual, 1 automatic.  Facts: g3 (oracle_w35_g3 facts() before and after), insert (Read-Facts), none.

function Get-G3Facts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","OutlineLevel","OutlineNumber","Summary","Milestone","Manual","Duration","Start","Finish") { try { [void]$t.$p } catch { } }
        try { [void]$t.Assignments.Count } catch { }
    }
}
function Read-Facts($proj) {
    [void]$proj.ProjectSummaryTask.ID; [void]$proj.Tasks.Count
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","Summary","Milestone","Manual","Duration","PercentComplete","Start","Finish","OutlineLevel","OutlineNumber") { try { [void]$t.$p } catch { } }
        try { $n = $t.Assignments.Count; for ($a = 1; $a -le $n; $a++) { $as = $t.Assignments($a); foreach ($p in "UniqueID","ResourceUniqueID","Units","Work","Start","Finish") { try { [void]$as.$p } catch { } } } } catch { }
    }
}

$specs = @{}
# File names of a target's trio (control, reference = committed edited file, null save); spec keys Ctl/Ref/Null override the defaults.
# Null = $null when no committed null exists (the gate then gets the control as its own null: no mask, noise judged by eye).
function Get-Names([string]$t) {
    $s = $specs[$t]
    $ctl = if ($s.Ctl) { $s.Ctl } else { "$t-control" }
    $ref = if ($s.Ref) { $s.Ref } else { $t }
    $nul = if ($s.ContainsKey("Null")) { $s.Null } else { "$t-control-null" }
    @{ Ctl = $ctl; Ref = $ref; Null = $nul }
}
$g = "wave35-g3-"

# ---- group 1: wave35-g3 (oracle_w35_g3.py), automatic calculation ----
$specs[$g + "font-bold-row2"]  = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(2, "Name", $false); [void]$app.FontBold($true) } }
$specs[$g + "font-bold-start"] = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Start", $false); [void]$app.FontBold($true) } }
$specs[$g + "font-bold-range"] = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative") @(1, "Name", $false); Invoke-Named $app "SelectTaskField" @("Row","Column","RowRelative","Extend") @(2, "Name", $false, $true); [void]$app.FontBold($true) } }
$specs[$g + "font-bold-off"]   = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.FontBold($false) } }
$specs[$g + "font-italic"]     = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.FontItalic($true) } }
$specs[$g + "font-underline"]  = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.Font32Ex("Calibri", 11, $false, $false, $true, 0, $false, 0, 0, $false) } }
$specs[$g + "font-size"]       = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.Font32Ex("Calibri", 14, $false, $false, $false, 0, $false, 0, 0, $false) } }
$specs[$g + "font-name"]       = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.Font32Ex("Arial", 11, $false, $false, $false, 0, $false, 0, 0, $false) } }
$specs[$g + "font-colour"]     = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.Font32Ex("Calibri", 11, $false, $false, $false, 255, $false, 0, 0, $false) } }
$specs[$g + "insert-milestone-nested"] = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(4, "Name", $false); [void]$app.InsertMilestoneTask() } }
$specs[$g + "insert-milestone-first"]  = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(1, "Name", $false); [void]$app.InsertMilestoneTask() } }
$specs[$g + "insert-milestone-last"]   = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(6, "Name", $false); [void]$app.InsertMilestoneTask() } }
$specs[$g + "insert-summary-nested"]   = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.SelectTaskField(4, "Name", $false); [void]$app.InsertSummaryTask() } }
$specs[$g + "baseline-set-nested"]     = @{ Mode = 1; Facts = "g3"; Settle = 25; Edit = { param($app, $proj) [void]$app.BaselineSave($true, 0, 0) } }
$specs[$g + "wbs-renumber-defined"]    = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) [void]$app.WBSCodeRenumber($true); for ($i = 1; $i -le $proj.Tasks.Count; $i++) { try { [void]$proj.Tasks($i).WBS } catch { } } } }
$specs[$g + "notes-generator"]         = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(3).Notes = "Generator probe line."; for ($i = 1; $i -le $proj.Tasks.Count; $i++) { try { [void]$proj.Tasks($i).Notes } catch { } } } }
$specs[$g + "assign-remove-port-saved"] = @{ Mode = 1; Facts = "g3"; Settle = 0; Edit = { param($app, $proj)
    $task = $proj.Tasks(1)
    for ($i = 1; $i -le $task.Assignments.Count; $i++) { $c = $task.Assignments($i); if ($c.ResourceUniqueID -eq 2) { $c.Delete(); break } }
    [void]$task.Assignments.Count } }

# ---- round 5b: licence check, null save of the wave30 calculate control (round 1's null), manual mode, no edit ----
$specs["r5b-null-w30calc"] = @{ Mode = 0; Facts = "none"; Settle = 0; Ctl = "wave30-calculate-control"; Ref = "wave30-calculate-control-null"; Null = "wave30-calculate-control-null"; Edit = { param($app, $proj) } }

# ---- group 2: wave34 (oracle_w34_*.py); the oracle sets no calculation mode, so automatic (the default) ----
function Get-TaskOpsFacts($proj) {
    [void]$proj.Tasks.Count
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","Manual","Summary","Milestone","Duration","Start","Finish","PercentComplete","OutlineLevel","Text1") { try { [void]$t.$p } catch { } }
        try { [void]$t.Name.Length } catch { }
        try { $c = $t.TaskDependencies.Count; for ($d = 1; $d -le $c; $d++) { $dep = $t.TaskDependencies($d); [void]$dep.From.UniqueID; [void]$dep.To.UniqueID; [void]$dep.Type; [void]$dep.Lag } } catch { }
        try { $n = $t.Assignments.Count; for ($a = 1; $a -le $n; $a++) { $as = $t.Assignments($a); foreach ($p in "UniqueID","ResourceUniqueID","Units") { try { [void]$as.$p } catch { } } } } catch { }
    }
}
function Get-NotesFacts($proj) {
    [void]$proj.Tasks.Count
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","Start","Finish","Duration","PercentComplete","Manual") { try { [void]$t.$p } catch { } }
        try { [void]$t.Notes.Length } catch { }
    }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { try { [void]$proj.Tasks($k).Notes } catch { } }
}
function Get-AssignFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","Work","Duration","Start","Finish") { try { [void]$t.$p } catch { } }
        try { $n = $t.Assignments.Count; for ($a = 1; $a -le $n; $a++) { $as = $t.Assignments($a); foreach ($p in "TaskID","TaskUniqueID","ResourceID","ResourceUniqueID","Units","Work","Start","Finish","UniqueID") { try { [void]$as.$p } catch { } } } } catch { }
    }
    for ($k = 1; $k -le $proj.Resources.Count; $k++) { $r = $proj.Resources($k); foreach ($p in "ID","UniqueID","Work","MaxUnits","Type") { try { [void]$r.$p } catch { } } }
}
function Get-ResFacts($proj) {
    [void]$proj.Resources.Count
    try { [void]$proj.Calendars } catch { }
    try { [void]$proj.BaseCalendars } catch { }
    for ($k = 1; $k -le $proj.Resources.Count; $k++) { $r = $proj.Resources($k); foreach ($p in "ID","UniqueID","Type","MaxUnits","StandardRate") { try { [void]$r.$p } catch { } } }
    try { [void]$proj.Calendars.Count; [void]$proj.BaseCalendars.Count } catch { }
}
function Get-UidTask($proj, $uid) { for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); if ($t.UniqueID -eq $uid) { return $t } } }

$w = "wave34-"
$specs[$w + "insert-task"] = @{ Mode = 1; Facts = "Get-TaskOpsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$proj.Tasks.Add("W34 Inserted Task") } }
$specs[$w + "delete-task"] = @{ Mode = 1; Facts = "Get-TaskOpsFacts"; Settle = 0; Edit = { param($app, $proj) (Get-UidTask $proj 2).Delete() } }
$specs[$w + "link-add"]    = @{ Mode = 1; Facts = "Get-TaskOpsFacts"; Settle = 0; Edit = { param($app, $proj) $s = Get-UidTask $proj 2; $p = Get-UidTask $proj 1; [void]$s.TaskDependencies.Add($p, 1, 0) } }
$specs[$w + "field-set"]   = @{ Mode = 1; Facts = "Get-TaskOpsFacts"; Settle = 0; Edit = { param($app, $proj) (Get-UidTask $proj 2).Text1 = "Synthetic text one."; [void](Get-UidTask $proj 2).Text1 } }
$specs[$w + "field-clear"] = @{ Mode = 1; Facts = "Get-TaskOpsFacts"; Settle = 0; Edit = { param($app, $proj) (Get-UidTask $proj 2).Text1 = ""; [void](Get-UidTask $proj 2).Text1 } }
$eacute = [string][char]0xE9; $iuml = [string][char]0xEF
$specs[$w + "note-escape"]    = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = 'Back\slash and {braces} end}' } }
$specs[$w + "note-nonascii"]  = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = "Caf${eacute} na${iuml}ve r${eacute}sum${eacute}." } }
$specs[$w + "note-multiline"] = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = "First line of the note.`r`nSecond line." } }
$specs[$w + "note-replace"]   = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = "Second note text, longer than the first." } }
$specs[$w + "note-clear"]     = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = "" } }
$specs[$w + "note-two"]       = @{ Mode = 1; Facts = "Get-NotesFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Notes = ""; $proj.Tasks(3).Notes = "Note three after the edit." } }
# round 5b fix attempt: oracle_w34_assign_counts.py made the whole chain in ONE session from the base file wave34-resource-add-third:
# Add res 2, SaveAs control; Add res 1, SaveAs edit (second); SaveAs control (third); Add res 3, SaveAs edit (third).  The first capture
# (control opened fresh, one Add) left the Assn FixedMeta/Fixed2Meta 34 / 53 bytes short of the pair, so the chain is repeated here.
function Set-W34Identity($app, $proj) { $app.UserName = "Fixture Builder"; $proj.Author = "Fixture Builder"; $proj.Manager = "Fixture Builder"; $proj.Company = "Fixture Company"; $proj.Title = "Synthetic W34 Assign Count Fixture" }
$specs[$w + "assign-add-second"] = @{ Mode = 1; Facts = "Get-AssignFacts"; Settle = 0; Open = "wave34-resource-add-third"; Edit = { param($app, $proj)
    Set-W34Identity $app $proj; $t = $proj.Tasks(1); [void]$t.Assignments.Add($t.ID, 2); Get-AssignFacts $proj
    [void]$app.FileSaveAs("C:\trace\work\w34aa-mid1.mpp"); Get-AssignFacts $proj
    [void]$t.Assignments.Add($t.ID, 1) } }
$specs[$w + "assign-add-third"]  = @{ Mode = 1; Facts = "Get-AssignFacts"; Settle = 0; Open = "wave34-resource-add-third"; Edit = { param($app, $proj)
    Set-W34Identity $app $proj; $t = $proj.Tasks(1); [void]$t.Assignments.Add($t.ID, 2); Get-AssignFacts $proj
    [void]$app.FileSaveAs("C:\trace\work\w34aa-mid1.mpp"); [void]$t.Assignments.Add($t.ID, 1); Get-AssignFacts $proj
    [void]$app.FileSaveAs("C:\trace\work\w34aa-mid2.mpp"); [void]$app.FileSaveAs("C:\trace\work\w34aa-mid3.mpp"); Get-AssignFacts $proj
    [void]$t.Assignments.Add($t.ID, 3) } }
$specs[$w + "resource-add-third"] = @{ Mode = 1; Facts = "Get-ResFacts"; Settle = 0; Edit = { param($app, $proj) [void]$proj.Resources.Add("R3") } }

# ---- group 3: wave30 (oracle_w30_*.py).  Mode: the app was in automatic mode for these oracles except the WBS pair, which
# oracle_w30_project_pairs.py ran after setting manual for Calculate (the setting sticks between runs). ----
function Get-W30Facts($proj) {
    [void]$proj.ProjectSummaryTask.ID; [void]$proj.Tasks.Count
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        foreach ($p in "ID","UniqueID","Active","Summary","Milestone","Manual","Duration","PercentComplete","Start","Finish","OutlineLevel","OutlineNumber") { try { [void]$t.$p } catch { } }
    }
}
function Get-RowFacts($proj) {      # move, task-mode: row() incl. the project summary row
    # (round 5b: written as in the untraced diagnostic r5b-diag.ps1, which ran in milliseconds; no @(COM object) wrapping)
    $props = "ID","UniqueID","Summary","Manual","Duration","DurationText","PercentComplete","Start","Finish","ConstraintType"
    $pst = $proj.ProjectSummaryTask
    foreach ($p in $props) { try { [void]$pst.$p } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in $props) { try { [void]$t.$p } catch { } } }
}
function Get-NotesW30Facts($proj) {
    foreach ($p in "Start","Finish","Duration") { try { [void]$proj.ProjectSummaryTask.$p } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","Summary","Manual","Duration","PercentComplete","Start","Finish","Notes") { try { [void]$t.$p } catch { } } }
}
function Get-TimelineFacts($proj) {
    foreach ($p in "Start","Finish","Duration") { try { [void]$proj.ProjectSummaryTask.$p } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","Summary","Manual","OutlineLevel","Duration","PercentComplete","Start","Finish") { try { [void]$t.$p } catch { } } }
    try { $tl = $proj.Timeline; foreach ($p in "Show","Visible","IsVisible","ShowTimeline","Bars","Tasks","Name") { try { [void]$tl.$p } catch { } } } catch { }
}
function Get-LevelFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","Manual","Start","Finish","Duration","LevelingDelay","Overallocated") { try { [void]$t.$p } catch { } } }
    for ($k = 1; $k -le $proj.Resources.Count; $k++) { $r = $proj.Resources($k); foreach ($p in "ID","UniqueID","Type","MaxUnits","PeakUnits","Overallocated") { try { [void]$r.$p } catch { } } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); for ($a = 1; $a -le $t.Assignments.Count; $a++) { $as = $t.Assignments($a); foreach ($p in "TaskID","ResourceID","Units","Work","Start","Finish","LevelingDelay") { try { [void]$as.$p } catch { } } } }
}
function Get-ResPairFacts($proj) {
    [void]$proj.Resources.Count; [void]$proj.Tasks.Count
    try { [void]$proj.OverallocatedResources } catch { }
    for ($k = 1; $k -le $proj.Resources.Count; $k++) { $r = $proj.Resources($k); foreach ($p in "ID","UniqueID","Type","MaxUnits","Start","Finish","Work","Overallocated","PeakUnits","Notes") { try { [void]$r.$p } catch { } } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","Summary","Manual","Start","Finish","Duration","LevelingDelay","Overallocated") { try { [void]$t.$p } catch { } } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); for ($a = 1; $a -le $t.Assignments.Count; $a++) { $as = $t.Assignments($a); foreach ($p in "TaskID","TaskUniqueID","ResourceID","ResourceUniqueID","Units","Work","Start","Finish","LevelingDelay") { try { [void]$as.$p } catch { } } } }
}
function Get-ProjPairFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","Manual","Start","Finish","Duration","PercentComplete","TotalSlack","Critical") { try { [void]$t.$p } catch { } } }
    foreach ($p in "ProjectFinish","Duration") { try { [void]$proj.$p } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { try { [void]$proj.Tasks($k).WBS } catch { } }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","BaselineStart","BaselineFinish","BaselineDuration","BaselineWork","BaselineCost") { try { [void]$t.$p } catch { } } }
}
function Select-ById($app, $proj, $id) { for ($k = 1; $k -le $proj.Tasks.Count; $k++) { if ($proj.Tasks($k).ID -eq $id) { [void]$app.SelectTaskField($proj.Tasks($k).ID, "Name", $false); return $proj.Tasks($k) } } }

$v = "wave30-"
$specs[$v + "assign-add"]    = @{ Mode = 1; Facts = "Get-ResPairFacts"; Settle = 0; Edit = { param($app, $proj) $t = $proj.Tasks(1); [void]$t.Assignments.Add($t.ID, 2) } }
$specs[$v + "assign-delete"] = @{ Mode = 1; Facts = "Get-ResPairFacts"; Settle = 0; Edit = { param($app, $proj) $t = $proj.Tasks(1); for ($a = 1; $a -le $t.Assignments.Count; $a++) { $c = $t.Assignments($a); if ($c.ResourceUniqueID -eq 2) { $c.Delete(); break } } } }
$specs[$v + "resource-add"]    = @{ Mode = 1; Facts = "Get-ResPairFacts"; Settle = 0; Edit = { param($app, $proj) [void]$proj.Resources.Add("R2") } }
$specs[$v + "resource-rename"] = @{ Mode = 1; Facts = "Get-ResPairFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Resources(2).Name = "R2b" } }
$specs[$v + "resource-notes"]  = @{ Mode = 1; Facts = "Get-ResPairFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Resources(2).Notes = "Resource note line." } }
$specs[$v + "inactivate"]        = @{ Mode = 1; Facts = "Get-W30Facts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(2).Active = $false } }
$specs[$v + "inactivate-second"] = @{ Mode = 1; MidSave = $true; Facts = "Get-W30Facts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(1).Active = $false } }
$specs[$v + "level"] = @{ Mode = 1; MidSave = $true; Ref = "wave30-level-all"; Facts = "Get-LevelFacts"; Settle = 0; Edit = { param($app, $proj) try { [void]$app.LevelNow($true) } catch { } } }
$specs[$v + "move-forward"] = @{ Mode = 1; Facts = "Get-RowFacts"; Settle = 0; Edit = { param($app, $proj) [void](Select-ById $app $proj 2); try { [void]$app.ActiveSelection.Tasks.Count } catch { }; [void]$app.TaskMove($true, $true, 1); try { $n = $app.GetUndoListCount(); for ($i = 1; $i -le $n; $i++) { [void]$app.GetUndoListItem($i) } } catch { } } }
$specs[$v + "move-back"]    = @{ Mode = 1; Facts = "Get-RowFacts"; Settle = 0; Edit = { param($app, $proj) [void](Select-ById $app $proj 2); try { [void]$app.ActiveSelection.Tasks.Count } catch { }; [void]$app.TaskMove($false, $true, 1); try { $n = $app.GetUndoListCount(); for ($i = 1; $i -le $n; $i++) { [void]$app.GetUndoListItem($i) } } catch { } } }
$specs[$v + "notes"]    = @{ Mode = 1; Facts = "Get-NotesW30Facts"; Settle = 0; Edit = { param($app, $proj) $t = Select-ById $app $proj 2; try { [void]$app.ActiveSelection.Tasks.Count } catch { }; $t.Notes = "A wave 30 notes fixture line." } }
$specs[$v + "timeline"] = @{ Mode = 1; Facts = "Get-TimelineFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.TaskOnTimeline(2, $false) } }
$specs[$v + "task-mode-auto"]   = @{ Mode = 1; Ref = "wave30-task-mode-manual"; Facts = "Get-RowFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(1).Manual = $true } }
$specs[$v + "task-mode-manual"] = @{ Mode = 1; Ref = "wave30-task-mode-auto"; Facts = "Get-RowFacts"; Settle = 0; Edit = { param($app, $proj) $proj.Tasks(1).Manual = $false } }
$rl = { param($app, $proj) $t = Select-ById $app $proj 4; try { [void]$app.ActiveSelection.Tasks.Count } catch { }; [void]$app.TaskRespectLinks(); try { [void]$app.GetUndoListCount() } catch { } }
$specs[$v + "respect-links-auto"]   = @{ Mode = 1; Ref = "wave30-respect-links-auto-after-first"; Facts = "Get-W30Facts"; Settle = 0; Edit = $rl }
$specs[$v + "respect-links-manual"] = @{ Mode = 1; Ref = "wave30-respect-links-manual-after-first"; Facts = "Get-W30Facts"; Settle = 0; Edit = $rl }
$specs[$v + "wbs"] = @{ Mode = 0; Ref = "wave30-wbs-code"; Facts = "Get-ProjPairFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.WBSCodeMaskEdit("W", 1, 0, 2, ".", $true, $true); [void]$app.WBSCodeRenumber($true) } }

# ---- group 4: wave35 view state (oracle_w35_viewstate.py), automatic ----
function Get-VsFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) { $t = $proj.Tasks($k); foreach ($p in "ID","UniqueID","OutlineLevel","Summary","Duration","Start","Finish") { try { [void]$t.$p } catch { } } }
    try { $a = $proj.Application; [void]$a.ActiveWindow.ActivePane.View.Name; [void]$a.ActiveWindow.ActivePane.View.TableName } catch { }
}
function Invoke-Named($obj, [string]$method, [string[]]$names, [object[]]$vals) {
    [void]$obj.GetType().InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $vals, $null, $null, $names)
}
$s = "wave35-viewstate-"
$M = [Type]::Missing
$specs[$s + "sort-name-desc"]      = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "Sort" @("Key1","Ascending1","Renumber","Outline") @("Name", $false, $false, $true) } }
$specs[$s + "sort-name-asc"]       = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "Sort" @("Key1","Ascending1","Renumber","Outline") @("Name", $true, $false, $true) } }
$specs[$s + "sort-duration-desc"]  = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "Sort" @("Key1","Ascending1","Renumber","Outline") @("Duration", $false, $false, $true) } }
$specs[$s + "sort-flat-name-desc"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "Sort" @("Key1","Ascending1","Renumber","Outline") @("Name", $false, $false, $false) } }
$specs[$s + "sort-two-keys"]       = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) Invoke-Named $app "Sort" @("Key1","Ascending1","Key2","Ascending2","Renumber","Outline") @("Name", $false, "Duration", $true, $false, $true) } }
$specs[$s + "table-cost"]     = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.TableApply("Cost") } }
$specs[$s + "table-schedule"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.TableApply("Schedule") } }
$specs[$s + "outline-level1"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.OutlineShowTasks(1) } }
$specs[$s + "outline-level2"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.OutlineShowTasks(2) } }
$specs[$s + "outline-all"]    = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.OutlineShowTasks(65535) } }
$specs[$s + "outline-deep-level1"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.OutlineShowTasks(1) } }
$specs[$s + "outline-deep-level2"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.OutlineShowTasks(2) } }
$specs[$s + "zoom-entire"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.ZoomTimescale($M, $true) } }
$specs[$s + "zoom-reset"]  = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.ZoomTimescale($M, $M, $M, $true) } }
$specs[$s + "zoom-custom"]     = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.ZoomTimescale("2w") } }
$specs[$s + "zoom-custom-10d"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.ZoomTimescale("10d") } }
$specs[$s + "zoom-custom-narrow"] = @{ Mode = 1; Facts = "Get-VsFacts"; Settle = 0; Edit = { param($app, $proj) [void]$app.Width; [void]$app.WindowState; try { $app.WindowState = -4143 } catch { }; try { $app.Width = 900 } catch { }; [void]$app.Width; [void]$app.ZoomTimescale("2w") } }

# ---- later groups are appended below ----

# round 5b group 5: extra spec files, if present
foreach ($g5 in "specs6-g5-w27w28.ps1","specs6-g5-w29.ps1","specs6-g5-w31.ps1") { $g5p = Join-Path $PSScriptRoot $g5; if (Test-Path $g5p) { . $g5p } }
