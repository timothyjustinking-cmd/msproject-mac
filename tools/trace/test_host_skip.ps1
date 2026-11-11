param([string]$Host1 = (Join-Path $PSScriptRoot 'kvp\host.ps1'), [string]$Skip = (Join-Path $PSScriptRoot 'capture\r5b-skip.ps1'))
# Guards for W36-0.5 (A-11).  Runs on the host with no VM: the Hyper-V calls are replaced by functions.
#   kvp/host.ps1          vmx throws on a sha MISMATCH and on a reply whose length is not the announced one
#   capture/r5b-skip.ps1  "Skip for now" is invoked only in a dialog whose title starts "Sign in"
# Exit 0 all pass, 1 a case failed.
$ErrorActionPreference = 'Stop'
$fail = 0
function Check($label, $ok) { if ($ok) { "PASS $label" } else { "FAIL $label"; $script:fail++ } }

# ---- host.ps1: dot-source with the CIM calls stubbed, then stub the KVP channel ----
function Get-CimInstance { $null }
. $Host1
function Set-Kvp($n, $d) { 0 }
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
function Reply([string]$text, [bool]$goodSha, [int]$lenDelta, [string]$nl = "`r`n", [bool]$keepCr = $false, [int]$dropPiece = -1) {
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '')
    if (-not $goodSha) { $sha = ('0' * 64) }
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($text + "`n#SHA " + $sha))
    $o = $b64 + $nl
    $n = [int][math]::Ceiling($o.Length / 900.0)
    $script:fake = @{}
    for ($k = 0; $k -lt $n; $k++) {
        if ($k -eq $dropPiece) { continue }
        $script:fake[('out_{0:d4}' -f $k)] = Through-Xml $o.Substring($k * 900, [math]::Min(900, $o.Length - $k * 900)) $keepCr
    }
    $script:meta = "{0}|{1}|{2}|x" -f $script:seq, $n, ($o.Length + $lenDelta)
}
function Get-GuestKvp {
    $h = $script:fake.Clone()
    # the id vmx made is "HHmmss<seq>": answer with whatever id it asked for
    $h['out_meta'] = $script:lastid + $script:meta.Substring($script:meta.IndexOf('|'))
    $h
}
function Run($text, $goodSha, $lenDelta, $nl = "`r`n", $keepCr = $false, $dropPiece = -1) {
    $script:seq = 0
    Reply $text $goodSha $lenDelta $nl $keepCr $dropPiece
    $script:lastid = "{0}{1}" -f (Get-Date -f HHmmss), 1
    try { $r = vmx 'x' 5; return @{ threw = $false; out = ($r -join "`n") } }
    catch { return @{ threw = $true; out = $_.Exception.Message } }
}
$r = Run 'hello' $true 0
Check 'host.ps1 good reply (CRLF dropped by the XML parse): returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
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
