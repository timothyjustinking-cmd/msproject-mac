# Round 5b group 5: wave31 pairs (oracle_w31_fill.py, oracle_w31_resource_sheet.py, oracle_w31_tables.py, oracle_w31_usage.py).
# Dot-source right after specs6.ps1 (needs $specs, Get-Names, Invoke-Named).  Additions only.
# The oracles set no calculation mode except fill (manual while building, automatic from the control on), so Mode = 1 throughout.

function G5c_Props($o, [string[]]$names) { foreach ($n in $names) { try { $null = $o.$n } catch { } } }

# Named-argument COM call that RETURNS the value (Invoke-Named in specs6.ps1 discards it).
function G5c_Call($obj, [string]$method, [string[]]$names, [object[]]$vals) {
    return $obj.GetType().InvokeMember($method, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $vals, $null, $null, $names)
}

# oracle_privacy.neutralise (all five fields), for the oracles that call it in the edit session.
function G5c_Neutralise($app, $proj, [string]$title) {
    $app.UserName = "Fixture Builder"; $proj.Author = "Fixture Builder"; $proj.Manager = "Fixture Builder"
    $proj.Company = "Fixture Company"; $proj.Title = $title
}

# ---- wave31-fill: oracle_w31_fill.py facts() (before the control save, after the fill) ----
function G5c_FillFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        G5c_Props $t @("ID","UniqueID","Manual","Start","Finish","Duration","PercentComplete","ConstraintType","ConstraintDate","Deadline","Summary","Milestone","Critical","TotalSlack","ResourceNames")
    }
    G5c_Props $proj @("ProjectStart","ProjectFinish","Duration")
}

# ---- shared by the three synthetic-base oracles ----
function G5c_ResourceRows($proj) {          # oracle_w31_resource_sheet.py resource_facts()
    for ($k = 1; $k -le $proj.Resources.Count; $k++) {
        $r = $proj.Resources($k)
        G5c_Props $r @("ID","UniqueID","Name","Initials","Type","MaterialLabel","Group","MaxUnits","Work","PeakUnits","Overallocated","StandardRate","OvertimeRate","CostPerUse","AccrueAt","BaseCalendar","Code")
    }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        for ($a = 1; $a -le $t.Assignments.Count; $a++) { G5c_Props $t.Assignments($a) @("TaskID","ResourceID","Units","Work","Start","Finish") }
    }
    [void]$proj.Resources.Count
    try { $null = $proj.OverallocatedResources } catch { }
}
function G5c_ColumnsRS($table) {            # resource_sheet table_columns(): Field, Title, Width
    try {
        $fields = $table.TableFields
        for ($j = 1; $j -le $fields.Count; $j++) { G5c_Props $fields.Item($j) @("Field","Title","Width") }
    } catch { }
}
function G5c_TableFactsRS($proj, [string]$collName) {   # resource_sheet table_facts(): Name, TableType, RowHeight, then columns
    $coll = $proj.$collName
    $count = $null; try { $count = $coll.Count } catch { }
    if ($count -isnot [int]) { return }
    for ($i = 1; $i -le $count; $i++) {
        $table = $coll.Item($i)
        G5c_Props $table @("Name","TableType","RowHeight")
        G5c_ColumnsRS $table
    }
}
function G5c_AppliedRS($proj) {             # resource_sheet applied_facts(): CurrentTable, CurrentView, Views.Count, view names
    [void]$proj.CurrentTable; [void]$proj.CurrentView
    $count = $null; try { $count = $proj.Views.Count } catch { }
    if ($count -is [int]) { for ($i = 1; $i -le $count; $i++) { try { $null = $proj.Views($i).Name } catch { } } }
}
function G5c_ResCells($app) {               # resource_sheet shown_cell(): SelectResourceField(Row=,Column=,RowRelative=False), ActiveCell Text/Field
    $cols = @("ID","Name","Type","MaterialLabel","Initials","Group","MaxUnits","StandardRate","OvertimeRate","CostPerUse","AccrueAt","BaseCalendar","Code","Work","PeakUnits","Overallocated")
    foreach ($row in 1,2,3) {
        foreach ($c in $cols) {
            try {
                Invoke-Named $app "SelectResourceField" @("Row","Column","RowRelative") @($row, $c, $false)
                try { $null = $app.ActiveCell.Text } catch { }
                try { $null = $app.ActiveCell.Field } catch { }
            } catch { }
        }
    }
}

