# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
#
# Removes what Install.ps1 added, and only that. Your documents are never
# touched, and neither are your settings, recent files and recovery copies in
# %APPDATA%\SpritsFast -- delete that folder yourself if you want them gone.
#
# Try it without changing anything:   .\Uninstall.ps1 -DryRun

[CmdletBinding()]
param(
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$name = "Sprit's'fast"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$progId = "SpritsFast.Document"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SpritsFast"
$shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$name.lnk"
$classes = "HKCU:\Software\Classes"

function Step([string]$what, [scriptblock]$action) {
    if ($DryRun) {
        Write-Host "[dry run] $what"
    } else {
        Write-Host $what
        & $action
    }
}

# The install folder is wherever the entry in Settings > Apps says it is; this
# script is a copy of the one in it.
$installDir = $here
if (Test-Path $uninstallKey) {
    $recorded = (Get-ItemProperty -Path $uninstallKey).InstallLocation
    if ($recorded) { $installDir = $recorded }
}
if (-not (Test-Path (Join-Path $installDir "sprits_fast.exe"))) {
    throw "No $name found in $installDir. Nothing was removed."
}
if (-not $DryRun) {
    $running = Get-Process -Name "sprits_fast" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($installDir, [System.StringComparison]::OrdinalIgnoreCase) }
    if ($running) {
        throw "$name is running. Close it and run this again."
    }
}

# The association is removed only if it is still ours: another program may
# have taken .lsprite since.
if ((Test-Path "$classes\.lsprite") -and
    ((Get-ItemProperty -Path "$classes\.lsprite")."(default)" -eq $progId)) {
    Step "Removing the .lsprite association" {
        Remove-Item -Path "$classes\.lsprite" -Recurse -Force
        Remove-Item -Path "$classes\$progId" -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (Test-Path $shortcut) {
    Step "Removing the Start menu shortcut" { Remove-Item -Path $shortcut -Force }
}
if (Test-Path $uninstallKey) {
    Step "Removing $name from Settings > Apps" { Remove-Item -Path $uninstallKey -Recurse -Force }
}

# This script is inside the folder being removed, so the folder goes from a
# process that outlives it.
Step "Removing $installDir" {
    $command = "ping 127.0.0.1 -n 3 > nul & rmdir /s /q `"$installDir`""
    Start-Process -FilePath "cmd.exe" -ArgumentList "/c", $command -WindowStyle Hidden
}

if ($DryRun) {
    Write-Host "Nothing was changed."
} else {
    Write-Host "Removed $name. Your settings in $env:APPDATA\SpritsFast were left alone."
}
