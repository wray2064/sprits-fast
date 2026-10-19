# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
#
# Installs Sprit's'fast for the current user, from the folder this script is in
# (the one build-release.bat makes). Nothing needs administrator rights and
# nothing outside the current user's own settings is touched:
#
#   - the program is copied to %LOCALAPPDATA%\Programs\SpritsFast;
#   - .lsprite files open with it (HKCU\Software\Classes);
#   - a Start menu shortcut;
#   - an entry in Settings > Apps, whose Uninstall runs Uninstall.ps1.
#
# Your settings, recent files and recovery copies (%APPDATA%\SpritsFast) are
# never read or changed here.
#
# Try it without changing anything:   .\Install.ps1 -DryRun
# Leave something out:                -NoAssociation  -NoShortcut
# Somewhere else:                     -InstallDir D:\Tools\SpritsFast

[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\SpritsFast"),
    [switch]$NoAssociation,
    [switch]$NoShortcut,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$name = "Sprit's'fast"
$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$exeName = "sprits_fast.exe"
$progId = "SpritsFast.Document"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SpritsFast"
$shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$name.lnk"

# Every change goes through here, so -DryRun can say what it would do and do none of it.
function Step([string]$what, [scriptblock]$action) {
    if ($DryRun) {
        Write-Host "[dry run] $what"
    } else {
        Write-Host $what
        & $action
    }
}

if (-not (Test-Path (Join-Path $source $exeName))) {
    throw "$exeName is not beside this script ($source). Run Install.ps1 from the folder build-release.bat made."
}
if (-not $DryRun) {
    $running = Get-Process -Name "sprits_fast" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir, [System.StringComparison]::OrdinalIgnoreCase) }
    if ($running) {
        throw "$name is running from $InstallDir. Close it and run this again."
    }
}

$version = (Get-Item (Join-Path $source $exeName)).VersionInfo.ProductVersion
if ([string]::IsNullOrWhiteSpace($version)) { $version = "1.0.0" }
$exe = Join-Path $InstallDir $exeName

Step "Copying the program to $InstallDir" {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Get-ChildItem -Path $source -Force | Where-Object { $_.Name -ne "Install.ps1" } |
        Copy-Item -Destination $InstallDir -Recurse -Force
    Copy-Item -Path (Join-Path $source "Install.ps1") -Destination $InstallDir -Force
}

if (-not $NoAssociation) {
    Step "Opening .lsprite files with $name" {
        $classes = "HKCU:\Software\Classes"
        New-Item -Path "$classes\.lsprite" -Force | Out-Null
        Set-ItemProperty -Path "$classes\.lsprite" -Name "(default)" -Value $progId
        New-Item -Path "$classes\$progId" -Force | Out-Null
        Set-ItemProperty -Path "$classes\$progId" -Name "(default)" -Value "LiveSprite document"
        New-Item -Path "$classes\$progId\DefaultIcon" -Force | Out-Null
        Set-ItemProperty -Path "$classes\$progId\DefaultIcon" -Name "(default)" -Value "`"$exe`",0"
        New-Item -Path "$classes\$progId\shell\open\command" -Force | Out-Null
        Set-ItemProperty -Path "$classes\$progId\shell\open\command" -Name "(default)" -Value "`"$exe`" `"%1`""
        # Tell Explorer, so the icon and the double-click work without signing out.
        Add-Type -Namespace SpritsFast -Name Shell -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
"@
        [SpritsFast.Shell]::SHChangeNotify(0x08000000, 0, [System.IntPtr]::Zero, [System.IntPtr]::Zero)
    }
}

if (-not $NoShortcut) {
    Step "Adding a Start menu shortcut" {
        $shell = New-Object -ComObject WScript.Shell
        $link = $shell.CreateShortcut($shortcut)
        $link.TargetPath = $exe
        $link.WorkingDirectory = $InstallDir
        $link.Description = $name
        $link.Save()
    }
}

Step "Adding $name to Settings > Apps" {
    New-Item -Path $uninstallKey -Force | Out-Null
    Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value $name
    Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value $version
    Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "the Sprit's'fast authors"
    Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $InstallDir
    Set-ItemProperty -Path $uninstallKey -Name "DisplayIcon" -Value $exe
    Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'Uninstall.ps1')`""
    Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1 -Type DWord
}

if ($DryRun) {
    Write-Host "Nothing was changed."
} else {
    Write-Host "Installed $name $version to $InstallDir."
}
