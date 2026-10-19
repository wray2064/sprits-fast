# fuzz.ps1 -- coverage-guided fuzzing of every reader of untrusted data.
#
# From the project folder:
#
#     .\tools\fuzz.ps1 -Seconds 60            # every target, a minute each
#     .\tools\fuzz.ps1 -Seconds 600 -Targets aseprite,package
#     .\tools\fuzz.ps1 -List
#
# Builds the AddressSanitizer + libFuzzer configuration (build-fuzz.bat), seeds a
# corpus for each target from tests\fuzz\fuzz_targets.cpp, and runs libFuzzer on
# each in turn. Corpora are kept in build-fuzz\corpus\<target> and grow between
# runs; a crash, a hang or a broken invariant leaves its input in
# build-fuzz\crashes\ and the script reports it and exits non-zero. To keep one as
# a regression test, copy it to tests\fuzz\regressions\<target>\<what-it-was>.bin
# after fixing the bug. See docs\fuzzing.md.
#
#     -Seconds   how long each target runs (default 60)
#     -Targets   only these (default all)
#     -NoBuild   use the build that is there
#     -MaxLen    the longest input tried (default 65536)

param(
    [int]$Seconds = 60,
    [string[]]$Targets = @(),
    [switch]$NoBuild,
    [switch]$List,
    [int]$MaxLen = 65536
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not $NoBuild) {
    & .\build-fuzz.bat
    if ($LASTEXITCODE -ne 0) { throw "the fuzz build failed" }
}
# The AddressSanitizer runtime sits with the compiler.
$msvc = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC" -Directory |
        Sort-Object Name | Select-Object -Last 1
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$env:PATH"

$driver = ".\build-fuzz\fast_fuzz_driver.exe"
$fuzzer = ".\build-fuzz\fast_libfuzzer.exe"
if ($List) { & $driver --list; return }

$all = @(& $driver --list | ForEach-Object { ($_ -split '\s+')[0] })
if ($Targets.Count -eq 0) { $Targets = $all }

$corpus = ".\build-fuzz\corpus"
& $driver --write-seeds $corpus | Out-Null
New-Item -ItemType Directory -Force ".\build-fuzz\crashes" | Out-Null

$results = @()
foreach ($target in $Targets) {
    if ($all -notcontains $target) { Write-Host "no target called $target"; continue }
    New-Item -ItemType Directory -Force "$corpus\$target" | Out-Null
    $regress = "tests\fuzz\regressions\$target"
    $env:FAST_FUZZ_TARGET = $target
    Write-Host ("== {0} for {1} s" -f $target, $Seconds)
    $arguments = @("$corpus\$target")
    if (Test-Path $regress) { $arguments += $regress }
    $arguments += @("-max_total_time=$Seconds", "-timeout=20", "-rss_limit_mb=3072",
                    "-max_len=$MaxLen", "-artifact_prefix=.\build-fuzz\crashes\$target-",
                    "-print_final_stats=1")
    # libFuzzer reports on stderr, which must not be mistaken for an error here.
    $ErrorActionPreference = "Continue"
    $log = & $fuzzer @arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    $execs = if ($log -match 'stat::number_of_executed_units:\s+(\d+)') { [int64]$Matches[1] } else { 0 }
    $features = [regex]::Matches($log, 'ft: (\d+)')
    $cov = if ($features.Count -gt 0) { [int]$features[$features.Count - 1].Groups[1].Value } else { 0 }
    $status = if ($code -eq 0) { "ok" } else { "FAILED ($code)" }
    $results += [pscustomobject]@{ Target = $target; Executions = $execs; Features = $cov; Status = $status }
    if ($code -ne 0) {
        Write-Host $log.Substring([Math]::Max(0, $log.Length - 2500))
    }
}
Remove-Item Env:\FAST_FUZZ_TARGET -ErrorAction SilentlyContinue
$results | Format-Table -AutoSize
$failed = @($results | Where-Object { $_.Status -ne "ok" })
if ($failed.Count -gt 0) {
    Write-Host ("{0} target(s) failed; inputs are in build-fuzz\crashes" -f $failed.Count)
    exit 1
}
Write-Host "every target ran clean"
