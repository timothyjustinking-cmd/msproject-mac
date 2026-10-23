# Drive DeepSeek (via the Claude CLI launcher) through one wave in GROUPS, a fresh
# session per group.  A group ends only when its exact marker line is committed in
# the wave's completion file; the wave ends after the CLOSE group's marker.  The
# completion file existing is NOT a finish line (wave 25 wrote it early).
#   pwsh -ExecutionPolicy Bypass -File tools\deepseek\run-wave.ps1 -Wave 26 -Groups G0,G1,G2,G3,G4,G5,G6,CLOSE
# Without -Groups the wave runs as one group named ALL (marker WAVE-<n>-COMPLETE).
param(
    [Parameter(Mandatory = $true)][int]$Wave,
    [string[]]$Groups = @("ALL"),
    [int]$MaxIterationsPerGroup = 40
)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root
$launcher = Join-Path $PSScriptRoot "deepseek-claude.cmd"
$brief = "docs/handoff/wave$Wave-brief.md"
$doneRel = "docs/handoff/wave$Wave-complete.md"
$done = Join-Path $root $doneRel
$branch = "wave$Wave"
$logdir = Join-Path $root "build\deepseek-sessions"
New-Item -ItemType Directory -Force $logdir | Out-Null
# Headless mode loads every built-in and MCP tool, and DeepSeek rejects some tool
# schemas (the Artifact tool's regex pattern returned HTTP 400 on 2026-10-23), so
# each session gets the core tools only and no MCP servers.
# --tools is variadic, so it goes LAST: anything after it would be read as a tool name.
$tools = "Bash,PowerShell,Read,Edit,Write,Glob,Grep,Agent,TodoWrite,WebFetch,WebSearch"

function Marker([string]$g) { if ($g -eq "ALL" -or $g -eq "CLOSE") { "WAVE-$Wave-COMPLETE" } else { "WAVE-$Wave-$g-COMPLETE" } }
function Has-Marker([string]$m) { (Test-Path $done) -and (Select-String -Path $done -SimpleMatch $m -Quiet) }

foreach ($g in $Groups) {
    $marker = Marker $g
    if (Has-Marker $marker) { Write-Host "=== $g already complete ($marker present); skipping ==="; continue }
    $sid = [guid]::NewGuid().ToString()
    "wave=$Wave group=$g session=$sid started=$(Get-Date -Format s)" | Add-Content (Join-Path $logdir "run-wave$Wave.session")
    $scope = if ($g -eq "ALL") { "the whole brief" } else { "ONLY group $g in section 2 of the brief (or the CLOSE section if your group is CLOSE)" }
    $first = "Read docs/START-HERE.md, then $brief, and work on branch ${branch} as the wave-$Wave coordinator for $scope.  Follow section 1 of the brief exactly: items in order, one finding id per commit, gates logged per commit, deferral only with evidence of an attempt.  When every item of your group is done or deferred with evidence, append the exact line $marker to $doneRel and commit it.  Do not stop to summarise or ask; act.  If something only Tim can resolve blocks the whole group, write BLOCKED and the reason as your last line."
    $nudge = "Continue your group ($g) of wave $Wave from where you left off.  Do not re-orient or summarise: take the next unfinished item of group $g, do the work, run the gates, commit with the finding id, and move on.  Append $marker to $doneRel and commit only when every item of group $g is done or deferred with evidence of an attempt.  If something only Tim can resolve blocks the whole group, write BLOCKED and the reason as your last line."
    $failures = 0
    for ($i = 1; $i -le $MaxIterationsPerGroup; $i++) {
        if (Has-Marker $marker) { Write-Host "`n=== $marker found; group $g done after $($i-1) iteration(s) ==="; break }
        $log = Join-Path $logdir ("run-wave{0}-{1}-{2:d2}.log" -f $Wave, $g, $i)
        Write-Host "`n=== wave $Wave group $g iteration $i / $MaxIterationsPerGroup  $(Get-Date -Format 'HH:mm:ss')  session $sid ===`n"
        if ($i -eq 1) {
            & $launcher -p $first --session-id $sid --permission-mode auto --verbose --strict-mcp-config --tools $tools 2>&1 | Tee-Object -FilePath $log
        } else {
            & $launcher -p $nudge --resume $sid --permission-mode auto --verbose --strict-mcp-config --tools $tools 2>&1 | Tee-Object -FilePath $log
        }
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            $failures++
            Write-Host "group $g iteration $i exited $code ($failures consecutive failure(s))"
            if ($failures -ge 3) { Write-Host "three consecutive failures; leaving group $g"; break }
            Start-Sleep 60
        } else { $failures = 0 }
        $tail = Get-Content $log -Tail 8 -ErrorAction SilentlyContinue
        if ($tail -match '^\s*BLOCKED') {
            Write-Host "group $g reports BLOCKED; recorded, moving to the next group"
            "wave=$Wave group=$g BLOCKED at $(Get-Date -Format s)" | Add-Content (Join-Path $logdir "run-wave$Wave.session")
            break
        }
    }
}
Write-Host "run-wave $Wave finished $(Get-Date -Format 'HH:mm:ss')"
