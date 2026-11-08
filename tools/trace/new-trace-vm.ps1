# Create the isolated trace VM (docs/handoff/trace-runbook.md section 4).  Run ONCE, from an
# ELEVATED PowerShell on the host:
#
#   pwsh -NoProfile -File D:\dev\msproject-mac\tools\trace\new-trace-vm.ps1 -Iso <path to Windows 11 x64 ISO>
#
# It creates a Generation 2 VM (TPM and Secure Boot, which Windows 11 needs), 4 vCPU,
# 4-8 GB dynamic memory and a 128 GB dynamic disk under D:\vm (never under Dropbox).  It
# attaches the ISO, connects the NAT "Default Switch" ONLY so Windows and Office can install
# and activate, disables the integration services that cross host-to-guest (guest file
# copy), and starts the VM.  It changes nothing else on the host.
param(
    [Parameter(Mandatory = $true)][string]$Iso,
    [string]$Name = "msp-trace",
    [string]$Root = "D:\vm"
)
$ErrorActionPreference = "Stop"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "run this from an elevated PowerShell (Run as administrator)"
}
if (-not (Test-Path $Iso)) { throw "ISO not found: $Iso" }
if (Get-VM -Name $Name -ErrorAction SilentlyContinue) { throw "a VM named $Name already exists" }
$switch = Get-VMSwitch -Name "Default Switch" -ErrorAction SilentlyContinue
if (-not $switch) { throw "no 'Default Switch' (Hyper-V's NAT switch); reboot once after enabling Hyper-V" }

$dir = Join-Path $Root $Name
New-Item -ItemType Directory -Force $dir | Out-Null
$vhd = Join-Path $dir "$Name.vhdx"

New-VM -Name $Name -Generation 2 -Path $Root -MemoryStartupBytes 4GB `
    -NewVHDPath $vhd -NewVHDSizeBytes 128GB -SwitchName "Default Switch" | Out-Null
Set-VMProcessor -VMName $Name -Count 4
Set-VMMemory -VMName $Name -DynamicMemoryEnabled $true -MinimumBytes 2GB -StartupBytes 4GB -MaximumBytes 8GB
Set-VMKeyProtector -VMName $Name -NewLocalKeyProtector
Enable-VMTPM -VMName $Name
Set-VMFirmware -VMName $Name -EnableSecureBoot On -SecureBootTemplate MicrosoftWindows
Add-VMDvdDrive -VMName $Name -Path $Iso
Set-VMFirmware -VMName $Name -FirstBootDevice (Get-VMDvdDrive -VMName $Name)
Disable-VMIntegrationService -VMName $Name -Name "Guest Service Interface"
Set-VM -Name $Name -CheckpointType Standard -AutomaticCheckpointsEnabled $false

Start-VM -Name $Name
Get-VM -Name $Name | Select-Object Name, State, Generation, ProcessorCount,
    @{n = "StartupGB"; e = { $_.MemoryStartup / 1GB } }, Path
"Next: vmconnect localhost $Name  (press a key at 'Press any key to boot from CD or DVD')"
