# make-msix.ps1 -- builds the MSIX package for the Microsoft Store.
#
# From the project folder:
#
#     .\tools\make-msix.ps1                       # a package for checking: placeholder identity
#     .\tools\make-msix.ps1 -IdentityName 12345Name.SpritsFast `
#         -Publisher "CN=ABCD1234-...." -PublisherDisplayName "Your name"
#
# The real identity comes from Partner Center (Product management > Product
# identity); use exactly what it shows. The Store signs the package when it is
# submitted, so the file this makes for the Store needs no certificate of yours.
#
# Steps: the release build (build-release.bat), a layout folder (the program, its
# language catalogues and runtime, the Store images, the filled manifest),
# resources.pri (makepri), and makeappx pack, which also checks the manifest and
# that every file it names is there.
#
#     -Version         four numbers; the last must be 0 (the Store keeps it). Default: the
#                      project's, from CMakeLists.txt, with .0 added
#     -SkipBuild       use dist\SpritsFast as it is
#     -TestSign        also make a signed copy for trying the package on a machine that
#                      trusts a test certificate: it creates a self-signed certificate, signs,
#                      exports the certificate (.cer) and removes it from your store again.
#                      It never trusts or installs anything; doing that is up to you.
#
# Needs the Windows SDK (makeappx, makepri, signtool) -- found under Windows Kits\10\bin.

param(
    [string]$IdentityName = "SpritsFast.Dev",
    [string]$Publisher = "CN=SpritsFast Dev",
    [string]$PublisherDisplayName = "the Sprit's'fast authors",
    [string]$Version = "",
    [switch]$SkipBuild,
    [switch]$TestSign
)

$ErrorActionPreference = "Stop"
$project = Split-Path -Parent $PSScriptRoot
Set-Location $project

# The version: the project's own.
if ($Version -eq "") {
    $cmake = Get-Content CMakeLists.txt -Raw
    if ($cmake -notmatch 'project\(sprits_fast VERSION (\d+)\.(\d+)\.(\d+)') { throw "no version in CMakeLists.txt" }
    $Version = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
}
if ($Version -notmatch '^\d+\.\d+\.\d+\.0$') { throw "the Store reserves the last number: the version must end in .0 ($Version)" }

# The Windows SDK tools, the newest installed.
$kits = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Directory |
        Where-Object { $_.Name -match '^10\.' } | Sort-Object Name -Descending
$bin = $kits | ForEach-Object { Join-Path $_.FullName "x64" } | Where-Object { Test-Path (Join-Path $_ "makeappx.exe") } | Select-Object -First 1
if (-not $bin) { throw "the Windows SDK (makeappx.exe) was not found" }
$makeappx = Join-Path $bin "makeappx.exe"
$makepri = Join-Path $bin "makepri.exe"
$signtool = Join-Path $bin "signtool.exe"

if (-not $SkipBuild) {
    # The compiler's environment script says things on stderr that are not errors.
    $ErrorActionPreference = "Continue"
    & .\build-release.bat 2>&1 | ForEach-Object { "$_" } | Select-Object -Last 3
    $built = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if (-not (Test-Path "build-release\sprits_fast.exe")) { throw "the release build failed" }
}
if (-not (Test-Path "dist\SpritsFast\sprits_fast.exe")) { throw "dist\SpritsFast\sprits_fast.exe is missing: run build-release.bat" }
if (-not (Test-Path "store\assets")) { throw "store\assets is missing: run python tools\make_store_assets.py" }

# The layout: what the package will hold.
$layout = "dist\msix-layout"
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
New-Item -ItemType Directory -Force "$layout\Assets" | Out-Null
Get-ChildItem "dist\SpritsFast" | Where-Object { $_.Name -notmatch '^(Install|Uninstall)\.ps1$' } |
    ForEach-Object { Copy-Item $_.FullName $layout -Recurse }
Copy-Item "store\assets\*.png" "$layout\Assets"
Copy-Item "LICENSE" $layout -ErrorAction SilentlyContinue
Copy-Item "NOTICE" $layout -ErrorAction SilentlyContinue

$manifest = Get-Content "store\AppxManifest.xml.in" -Raw
$manifest = $manifest.Replace("@IDENTITY_NAME@", $IdentityName).Replace("@PUBLISHER@", $Publisher).
            Replace("@PUBLISHER_DISPLAY_NAME@", $PublisherDisplayName).Replace("@VERSION@", $Version)
if ($manifest -match '@[A-Z_]+@') { throw "an unfilled value is left in the manifest: $($Matches[0])" }
[IO.File]::WriteAllText((Join-Path (Resolve-Path $layout) "AppxManifest.xml"), $manifest, (New-Object Text.UTF8Encoding $false))

# resources.pri: what lets Windows pick the right scale of each image.
$config = "dist\priconfig.xml"
& $makepri createconfig /cf $config /dq en-US /pv 10.0.0 /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri createconfig failed" }
& $makepri new /pr $layout /cf $config /mn "$layout\AppxManifest.xml" /of "$layout\resources.pri" /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri new failed" }

# The package.
$output = "dist\SpritsFast-$Version-x64.msix"
if (Test-Path $output) { Remove-Item $output }
& $makeappx pack /d $layout /p $output /o
if ($LASTEXITCODE -ne 0) { throw "makeappx pack failed" }
$size = (Get-Item $output).Length
Write-Host ("Built {0} ({1:N1} MB) as {2}, publisher {3}" -f $output, ($size / 1MB), $IdentityName, $Publisher)

if ($TestSign) {
    # A self-signed certificate whose subject is the package's publisher, made only to
    # sign this copy. It is exported and then removed from the certificate store.
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature `
        -FriendlyName "Sprit's'fast test signing" -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    try {
        $password = ConvertTo-SecureString -String ([guid]::NewGuid().ToString()) -Force -AsPlainText
        $pfx = "dist\SpritsFast-test.pfx"
        Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $password | Out-Null
        Export-Certificate -Cert $cert -FilePath "dist\SpritsFast-test.cer" | Out-Null
        $signed = "dist\SpritsFast-$Version-x64-testsigned.msix"
        Copy-Item $output $signed -Force
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($password))
        & $signtool sign /fd SHA256 /f $pfx /p $plain $signed
        if ($LASTEXITCODE -ne 0) { throw "signtool failed" }
        Remove-Item $pfx
        Write-Host "Signed copy: $signed. Its certificate is dist\SpritsFast-test.cer: to install the copy, a machine must trust that certificate (yours to decide; nothing here did)."
    } finally {
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -ErrorAction SilentlyContinue
    }
}
