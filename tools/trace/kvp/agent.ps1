# guest agent: polls host->guest KVP item "cmd" (format id|code), runs it, returns text via guest KVP out_*
$ext='HKLM:\SOFTWARE\Microsoft\Virtual Machine\External'; $g='HKLM:\SOFTWARE\Microsoft\Virtual Machine\Guest'
$last=''
Set-ItemProperty $g -Name agent -Value ("up " + (Get-Date -f s)) -ErrorAction SilentlyContinue
while ($true) {
  $c = $null; try { $c = (Get-ItemProperty $ext -ErrorAction Stop).cmd } catch {}
  if ($c -and $c -ne $last) {
    $last = $c
    $i = $c.IndexOf('|'); $id = $c.Substring(0,$i); $code = $c.Substring($i+1)
    $o = ''
    try { $o = (& ([scriptblock]::Create($code)) 2>&1 | Out-String -Width 300) } catch { $o = "ERR $_" }
    $p = Get-ItemProperty $g; foreach ($n in $p.PSObject.Properties.Name) { if ($n -like 'out_*') { Remove-ItemProperty $g -Name $n -ErrorAction SilentlyContinue } }
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($o))).Replace('-','')
    $n = [math]::Ceiling($o.Length/900.0)
    for ($k=0; $k -lt $n; $k++) { Set-ItemProperty $g -Name ('out_{0:d4}' -f $k) -Value $o.Substring($k*900,[math]::Min(900,$o.Length-$k*900)) }
    Set-ItemProperty $g -Name out_meta -Value "$id|$n|$($o.Length)|$sha"
  }
  Start-Sleep -Milliseconds 700
}
