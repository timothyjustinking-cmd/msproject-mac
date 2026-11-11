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
function Reply([string]$text, [bool]$goodSha, [int]$lenDelta) {
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '')
    if (-not $goodSha) { $sha = ('0' * 64) }
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($text + "`n#SHA " + $sha))
    $script:fake = @{ out_0000 = $b64 }
    $script:meta = "{0}|1|{1}|x" -f $script:seq, ($b64.Length + $lenDelta)
}
function Get-GuestKvp {
    $h = $script:fake.Clone()
    # the id vmx made is "HHmmss<seq>": answer with whatever id it asked for
    $h['out_meta'] = $script:lastid + $script:meta.Substring($script:meta.IndexOf('|'))
    $h
}
function Run($text, $goodSha, $lenDelta) {
    $script:seq = 0
    Reply $text $goodSha $lenDelta
    $script:lastid = "{0}{1}" -f (Get-Date -f HHmmss), 1
    try { $r = vmx 'x' 5; return @{ threw = $false; out = ($r -join "`n") } }
    catch { return @{ threw = $true; out = $_.Exception.Message } }
}
$r = Run 'hello' $true 0
Check 'host.ps1 good reply: returns the text, no throw' (-not $r.threw -and $r.out -match 'hello')
$r = Run 'hello' $false 0
Check 'host.ps1 sha MISMATCH: throws' ($r.threw -and $r.out -match 'MISMATCH')
$r = Run 'hello' $true 5
Check 'host.ps1 reply length differs from out_meta: throws' ($r.threw -and $r.out -match 'length')

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
