param([string]$Host1 = (Join-Path $PSScriptRoot 'kvp\host.ps1'), [string]$Skip = (Join-Path $PSScriptRoot 'capture\r5b-skip.ps1'))
# Guards for W36-0.5 (A-11).  Runs on the host with no VM: the Hyper-V calls are replaced by functions.
#   kvp/host.ps1          vmx throws on a sha MISMATCH and on a reply whose length is not the announced one
#   capture/r5b-skip.ps1  "Skip for now" is invoked only in a dialog whose title starts "Sign in"
# Exit 0 all pass, 1 a case failed.
$ErrorActionPreference = 'Stop'
$fail = 0
function Check($label, $ok) { if ($ok) { "PASS $label" } else { "FAIL $label"; $script:fail++ } }

# ---- host.ps1: dot-source with the CIM calls stubbed, then stub the KVP channel ----
# W36c-0.1 (R36bCI-1): a Linux or macOS runner has no TEMP.  The test runs without it on every host,
# so host.ps1 must take its temp folder from [IO.Path]::GetTempPath(), never from $env:TEMP.
Remove-Item Env:TEMP -ErrorAction SilentlyContinue
function Get-CimInstance { $null }
. $Host1
Check 'host.ps1 loads with TEMP unset and has a temp folder' ($null -eq $env:TEMP -and $script:tmp -and (Test-Path -LiteralPath $script:tmp))
# W36c-0.2 (R36bCI-2): keep what vmx writes ("<id>|<code>" in `cmd`); the fake answers with that id, never
# with one the test guessed from the clock.
$script:sent = @{}
function Set-Kvp($n, $d) { $script:sent[$n] = $d; 0 }
# The reply the real guest agent (kvp/agent.ps1) sends, W36-0b.6 (R36G0-7).  The code vmx sends
# returns the base64 text; the agent runs it through `Out-String -Width 300`, which ends the text
# with a newline (CRLF in Windows PowerShell), announces `$o.Length` including that newline, and
# stores `$o` in 900-character pieces (`out_0000`, `out_0001`, ...).  The host reads each piece
# back out of the CIM `GuestExchangeItems` XML through `[xml]`: a literal CR is normalised away
# (and a piece of nothing but whitespace is dropped) unless the XML carries it as `&#13;`.
function Through-Xml([string]$piece, [bool]$keepCr) {
    $esc = [Security.SecurityElement]::Escape($piece)
    if ($keepCr) { $esc = $esc.Replace("`r", '&#13;') }
    $x = [xml]("<INSTANCE><PROPERTY NAME=`"Data`"><VALUE>$esc</VALUE></PROPERTY></INSTANCE>")
    [string]$x.INSTANCE.PROPERTY.VALUE
}
# Wrapping (W36-0c.7 / R36G0b-9): `Out-String -Width 300` does not wrap a plain string.  Run on the
# host, in PowerShell 7.7.0 and in Windows PowerShell 5.1.26100.9818 alike, a 1000-character string
# and a 2000-character base64 line each come back whole with only the closing CRLF added
# (build/w36/wrap_probe.ps1).  The model therefore adds no newline inside a reply of any length,
# and the cases below send replies of 296, 320, 896 and about 4000 characters.  The VM has not
# confirmed it.
# `$raw` is a reply sent as it is (the agent's catch path: "ERR $_", no Out-String newline).
function Reply([string]$text, [bool]$goodSha, [int]$lenDelta, [string]$nl = "`r`n", [bool]$keepCr = $false, [int]$dropPiece = -1, [string]$raw = $null) {
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '')
    if (-not $goodSha) { $sha = ('0' * 64) }
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($text + "`n#SHA " + $sha))
    $o = $b64 + $nl
    if ($raw) { $o = $raw }
    $n = [int][math]::Ceiling($o.Length / 900.0)
    $script:fake = @{}
    for ($k = 0; $k -lt $n; $k++) {
        if ($k -eq $dropPiece) { continue }
        $script:fake[('out_{0:d4}' -f $k)] = Through-Xml $o.Substring($k * 900, [math]::Min(900, $o.Length - $k * 900)) $keepCr
    }
    $script:meta = "{0}|{1}|{2}|x" -f $script:seq, $n, ($o.Length + $lenDelta)
}
# W36c-0.2 (R36bCI-2): `$script:tickClock` makes every Get-Date call answer one second later than the one
# before, a clock boundary between any two calls (a slow runner crosses one between Run and vmx).
$script:tickCount = 0; $script:tickClock = $false
function Get-Date {
    [CmdletBinding()] param([Alias('f')][string]$Format)
    if (-not $script:tickClock) { return Microsoft.PowerShell.Utility\Get-Date @PSBoundParameters }
    $script:tickCount++
    $d = [datetime]::Now.AddSeconds($script:tickCount)
    if ($Format) { $d.ToString($Format) } else { $d }
}
function Get-GuestKvp {
    $h = $script:fake.Clone()
    # the id vmx made is "HHmmss<seq>", the text before the first '|' of what it wrote to `cmd`
    $id = ([string]$script:sent['cmd']).Split('|')[0]
    $h['out_meta'] = $id + $script:meta.Substring($script:meta.IndexOf('|'))
    $h
}
function Run($text, $goodSha, $lenDelta, $nl = "`r`n", $keepCr = $false, $dropPiece = -1, $raw = $null) {
    $script:seq = 0
    Reply $text $goodSha $lenDelta $nl $keepCr $dropPiece $raw
    $script:sent = @{}
    try { $r = vmx 'x' 5; return @{ threw = $false; out = ($r -join "`n") } }
    catch { return @{ threw = $true; out = $_.Exception.Message } }
}
$r = Run 'hello' $true 0
Check 'host.ps1 good reply (CRLF dropped by the XML parse): returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
$script:tickClock = $true
$r = Run 'hello' $true 0
$script:tickClock = $false
Check 'host.ps1 good reply with the clock a second on between Run and vmx (forced boundary): returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
$r = Run 'hello' $true 0 "`r`n" $true
Check 'host.ps1 good reply (CR kept as &#13;): returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
$r = Run 'hello' $true 0 "`n"
Check 'host.ps1 good reply (LF newline): returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
$long = 'x' * 3000
$r = Run $long $true 0
Check 'host.ps1 good reply of four pieces: returns the whole text' (-not $r.threw -and $r.out -match ('x{3000}'))
# the newline alone in the last piece: base64 length + 2 is 1 or 2 more than a multiple of 900
foreach ($want in 1, 2) {
    $nl = "`n" * $want -replace "`n`n", "`r`n"      # 1: LF; 2: CRLF
    $hit = $null
    foreach ($len in 1200..1400) {
        $t = 'y' * $len
        $b = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($t + "`n#SHA " + ('0' * 64))).Length
        if ((($b + $nl.Length) % 900) -eq $want) { $hit = $t; break }
    }
    $r = Run $hit $true 0 $nl
    Check "host.ps1 good reply whose last piece is $want character(s) of newline: returns the text" ($hit -and -not $r.threw -and $r.out -match 'y{1000}')
}
$r = Run 'hello' $false 0
Check 'host.ps1 sha MISMATCH: throws' ($r.threw -and $r.out -match 'MISMATCH')
$r = Run 'hello' $true 5
Check 'host.ps1 reply length differs from out_meta (announced 5 more): throws' ($r.threw -and $r.out -match 'length')
$r = Run 'hello' $true -3
Check 'host.ps1 reply length differs from out_meta (announced 3 fewer): throws' ($r.threw -and $r.out -match 'length')
$r = Run 'hello' $true 0 "" $false
Check 'host.ps1 reply with no newline at all (not the Out-String shape): throws' ($r.threw -and $r.out -match 'length')
$r = Run $long $true 0 "`r`n" $false 1
Check 'host.ps1 reply with a piece missing: throws' ($r.threw -and $r.out -match 'length')
# a reply over the 300-character width is one unwrapped line plus the closing newline
foreach ($len in 150, 170, 600) {
    $t = 'w' * $len
    $b = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($t + "`n#SHA " + ('0' * 64))).Length
    $r = Run $t $true 0
    Check "host.ps1 good reply of $b characters ($(if ($b -gt 300) { 'over' } else { 'under' }) the 300 width): returns the text" (-not $r.threw -and $r.out -match "w{$len}")
}
# the agent's own error: "ERR <message>", announced at its own length, no newline
$r = Run 'unused' $true 0 "" $false -1 'ERR Missing closing ''}'' in statement block.'
Check 'host.ps1 agent ERR reply: throws the agent error text, not a length error' ($r.threw -and $r.out -match 'guest agent error: Missing closing' -and $r.out -notmatch 'does not match')
$r = Run 'unused' $true 0 "" $false -1 ('ERR ' + ('e' * 1500))
Check 'host.ps1 agent ERR reply of two pieces: throws the agent error text' ($r.threw -and $r.out -match 'guest agent error: e{1500}')

# ---- r5b-skip.ps1: the Invoke() sits under a Sign in title test (AST) ----
$tokens = $null; $errs = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Skip, [ref]$tokens, [ref]$errs)
Check 'r5b-skip.ps1 parses' ($errs.Count -eq 0)
$inv = $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.InvokeMemberExpressionAst] -and $n.Member.Value -eq 'Invoke' }, $true)
Check 'r5b-skip.ps1 has exactly one Invoke()' ($inv.Count -eq 1)
$guard = $false
if ($inv.Count -eq 1) {
    $p = $inv[0].Parent
    while ($p) {
        if ($p -is [System.Management.Automation.Language.IfStatementAst]) {
            foreach ($c in $p.Clauses) { if ($c.Item2.Extent.Text.Contains($inv[0].Extent.Text) -and $c.Item1.Extent.Text -match "StartsWith\('Sign in'\)") { $guard = $true } }
        }
        $p = $p.Parent
    }
}
Check 'r5b-skip.ps1 Invoke() is under a title StartsWith(Sign in) test' $guard
if ($fail) { "$fail failed"; exit 1 }
'all passed'
exit 0
