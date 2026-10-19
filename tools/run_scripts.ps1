# run_scripts.ps1 -- runs every UI script in tests\ui against the GUI build and
# says which failed. From the project folder:
#
#     .\tools\run_scripts.ps1                  # all of them
#     .\tools\run_scripts.ps1 -Only menu_      # those whose name has "menu_" in it
#     .\tools\run_scripts.ps1 -Exe build-release\sprits_fast.exe
#
# A script fails when it prints FAIL, when ImGui reports an error in how the
# interface was built, or when the program exits with anything but 0. The exit
# code here is the number of scripts that failed.

param(
    [string]$Exe = "build-gui\sprits_fast.exe",
    [string]$Only = ""
)

$project = Split-Path -Parent $PSScriptRoot
Set-Location $project
if (-not (Test-Path $Exe)) { throw "$Exe is not built: run .\build-gui.bat" }

$failed = @()
$ran = 0
$checks = 0
$start = Get-Date
foreach ($script in Get-ChildItem tests\ui\*.txt | Sort-Object Name) {
    if ($Only -ne "" -and $script.Name -notlike "*$Only*") { continue }
    $ran++
    $output = & $Exe --script $script.FullName 2>&1 | Out-String
    $code = $LASTEXITCODE
    $summary = [regex]::Match($output, 'script: (\d+) check\(s\), (\d+) failed')
    if ($summary.Success) { $checks += [int]$summary.Groups[1].Value }
    $bad = $code -ne 0 -or $output -cmatch 'FAIL' -or $output -match 'imgui-error' -or -not $summary.Success
    if ($bad) {
        $failed += $script.Name
        Write-Host ("FAIL  {0}" -f $script.Name) -ForegroundColor Red
        ($output -split "`n" | Where-Object { $_ -match 'FAIL|imgui-error|script:' } | Select-Object -First 8) |
            ForEach-Object { Write-Host ("      " + $_.Trim()) }
    } else {
        Write-Host ("ok    {0}  ({1} checks)" -f $script.Name, $summary.Groups[1].Value)
    }
}
$seconds = [int]((Get-Date) - $start).TotalSeconds
Write-Host ("{0} scripts, {1} checks, {2} failed, {3} s" -f $ran, $checks, $failed.Count, $seconds)
exit $failed.Count