# ---- wave31-tables: oracle_w31_tables.py table_facts() ----
function G5c_TableFactsTables($proj) {
    $count = $null; try { $count = $proj.TaskTables.Count } catch { }
    if ($count -isnot [int]) { return }
    for ($i = 1; $i -le $count; $i++) {
        $table = $proj.TaskTables($i)
        try {
            $fields = $table.TableFields
            for ($j = 1; $j -le $fields.Count; $j++) { G5c_Props $fields.Item($j) @("Field","Title","Width") }
        } catch { }
        try { $null = $table.Name } catch { }
    }
}

# ---- wave31-usage: oracle_w31_usage.py project_facts() / applied_facts() (the pre-save reads only) ----
function G5c_UsageFacts($proj) {
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        G5c_Props $proj.Tasks($k) @("ID","UniqueID","Name","Manual","Start","Finish","Duration","Work","ActualWork","RemainingWork","PercentComplete","Cost")
    }
    for ($k = 1; $k -le $proj.Resources.Count; $k++) {
        G5c_Props $proj.Resources($k) @("ID","UniqueID","Name","Type","Work","ActualWork","Cost","StandardRate","OvertimeRate","CostPerUse","AccrueAt")
    }
    for ($k = 1; $k -le $proj.Tasks.Count; $k++) {
        $t = $proj.Tasks($k)
        for ($a = 1; $a -le $t.Assignments.Count; $a++) { G5c_Props $t.Assignments($a) @("UniqueID","TaskID","ResourceID","Units","Work","ActualWork","RemainingWork","Cost","Start","Finish") }
    }
    [void]$proj.Tasks.Count; [void]$proj.Resources.Count
}
function G5c_UsageApplied($proj) {
    [void]$proj.CurrentView; [void]$proj.CurrentTable
    try { $null = $proj.Views.Count } catch { }
    for ($i = 1; $i -le $proj.TaskTables.Count; $i++) { try { $null = $proj.TaskTables($i).Name } catch { } }
    for ($i = 1; $i -le $proj.ResourceTables.Count; $i++) { try { $null = $proj.ResourceTables($i).Name } catch { } }
}
# the shared base-file clearing both resource oracles do after neutralise: tasks 1-3 automatic, delete placeholder assignments and resources
function G5c_ClearBase($proj) {
    foreach ($id in 1,2,3) { $proj.Tasks($id).Manual = $false }
    foreach ($id in 1,2,3) { $t = $proj.Tasks($id); while ($t.Assignments.Count -gt 0) { $t.Assignments(1).Delete() } }
    while ($proj.Resources.Count -gt 0) { $proj.Resources(1).Delete() }
}

# =====================================================================================================
# wave31-fill: the oracle made seed -> reopen -> control (SaveAs) -> fill -> edit in one session; the committed control is that
# control, so open it, mid-save (as the oracle saved the control), then repeat the fill.  Facts read before the mid-save and after.
# Only UserName is set (the file already carries Author/Manager/Company/Title from the seed session).
$specs["wave31-fill"] = @{ Mode = 1; MidSave = $true; Facts = "G5c_FillFacts"; Settle = 0; Edit = { param($app, $proj)
    $app.UserName = "Fixture Builder"
    [void]$app.SelectTaskField(1, "Start", $false, 0, 1)
    try { $null = $app.ActiveSelection.Tasks.Count; try { $null = $app.ActiveSelection.FieldID } catch { } } catch { }
    [void]$app.FillDown($true) } }

# wave31-resource-sheet: oracle_w31_resource_sheet.py main(), replayed from synthetic-base.mpp.  No Facts function: the oracle reads
# inside the chain and saves right after the cell reads, so every read is in Edit (Facts = none).
$specs["wave31-resource-sheet"] = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "synthetic-base"; Null = $null; Edit = { param($app, $proj)
    G5c_Neutralise $app $proj "Synthetic Resource Sheet Fixture"
    G5c_ResourceRows $proj                                   # base_resources
    G5c_ClearBase $proj
    G5c_ResourceRows $proj                                   # cleared
    $crewA = $proj.Resources.Add("Crew A"); $crewB = $proj.Resources.Add("Crew B"); $crewC = $proj.Resources.Add("Crew C")
    [void]$crewA.ID; [void]$crewB.ID; [void]$crewC.ID
    $crewA.Group = "North"; $crewA.Code = "CR-A"
    $crewB.Group = "South"; $crewB.Code = "CR-B"
    $crewC.Group = "North"; $crewC.Code = "CR-C"
    $crewB.StandardRate = '$50.00/hr'; $crewB.OvertimeRate = '$75.00/hr'; $crewB.CostPerUse = '$10.00'
    $crewB.AccrueAt = 1; $crewC.AccrueAt = 2
    G5c_ResourceRows $proj                                   # resources_after_add
    foreach ($id in 1,2,3) { $t = $proj.Tasks($id); [void]$crewA.ID; [void](G5c_Call $t.Assignments "Add" @("TaskID","ResourceID") @($t.ID, $crewA.ID)) }
    $t = $proj.Tasks(1); [void]$crewB.ID; [void](G5c_Call $t.Assignments "Add" @("TaskID","ResourceID") @($t.ID, $crewB.ID))
    foreach ($id in 2,3) {
        $t = $proj.Tasks($id); [void]$crewC.ID
        $asg = G5c_Call $t.Assignments "Add" @("TaskID","ResourceID") @($t.ID, $crewC.ID)
        $asg.Units = 0.5
    }
    [void]$app.CalculateProject()
    G5c_ResourceRows $proj                                   # resources_after_assign
    for ($i = 1; $i -le $proj.Tasks.Count; $i++) { G5c_Props $proj.Tasks($i) @("ID","UniqueID","Manual","Start","Finish","Duration") }   # tasks_after_assign
    [void]$app.ViewApply("Resource Sheet")
    [void]$app.TableApply("Entry")
    G5c_AppliedRS $proj                                      # applied_before_save
    G5c_TableFactsRS $proj "ResourceTables"; G5c_TableFactsRS $proj "TaskTables"   # tables_before_save
    G5c_ResCells $app                                        # cells_before_save
} }

