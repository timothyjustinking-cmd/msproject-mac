# Run inside the VM (no recorder): open the baseline control, optionally edit, save, print the Task FixedData date words.
# drift.ps1 -Mode <auto-null|man-null|auto-base|auto-base-wait> [-Fixture wave30-baseline-control] [-Settle 0]
param([string]$Mode, [string]$Fixture = 'wave30-baseline-control', [int]$Settle = 0, [string]$Tag = '')
$ErrorActionPreference = 'Stop'
$src = "C:\trace\in\$Fixture.mpp"; $w = 'C:\trace\work'
$copy = "$w\drift-$Mode$Tag-in.mpp"; $out = "$w\drift-$Mode$Tag.mpp"
Copy-Item $src $copy -Force
$app = New-Object -ComObject MSProject.Application
try {
    $app.Visible = $true; $app.DisplayAlerts = $false; $app.UserName = 'trace'
    if ($Mode -like 'man*') { $app.Calculation = 0 } else { $app.Calculation = 1 }
    [void]$app.FileOpenEx($copy)
    if ($Mode -like '*base*') { [void]$app.BaselineSave($true, 0, 0) }
    if ($Settle -gt 0) { Start-Sleep -Seconds $Settle }
    [void]$app.FileSaveAs($out)
    [void]$app.FileCloseEx(0)
} finally { $app.Quit(0); [void][Runtime.InteropServices.Marshal]::ReleaseComObject($app) }
$py = 'C:\trace\work\drift_show.py'
& C:\trace\python\python.exe $py $out $src
