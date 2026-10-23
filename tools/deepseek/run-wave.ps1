# Drive DeepSeek (via the Claude CLI launcher) through a wave without manual nudges.
# deepseek-flash tends to end its turn after each planning or orientation pass; this
# loop re-prompts the SAME conversation (--continue) until the wave's completion file
# exists, a blocker is declared, or the iteration cap is hit.  Run from the repo root:
#   pwsh -ExecutionPolicy Bypass -File tools\deepseek\run-wave.ps1
param(
    [string]$DoneFile = "docs\handoff\wave23-complete.md",
    [int]$MaxIterations = 80,
    [string]$Brief = "docs/handoff/wave23-brief.md"
)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root
$launcher = Join-Path $PSScriptRoot "deepseek-claude.cmd"
$first = "Read docs/START-HERE.md, then $Brief, and run it as the wave-23 coordinator on branch wave23: the W23 items in order, each committed with its finding id, every gate in section 1 green before each commit, one CI dispatch per workflow on the branch at the end, no merge to main. Write $DoneFile when done. Do not stop to summarise; act."
$nudge = "Continue the wave from where you left off. Do not re-orient or summarise: pick up the next unfinished W23 item, do the work, run the gates, commit with the finding id, and move to the next item. Write $DoneFile only when every item is fixed or deferred with a reason. If you are blocked by something only Tim can resolve, write BLOCKED and the reason as your last line."
$failures = 0
for ($i = 1; $i -le $MaxIterations; $i++) {
    if (Test-Path (Join-Path $root $DoneFile)) { Write-Host "`n=== $DoneFile exists; wave done after $($i-1) iteration(s) ==="; break }
    Write-Host "`n=== run-wave iteration $i / $MaxIterations  $(Get-Date -Format 'HH:mm:ss') ===`n"
    $prompt = if ($i -eq 1) { $first } else { $nudge }
    & $launcher -p --continue --permission-mode auto --verbose $prompt 2>&1 | Tee-Object -FilePath (Join-Path $root "build\deepseek-sessions\run-wave-$i.log")
    $code = $LASTEXITCODE
    if ($code -ne 0) { $failures++; Write-Host "iteration $i exited $code ($failures consecutive failure(s))"; if ($failures -ge 3) { Write-Host "three consecutive failures; stopping"; break }; Start-Sleep 30 } else { $failures = 0 }
    $log = Get-Content (Join-Path $root "build\deepseek-sessions\run-wave-$i.log") -Tail 5 -ErrorAction SilentlyContinue
    if ($log -match '^BLOCKED') { Write-Host "DeepSeek reports BLOCKED; stopping for Tim"; break }
}
Write-Host "run-wave finished $(Get-Date -Format 'HH:mm:ss')"