# wave31-tables: oracle_w31_tables.py main(), replayed from synthetic-base.mpp.
$specs["wave31-tables"] = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "synthetic-base"; Null = $null; Edit = { param($app, $proj)
    G5c_Neutralise $app $proj "Synthetic Tables Fixture"
    G5c_TableFactsTables $proj                               # before
    Invoke-Named $app "TableEdit" @("Name","TaskTable","Create","OverwriteExisting","NewName","FieldName","NewFieldName","Title","Width","ColumnPosition") @("Entry", $true, $true, $true, "Review", "Number1", "Number1", "Number1", 12, 0)
    Invoke-Named $app "TableEdit" @("Name","TaskTable","Create","OverwriteExisting","FieldName","NewFieldName","Title","Width","ColumnPosition") @("Review", $true, $false, $true, "Text1", "Text1", "Text1", 12, 0)
    [void]$app.TableApply("Entry")
    G5c_TableFactsTables $proj                               # after_edits
    [void]$proj.CurrentTable
} }

# wave31-usage: oracle_w31_usage.py main(), replayed from synthetic-base.mpp (reads after the save, in the reopened file, are not replayed).
$specs["wave31-usage"] = @{ Mode = 1; Facts = "none"; Settle = 0; Open = "synthetic-base"; Ctl = "synthetic-base"; Null = $null; Edit = { param($app, $proj)
    G5c_Neutralise $app $proj "Synthetic Usage Fixture"
    G5c_UsageFacts $proj                                     # base
    G5c_ClearBase $proj
    G5c_UsageFacts $proj                                     # cleared
    $crewA = $proj.Resources.Add("Crew A"); $crewB = $proj.Resources.Add("Crew B"); $crewC = $proj.Resources.Add("Crew C")
    $crewA.StandardRate = '$50.00/hr'; $crewB.StandardRate = '$60.00/hr'; $crewC.StandardRate = '$40.00/hr'
    [void]$crewA.ID; [void]$crewB.ID; [void]$crewC.ID
    # pairs in the oracle's order: (1,A) (1,B) (2,A) (2,C) (3,A) (3,C); resource ID read at each Add as prop(resource, "ID") does
    $pairTask = 1, 1, 2, 2, 3, 3; $pairRes = "A", "B", "A", "C", "A", "C"
    for ($q = 0; $q -lt 6; $q++) {
        $t = $proj.Tasks($pairTask[$q])
        if ($pairRes[$q] -eq "A") { $rid = $crewA.ID } elseif ($pairRes[$q] -eq "B") { $rid = $crewB.ID } else { $rid = $crewC.ID }
        [void](G5c_Call $t.Assignments "Add" @("TaskID","ResourceID") @($t.ID, $rid))
    }
    [void]$app.CalculateProject()
    $proj.Tasks(1).PercentComplete = 100
    [void]$app.CalculateProject()
    G5c_UsageFacts $proj                                     # after_assign
    [void]$app.ViewApply("Task Usage")
    [void]$app.DetailStylesRemoveAll()
    [void]$app.DetailStylesAdd(0)
    [void]$app.DetailStylesAdd(4)
    [void]$app.ViewApply("Resource Usage")
    [void]$app.DetailStylesRemoveAll()
    [void]$app.DetailStylesAdd(0)
    [void]$app.DetailStylesAdd(1)
    G5c_UsageApplied $proj                                   # applied_before_save
} }
