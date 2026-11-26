# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#
# One-click elevated setup / uninstall / recovery for the AirpodsBridge No-Test-Mode
# Radio Proxy Service (`AirpodsBridgeService.exe`, native x64).
#
# Automatically downloads & verifies the latest Microsoft-signed `usbip-win2` x64 release
# from GitHub if not already present, stages and starts `AirpodsBridgeService`, and writes
# its outcome to `C:\ProgramData\AirpodsBridge\SetupResult.txt` so the GUI reports exact
# progress and errors instead of closing silently.
#
# Never enables Windows Test Mode and never disables Secure Boot.

[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$RestoreOnly,
    [string]$ServiceBinaryPath,
    [string]$UsbipInstallerPath,
    [switch]$NoPauseOnError
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$ServiceName = 'AirpodsBridgeService'
$InstallRoot = Join-Path $env:ProgramFiles 'AirpodsBridge\RadioService'
$TargetBinary = Join-Path $InstallRoot 'AirpodsBridgeService.exe'
$DataDir = 'C:\ProgramData\AirpodsBridge'
$ResultFile = Join-Path $DataDir 'SetupResult.txt'
$SetupLog = Join-Path $DataDir 'Setup.log'

New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

function Write-Result([string]$Status, [string]$Message) {
    $line = "$Status|$Message"
    [IO.File]::WriteAllText($ResultFile, $line, (New-Object Text.UTF8Encoding $false))
    Add-Content -LiteralPath $SetupLog -Value "$(Get-Date -Format 's') [$Status] $Message"
}

function Write-Step([string]$Text) {
    Write-Host "==> $Text" -ForegroundColor Cyan
    Add-Content -LiteralPath $SetupLog -Value "$(Get-Date -Format 's') [STEP] $Text"
}

function Assert-Elevated64Bit {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'AirpodsBridgeService requires 64-bit Windows 10 (2004+) or Windows 11.'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator privileges are required. Re-run from an elevated PowerShell prompt.'
    }
}

function Find-ServiceBinary {
    if ($ServiceBinaryPath -and (Test-Path -LiteralPath $ServiceBinaryPath)) {
        return (Resolve-Path -LiteralPath $ServiceBinaryPath).Path
    }
    $searchPaths = @(
        (Join-Path $PSScriptRoot 'AirpodsBridgeService.exe'),
        (Join-Path $PSScriptRoot '..\AirpodsBridgeService.exe'),
        (Join-Path $PSScriptRoot '..\Build\service-x64\Binary\AirpodsBridgeService.exe'),
        $TargetBinary
    )
    foreach ($candidate in $searchPaths) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Could not locate AirpodsBridgeService.exe (x64).'
}

function Test-UsbipWin2Ready {
    # Note: usbip2_ude is a kernel driver (SERVICE_KERNEL_DRIVER), which PowerShell's
    # Get-Service does not list; inspect the SCM Services registry key directly.
    if (-not (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude')) {
        return $false
    }
    foreach ($dir in @('USBip', 'usbip-win2')) {
        if (Test-Path -LiteralPath (Join-Path $env:ProgramFiles "$dir\usbip.exe")) {
            return $true
        }
    }
    return $false
}

function Download-LatestUsbipWin2 {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Write-Step 'Querying latest signed usbip-win2 release from GitHub...'
    $headers = @{ 'User-Agent' = 'AirpodsBridge-Setup' }
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest' -Headers $headers -TimeoutSec 20
    $asset = $release.assets | Where-Object { $_.name -match '^USBip-.*-x64\.exe$' } | Select-Object -First 1
    if (-not $asset) {
        throw 'Could not find x64 installer asset in vadimgrn/usbip-win2 latest release.'
    }

    $tempDir = Join-Path $env:TEMP 'AirpodsBridge-Usbip'
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    $dest = Join-Path $tempDir $asset.name

    Write-Step "Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 1)) MB)..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $dest -Headers $headers -UseBasicParsing

    if ($asset.digest -and $asset.digest -match '^sha256:([0-9a-fA-F]{64})$') {
        $expected = $Matches[1].ToUpperInvariant()
        $actual = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -ne $expected) {
            Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
            throw "SHA-256 mismatch for $($asset.name). Expected $expected, got $actual."
        }
        Write-Host "    SHA-256 verified ($($actual.Substring(0, 16))...)." -ForegroundColor Green
    }
    return $dest
}

