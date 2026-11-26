# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

<#
.SYNOPSIS
    Switches Windows Bluetooth A2DP latency profile for AirPods between:
      - LowLatency   : Forces SBC codec (BluetoothAacEnable=0, -70..100 ms), disables
                       Audio Enhancements APO on the AirPods A2DP endpoint (-10..30 ms),
                       and disables USB Bluetooth adapter power-saving suspend.
      - MusicQuality : Restores AAC codec (BluetoothAacEnable=1), default Audio Enhancements,
                       and default USB Bluetooth power management.

    Applies immediately without rebooting by re-negotiating the AirPods A2DP PnP node.
    Requires an elevated (Administrator) PowerShell session.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("LowLatency", "MusicQuality")]
    [string]$Mode,

    [string]$EndpointGuid = ""
)

$ErrorActionPreference = "Stop"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw "Administrator privileges are required to modify BthA2dp kernel parameters."
}

$lowLatency = ($Mode -eq "LowLatency")

# 1. Switch Microsoft BthA2dp.sys codec preference: 0 = SBC (low latency), 1 = AAC (default).
$a2dpReg = "HKLM:\SYSTEM\CurrentControlSet\Services\BthA2dp\Parameters"
if (-not (Test-Path $a2dpReg)) {
    New-Item -Path $a2dpReg -Force | Out-Null
}
$aacValue = if ($lowLatency) { 0 } else { 1 }
New-ItemProperty -Path $a2dpReg -Name "BluetoothAacEnable" -PropertyType DWord -Value $aacValue -Force | Out-Null
Write-Host "Set BthA2dp BluetoothAacEnable = $aacValue ($Mode)."

# 2. Disable or restore SysFx Audio Enhancements APO on the AirPods A2DP render endpoint(s).
$mmRoot = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
$disableSysFx = if ($lowLatency) { 1 } else { 0 }
if (Test-Path $mmRoot) {
    $targets = @()
    if (-not [string]::IsNullOrWhiteSpace($EndpointGuid)) {
        $norm = $EndpointGuid.Trim()
        if (-not $norm.StartsWith("{")) { $norm = "{$norm}" }
        $candidate = Join-Path $mmRoot $norm
        if (Test-Path $candidate) { $targets += (Get-Item $candidate) }
    }
    if ($targets.Count -eq 0) {
        Get-ChildItem $mmRoot | ForEach-Object {
            $props = Get-ItemProperty (Join-Path $_.PSPath "Properties") -ErrorAction SilentlyContinue
            if ($props -and ($props.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6' -match 'AirPods')) {
                $targets += $_
            }
        }
    }

    foreach ($ep in $targets) {
        $fxPath = Join-Path $ep.PSPath "FxProperties"
        if (-not (Test-Path $fxPath)) {
            New-Item -Path $fxPath -Force | Out-Null
        }
        # PKEY_AudioEndpoint_Disable_SysFx = {1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5
        New-ItemProperty -Path $fxPath -Name "{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5" `
            -PropertyType DWord -Value $disableSysFx -Force | Out-Null
        Write-Host "Updated Disable_SysFx = $disableSysFx on $($ep.PSChildName)."
    }
}

# 3. Disable or restore USB Bluetooth radio power-saving suspend via official WMI power policy.
try {
    $usbRadios = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -like 'USB\*' }
    foreach ($radio in $usbRadios) {
        $escapedId = $radio.InstanceId.Replace('\', '\\')
        $pwr = Get-CimInstance -Namespace root\wmi -ClassName MSPower_DeviceEnable -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceName -like "$($radio.InstanceId)*" }
        foreach ($entry in $pwr) {
            Set-CimInstance -InputObject $entry -Property @{ Enable = (-not $lowLatency) } -ErrorAction SilentlyContinue | Out-Null
            Write-Host "Set USB Bluetooth power-saving Enable = $(-not $lowLatency) on $($radio.FriendlyName)."
        }
    }
}
catch {
    Write-Host "Skipped USB Bluetooth power policy (optional): $($_.Exception.Message)"
}

# 4. Re-negotiate the AirPods A2DP PnP node so the new codec and APO state take effect
#    immediately without rebooting Windows.
$a2dpNodes = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
    $_.InstanceId -match '0000110B' -and $_.FriendlyName -match 'AirPods'
}
foreach ($node in $a2dpNodes) {
    Write-Host "Re-negotiating A2DP link on $($node.FriendlyName)..."
    Disable-PnpDevice -InstanceId $node.InstanceId -Confirm:$false -ErrorAction SilentlyContinue | Out-Null
    Start-Sleep -Milliseconds 600
    Enable-PnpDevice -InstanceId $node.InstanceId -Confirm:$false -ErrorAction SilentlyContinue | Out-Null
}

Write-Host "Done! AirPods latency profile switched to $Mode."
