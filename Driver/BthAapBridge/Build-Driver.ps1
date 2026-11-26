# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

<#
.SYNOPSIS
    Builds the x64 KMDF Bluetooth L2CAP profile driver (`BthAapBridge.sys`) using the official
    Microsoft WDK NuGet package (no GUI WDK installer or administrator rights required).
#>
[CmdletBinding()]
param(
    [string]$WdkVersion = "10.0.26100.6584",
    [string]$KmdfVersion = "1.15",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$wdkCache = Join-Path $repoRoot "Build\wdk-nuget"
$wdkPkgDir = Join-Path $wdkCache "Microsoft.Windows.WDK.x64.$WdkVersion"
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "Build\driver\x64"
}

if (-not (Test-Path (Join-Path $wdkPkgDir "c\Include\10.0.26100.0\km\bthddi.h"))) {
    Write-Host "Downloading official Microsoft.Windows.WDK.x64 ($WdkVersion) from NuGet..."
    New-Item -ItemType Directory -Force -Path $wdkCache | Out-Null
    $zipPath = Join-Path $wdkCache "Microsoft.Windows.WDK.x64.$WdkVersion.zip"
    $url = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/$WdkVersion/microsoft.windows.wdk.x64.$WdkVersion.nupkg"
    curl.exe -sSL -o $zipPath $url
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $wdkPkgDir) { Remove-Item -Recurse -Force $wdkPkgDir }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $wdkPkgDir)
    Remove-Item $zipPath -Force
}

$wdkContent = Join-Path $wdkPkgDir "c"
$sdkKit = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer = "10.0.26100.0"

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsRoot = & $vswhere -latest -products * -property installationPath
    $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$objDir = Join-Path $OutDir "obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$incFlags = @(
    "/I`"$PSScriptRoot`"",
    "/I`"$wdkContent\Include\$sdkVer\km`"",
    "/I`"$wdkContent\Include\$sdkVer\km\crt`"",
    "/I`"$wdkContent\Include\$sdkVer\shared`"",
    "/I`"$wdkContent\Include\wdf\kmdf\$KmdfVersion`"",
    "/I`"$sdkKit\Include\$sdkVer\shared`"",
    "/I`"$sdkKit\Include\$sdkVer\km`""
) -join " "

$libPaths = @(
    "/LIBPATH:`"$wdkContent\Lib\$sdkVer\km\x64`"",
    "/LIBPATH:`"$wdkContent\Lib\wdf\kmdf\x64\$KmdfVersion`""
) -join " "

$cFlags = "/nologo /c /kernel /W4 /WX /wd4201 /wd4214 /wd4324 /O2 /Zi /Fd`"$objDir\vc140.pdb`" /GS /guard:cf /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000008 /D_WIN32_WINNT=0x0A00 /DPOOL_NX_OPTIN=1 /DPOOL_ZERO_DOWN_LEVEL_SUPPORT=1 $incFlags"

$srcDriver = Join-Path $PSScriptRoot "Driver.c"
$srcL2cap = Join-Path $PSScriptRoot "L2cap.c"
$objDriver = Join-Path $objDir "Driver.obj"
$objL2cap = Join-Path $objDir "L2cap.obj"
$outSys = Join-Path $OutDir "BthAapBridge.sys"
$outPdb = Join-Path $OutDir "BthAapBridge.pdb"

$linkFlags = "/nologo /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:FxDriverEntry /NODEFAULTLIB /INTEGRITYCHECK /OPT:REF /OPT:ICF /GUARD:CF /DEBUG /PDB:`"$outPdb`" /OUT:`"$outSys`" $libPaths WdfDriverEntry.lib WdfLdr.lib ntoskrnl.lib hal.lib BufferOverflowFastFailK.lib"

Write-Host "Compiling BthAapBridge.sys (KMDF $KmdfVersion, x64)..."
$cmd = "`"$vcvars`" >nul && cl.exe $cFlags /Fo`"$objDriver`" `"$srcDriver`" && cl.exe $cFlags /Fo`"$objL2cap`" `"$srcL2cap`" && link.exe $linkFlags `"$objDriver`" `"$objL2cap`""
cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    throw "KMDF driver build failed with exit code $LASTEXITCODE"
}

Copy-Item (Join-Path $PSScriptRoot "BthAapBridge.inf") (Join-Path $OutDir "BthAapBridge.inf") -Force

$inf2cat = Join-Path $wdkContent "bin\$sdkVer\x86\Inf2Cat.exe"
if (Test-Path $inf2cat) {
    Write-Host "Generating catalog file with Inf2Cat..."
    & $inf2cat /driver:"$OutDir" /os:10_X64 /verbose | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Inf2Cat returned exit code $LASTEXITCODE"
    }
}

Write-Host "Built successfully:"
Get-ChildItem $OutDir -File | Select-Object Name, Length | Format-Table -AutoSize
