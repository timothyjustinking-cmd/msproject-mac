# dot-source on the host: vmx "<code>" runs code in the guest agent and prints output (sha-checked)
$script:vm = Get-CimInstance -Namespace root\virtualization\v2 -ClassName Msvm_ComputerSystem -Filter "ElementName='msp-trace'"
$script:svc = Get-CimInstance -Namespace root\virtualization\v2 -ClassName Msvm_VirtualSystemManagementService
function Set-Kvp($n,$d){
  $cls = Get-CimClass -Namespace root/virtualization/v2 -ClassName Msvm_KvpExchangeDataItem
  $it = New-CimInstance -CimClass $cls -ClientOnly -Property @{Name=$n;Data=$d;Source=[uint16]0}
  $s = [Microsoft.Management.Infrastructure.Serialization.CimSerializer]::Create()
  $b = $s.Serialize($it,[Microsoft.Management.Infrastructure.Serialization.InstanceSerializationOptions]::None)
  $x = [Text.Encoding]::Unicode.GetString($b)
  Invoke-CimMethod -InputObject $script:svc -MethodName RemoveKvpItems -Arguments @{TargetSystem=$script:vm;DataItems=@($x)} | Out-Null
  $r = Invoke-CimMethod -InputObject $script:svc -MethodName AddKvpItems -Arguments @{TargetSystem=$script:vm;DataItems=@($x)}
  $r.ReturnValue
}
function Get-GuestKvp {
  $c = Get-CimInstance -Namespace root\virtualization\v2 -ClassName Msvm_KvpExchangeComponent -Filter "SystemName='$($script:vm.Name)'"
  $h=@{}
  foreach ($x in $c.GuestExchangeItems) { $d=([xml]$x).INSTANCE.PROPERTY; $n=($d|?{$_.NAME -eq 'Name'}).VALUE; $v=($d|?{$_.NAME -eq 'Data'}).VALUE; $h[$n]=$v }
  $h
}
$script:seq = 0
function vmx([string]$code,[int]$timeout=120){
  $script:seq++; $id = "{0}{1}" -f (Get-Date -f HHmmss),$script:seq
  $w = '$__o = ((& {' + $code + '} 2>&1 | Out-String -Width 300) -replace "\r","").TrimEnd(); $__h = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($__o))).Replace("-",""); [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($__o + "`n#SHA " + $__h))'; Set-Kvp 'cmd' "$id|$w" | Out-Null
  $t0=Get-Date
  while (((Get-Date)-$t0).TotalSeconds -lt $timeout) {
    Start-Sleep -Milliseconds 1500
    $h = Get-GuestKvp
    if ($h['out_meta'] -and $h['out_meta'].StartsWith("$id|")) {
      $m = $h['out_meta'].Split('|'); $o=''
      for ($k=0;$k -lt [int]$m[1];$k++){ $o += $h[('out_{0:d4}' -f $k)] }
      $o=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($o)); $k=$o.LastIndexOf("`n#SHA "); $gh=$o.Substring($k+6); $o=$o.Substring(0,$k); $hh=[BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($o))).Replace("-",""); "[len $($o.Length) sha " + $(if($hh -eq $gh){"OK"}else{"MISMATCH"}) + " $($hh.Substring(0,12))]"
      return $o
    }
  }
  "TIMEOUT"
}




$script:tmp = Join-Path $env:TEMP 'trace-kvp'; New-Item -ItemType Directory -Force $script:tmp | Out-Null
function cdbq([string]$target,[string[]]$cmds,[string]$name,[int]$timeout=900,[string]$filter='.'){
  $cf = Join-Path $script:tmp "$name.txt"
  (@(".scriptload C:\trace\cmd\chain.js", ".logopen /u C:\trace\out\$name.log") + $cmds + @(".logclose","q")) | Set-Content $cf -Encoding ascii
  Copy-VMFile msp-trace -SourcePath $cf -DestinationPath "C:\trace\cmd\$name.txt" -FileSource Host -CreateFullPath -Force
  vmx ('Remove-Item C:\trace\out\'+$name+'.log -ErrorAction SilentlyContinue; & C:\trace\dbg\cdb.exe -z C:\trace\runs\'+$target+'\WINPROJ01.run -y C:\trace\sym -cf C:\trace\cmd\'+$name+'.txt | Out-Null; Get-Content C:\trace\out\'+$name+'.log | ? { $_ -match "'+$filter+'" }') $timeout
}