function Ensure-UsbipWin2 {
    if (Test-UsbipWin2Ready) {
        Write-Host '    Signed usbip-win2 driver is already installed.' -ForegroundColor Green
        return $false
    }

    $installer = $UsbipInstallerPath
    if (-not $installer -or -not (Test-Path -LiteralPath $installer)) {
        $installer = Download-LatestUsbipWin2
    }

    Write-Step 'Installing Microsoft-signed usbip-win2 (USB devices may briefly re-enumerate)...'
    $usbipLog = Join-Path $DataDir 'usbip-install.log'
    $args = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /MERGETASKS=`"!desktopicon`" /LOG=`"$usbipLog`""
    $proc = Start-Process -FilePath $installer -ArgumentList $args -Wait -PassThru
    if ($proc.ExitCode -notin 0, 3010) {
        throw "usbip-win2 setup exited with code $($proc.ExitCode). See $usbipLog."
    }

    # Wait briefly for SCM to expose usbip2_ude.
    for ($i = 0; $i -lt 15; $i++) {
        if (Test-UsbipWin2Ready) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not (Test-UsbipWin2Ready)) {
        throw 'usbip-win2 installer finished, but usbip2_ude or usbip.exe was not detected. A Windows restart may be required.'
    }

    return ($proc.ExitCode -eq 3010)
}

try {
    Write-Result 'RUNNING' 'Setup started.'
    Assert-Elevated64Bit

    if ($RestoreOnly) {
        Write-Step 'Stopping AirpodsBridgeService and restoring native Windows Bluetooth driver...'
        Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
        $exe = Find-ServiceBinary
        & $exe --restore-radio
        if ($LASTEXITCODE -ne 0) {
            throw "Driver restoration exited with code $LASTEXITCODE."
        }
        Write-Result 'OK' 'Native Windows Bluetooth driver restored.'
        Write-Host '[OK] Native Bluetooth driver restored.' -ForegroundColor Green
        exit 0
    }

    if ($Uninstall) {
        Write-Step 'Uninstalling AirpodsBridgeService and restoring native Windows Bluetooth driver...'
        $exe = $null
        if (Test-Path -LiteralPath $TargetBinary) {
            $exe = $TargetBinary
        } else {
            try { $exe = Find-ServiceBinary } catch { $exe = $null }
        }
        if ($exe) {
            & $exe --uninstall
        } else {
            Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
            sc.exe delete $ServiceName | Out-Null
        }
        if (Test-Path -LiteralPath $InstallRoot) {
            Remove-Item -LiteralPath $InstallRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Result 'OK' 'Control engine disabled and native Windows Bluetooth driver restored.'
        Write-Host '[OK] AirpodsBridgeService removed cleanly.' -ForegroundColor Green
        exit 0
    }

    $sourceExe = Find-ServiceBinary
    Write-Step 'Inspecting Bluetooth adapter compatibility...'
    $preflightRaw = (& $sourceExe --preflight) -join "`n"
    Write-Host $preflightRaw
    $preflight = $preflightRaw | ConvertFrom-Json
    if ($preflight.adapterStatus -eq 'non_usb_only') {
        throw 'Your Bluetooth adapter is connected over UART/SDIO rather than USB and cannot be proxied.'
    }
    if ($preflight.adapterStatus -eq 'none') {
        throw 'No physical USB Bluetooth adapter was detected.'
    }

    $rebootNeeded = Ensure-UsbipWin2

    Write-Step "Installing AirpodsBridgeService (x64) to $TargetBinary..."
    Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
    Copy-Item -LiteralPath $sourceExe -Destination $TargetBinary -Force

    & $TargetBinary --install
    if ($LASTEXITCODE -ne 0) {
        throw "Service registration failed with exit code $LASTEXITCODE."
    }

    Write-Step 'Starting AirpodsBridgeService...'
    Start-Service -Name $ServiceName
    Start-Sleep -Milliseconds 1200

    $svc = Get-Service -Name $ServiceName
    if ($svc.Status -ne 'Running') {
        throw "AirpodsBridgeService did not reach Running state (status: $($svc.Status))."
    }

    if ($rebootNeeded) {
        Write-Result 'REBOOT_REQUIRED' 'Control engine installed. Restart Windows once so usbip-win2 finishes initializing.'
        Write-Host '[OK] Installed. A Windows restart is recommended for usbip-win2.' -ForegroundColor Yellow
        exit 3010
    }

    Write-Result 'OK' 'No-Test-Mode Control Engine is active (WinUSB + signed usbip-win2, Secure Boot ON).'
    Write-Host '[OK] AirpodsBridgeService is running (No Test Mode, Secure Boot ON).' -ForegroundColor Green
    exit 0
} catch {
    $errMsg = $_.Exception.Message
    Write-Result 'ERROR' $errMsg
    Write-Host ''
    Write-Host "ERROR: $errMsg" -ForegroundColor Red
    Write-Host "Log: $SetupLog" -ForegroundColor DarkGray
    if (-not $NoPauseOnError) {
        Write-Host ''
        Read-Host 'Press Enter to close this window'
    }
    exit 1
}
