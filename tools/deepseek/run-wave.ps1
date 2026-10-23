# Drive DeepSeek (via the Claude CLI launcher) through one wave without manual nudges.
# deepseek-flash tends to end its turn after a planning pass; this loop re-prompts
# the SAME conversation until the wave's completion file exists, DeepSeek declares
# BLOCKED, or the iteration cap is hit.  Run from anywhere:
#   pwsh -ExecutionPolicy Bypass -File tools\deepseek\run-wave.ps1 -Wave 24
# The conversation is pinned by an explicit session id (never --continue: the most
# recent session in this project folder may be another Claude session's).
param(
    [Parameter(Mandatory = $true)][int]$Wave,
    [int]$MaxIterations = 80,
    [string]$SessionId = ""
)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root
$launcher = Join-Path $PSScriptRoot "deepseek-claude.cmd"
$brief = "docs/handoff/wave$Wave-brief.md"
$done = "docs/handoff/wave$Wave-complete.md"
$branch = "wave$Wave"
$logdir = Join-Path $root "build\deepseek-sessions"
New-Item -ItemType Directory -Force $logdir | Out-Null
$resume = $SessionId -ne ""
if (-not $resume) { $SessionId = [guid]::NewGuid().ToString() }
"wave=$Wave session=$SessionId started=$(Get-Date -Format s)" | Set-Content (Join-Path $logdir "run-wave$Wave.session")

$first = "Read docs/START-HERE.md, then $brief, and run it as the wave-$Wave coordinator on branch ${branch}: the items in order, each committed with its finding id, every gate in the brief green before each commit and recorded in the commit message, one CI dispatch per workflow on the branch at the end, no merge to main. Write $done when done. Do not stop to summarise or ask; act. If something only Tim can resolve blocks you, write BLOCKED and the reason as your last line."
$nudge = "Continue wave $Wave from where you left off. Do not re-orient or summarise: take the next unfinished item in $brief order, do the work, run the gates, commit with the finding id, and move on. Write $done only when every item is fixed or deferred with a reason. If something only Tim can resolve blocks you, write BLOCKED and the reason as your last line."
# Headless mode loads every built-in and MCP tool, and DeepSeek rejects some tool
# schemas (the Artifact tool's regex pattern returned HTTP 400 on 2026-10-23), so
# the session gets the core tools only and no MCP servers.
# --tools is variadic, so it goes LAST: anything after it would be read as a tool name.
$tools = "Bash,PowerShell,Read,Edit,Write,Glob,Grep,Agent,TodoWrite,WebFetch,WebSearch"
$failures = 0
for ($i = 1; $i -le $MaxIterations; $i++) {
    if (Test-Path (Join-Path $root $done)) { Write-Host "`n=== $done exists; wave $Wave done after $($i-1) iteration(s) ==="; break }
    $log = Join-Path $logdir ("run-wave{0}-{1:d2}.log" -f $Wave, $i)
    Write-Host "`n=== wave $Wave iteration $i / $MaxIterations  $(Get-Date -Format 'HH:mm:ss')  session $SessionId ===`n"
    if ($i -eq 1 -and -not $resume) {
        & $launcher -p $first --session-id $SessionId --permission-mode auto --verbose --strict-mcp-config --tools $tools 2>&1 | Tee-Object -FilePath $log
    } else {
        & $launcher -p $nudge --resume $SessionId --permission-mode auto --verbose --strict-mcp-config --tools $tools 2>&1 | Tee-Object -FilePath $log
    }
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        $failures++
        Write-Host "iteration $i exited $code ($failures consecutive failure(s))"
        if ($failures -ge 3) { Write-Host "three consecutive failures; stopping"; break }
        Start-Sleep 60
    } else { $failures = 0 }
    $tail = Get-Content $log -Tail 8 -ErrorAction SilentlyContinue
    if ($tail -match '^\s*BLOCKED') { Write-Host "DeepSeek reports BLOCKED; stopping for Tim"; break }
}
Write-Host "run-wave $Wave finished $(Get-Date -Format 'HH:mm:ss')"
