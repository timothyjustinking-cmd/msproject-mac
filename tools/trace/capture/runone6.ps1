param([string]$t, [switch]$MidSave)
# Capture (trace-save6.ps1) then gate one pair.  Inside the VM, elevated.
. C:\trace\cmd\specs6.ps1
$nm = Get-Names $t
$i = 'C:\trace\in\'
$rr = if (Test-Path "E:\runs") { "E:\runs" } else { "C:\trace\runs" }   # round 5b: second data disk for the recordings once C: is low
if (Test-Path "$rr\$t") { Remove-Item "$rr\$t" -Recurse -Force }
if (Test-Path "C:\trace\work\$t.mpp") { Remove-Item "C:\trace\work\$t.mpp" -Force }
try { if ($MidSave) { powershell -NoProfile -ExecutionPolicy Bypass -File C:\trace\cmd\trace-save6.ps1 -Target $t -MidSave | Out-Null } else { powershell -NoProfile -ExecutionPolicy Bypass -File C:\trace\cmd\trace-save6.ps1 -Target $t | Out-Null } } catch { Write-Host "CAPTURE FAILED $t $_" }
if (-not (Test-Path "C:\trace\work\$t.mpp")) { "$t NOSAVE" | Out-File "C:\trace\out\$t.saved.txt" -Encoding ascii; Get-Content "C:\trace\out\$t.saved.txt"; return }
$ctl = $i + $nm.Ctl + '.mpp'
$nul = if ($nm.Null) { $i + $nm.Null + '.mpp' } else { $ctl }
& C:\trace\python\python.exe C:\trace\tools\trace_gate.py "C:\trace\work\$t.mpp" ($i + $nm.Ref + '.mpp') $ctl $nul | Out-File "C:\trace\out\$t.gate.txt" -Encoding ascii
$rawMB = [math]::Round((Get-ChildItem "$rr\$t" -Filter *.run | Measure-Object Length -Sum).Sum / 1MB)
Get-ChildItem "$rr\$t" -Filter *.run | ForEach-Object { compact /c /i $_.FullName | Out-Null }   # NTFS compression, about 1.8 to 1
$h = (Get-FileHash "C:\trace\work\$t.mpp").Hash
("{0} saved {1} size {2} run MB {3}" -f $t, (($h -split '(.{8})' | ? { $_ }) -join ' '), (Get-Item "C:\trace\work\$t.mpp").Length, $rawMB) | Out-File "C:\trace\out\$t.saved.txt" -Encoding ascii
