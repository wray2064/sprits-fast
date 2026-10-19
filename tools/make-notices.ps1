# make-notices.ps1 -- writes THIRD_PARTY_NOTICES.txt: the licence of everything Fast is
# built on, with the text each asks to travel with it.
#
# From the project folder, after a GUI build (which fetches SDL3 and Dear ImGui):
#
#     .\tools\make-notices.ps1
#
# The result is committed and shipped beside the program (and in the Store package);
# run this again, and read the diff, whenever a dependency or its version changes.
# NOTICE is the short list; this is the full texts.

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

function Section([string]$title, [string]$source, [string]$text) {
    $rule = "=" * 78
    "$rule`r`n$title`r`n$source`r`n$rule`r`n`r`n$($text.Trim())`r`n`r`n"
}

$sdl = "build-gui\_deps\sdl3-src\LICENSE.txt"
$imgui = "build-gui\_deps\imgui-src\LICENSE.txt"
foreach ($needed in @($sdl, $imgui, "assets\fonts\Inter-OFL.txt", "assets\icons\lucide\LICENSE",
                       "..\livesprite-engine\LICENSE", "third_party\stb_image.h")) {
    if (-not (Test-Path $needed)) { throw "$needed is missing (run build-gui.bat first for the fetched libraries)" }
}

# The licence block at the end of stb_image.h: both alternatives, as the author wrote them.
$stb = Get-Content "third_party\stb_image.h" -Raw
$at = $stb.LastIndexOf("This software is available under 2 licenses")
$block = $stb.Substring($at).TrimEnd()
$block = $block -replace '(?s)\*/\s*$', ''

$out = New-Object System.Text.StringBuilder
[void]$out.Append("Sprit's'fast is Copyright (c) 2026 the Sprit's'fast authors and is licensed under the`r`n")
[void]$out.Append("Apache License, Version 2.0 (see LICENSE). It is built on the software below,`r`n")
[void]$out.Append("each under its own licence, reproduced here as those licences ask.`r`n`r`n")
[void]$out.Append((Section "LiveSprite Engine" "https://github.com/wray2064/livesprite-engine" (Get-Content "..\livesprite-engine\LICENSE" -Raw)))
[void]$out.Append((Section "SDL3 (Simple DirectMedia Layer)" "https://github.com/libsdl-org/SDL" (Get-Content $sdl -Raw)))
[void]$out.Append((Section "Dear ImGui" "https://github.com/ocornut/imgui" (Get-Content $imgui -Raw)))
[void]$out.Append((Section "stb_image.h and stb_image_write.h (Sean Barrett)" "https://github.com/nothings/stb" $block))
[void]$out.Append((Section "Inter (Regular and SemiBold), the interface font" "https://github.com/rsms/inter" (Get-Content "assets\fonts\Inter-OFL.txt" -Raw)))
[void]$out.Append((Section "Lucide icons (24 of them)" "https://lucide.dev" (Get-Content "assets\icons\lucide\LICENSE" -Raw)))
[void]$out.Append((Section "Microsoft Visual C++ runtime" "https://visualstudio.microsoft.com/" @"
The C++ runtime libraries (msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll) shipped
beside the program are redistributed under the Microsoft Visual Studio redistribution
terms. They are Microsoft's, and are not covered by the Apache License.
"@))
[IO.File]::WriteAllText((Join-Path (Get-Location) "THIRD_PARTY_NOTICES.txt"), $out.ToString(), (New-Object Text.UTF8Encoding $false))
Write-Host ("THIRD_PARTY_NOTICES.txt: {0:N0} KB" -f ((Get-Item THIRD_PARTY_NOTICES.txt).Length / 1KB))
