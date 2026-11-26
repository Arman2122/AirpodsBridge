# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

<#
.SYNOPSIS
    Test-signs and installs `BthAapBridge.sys` on a developer machine with Test Signing enabled.
    Requires an elevated (Administrator) PowerShell prompt.
#>
[CmdletBinding()]
param(
    [string]$DriverDir = ""
)

$ErrorActionPreference = "Stop"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw "Please run this script from an elevated (Administrator) PowerShell prompt."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($DriverDir)) {
    $DriverDir = Join-Path $repoRoot "Build\driver\x64"
}

$sysPath = Join-Path $DriverDir "BthAapBridge.sys"
$catPath = Join-Path $DriverDir "BthAapBridge.cat"
$infPath = Join-Path $DriverDir "BthAapBridge.inf"
if (-not (Test-Path $sysPath)) {
    throw "Driver binary not found at $sysPath. Run Build-Driver.ps1 first."
}

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) {
    throw "signtool.exe not found in Windows Kits."
}

$cert = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq "CN=AirpodsBridge Test Driver" } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=AirpodsBridge Test Driver" `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(5)
    $tmpCer = Join-Path $env:TEMP "AirpodsBridgeTestDriver.cer"
    Export-Certificate -Cert $cert -FilePath $tmpCer -Force | Out-Null
    Import-Certificate -FilePath $tmpCer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $tmpCer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    Remove-Item $tmpCer -Force
}

& $signtool sign /v /sm /s My /sha1 $cert.Thumbprint /fd SHA256 $sysPath
if (Test-Path $catPath) {
    & $signtool sign /v /sm /s My /sha1 $cert.Thumbprint /fd SHA256 $catPath
}

Write-Host "Installing BthAapBridge driver package via pnputil..."
pnputil /add-driver $infPath /install
Write-Host "Done! Make sure 'bcdedit /set testsigning on' has been enabled and rebooted once."
