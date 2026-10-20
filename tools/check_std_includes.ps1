# check_std_includes.ps1 -- does each source file include the standard headers it uses?
#
# MSVC and Apple's compiler reach <cstdint>, <string>, <vector> ... through other
# headers; GCC's library often does not, and the Ubuntu job in CI then fails on a file
# that builds everywhere else. This lists, for each file under src and tests, the
# standard headers whose symbols it uses and does not include itself.
#
#     .\tools\check_std_includes.ps1            # the headers (exit code: how many files)
#     .\tools\check_std_includes.ps1 -Fix       # add the includes, in the standard block
#     .\tools\check_std_includes.ps1 -Sources   # the .cpp files too
#
# Headers are what matters: a .cpp that includes its own header gets what that includes
# from it, and a header that does not name what it uses fails in every file that
# includes it. So only headers are checked unless -Sources says otherwise (and then most
# .cpp files are listed for what their own headers supply, which is fine). A symbol
# named only in a comment counts as used, so a fix may add an include a file does not
# strictly need: that is harmless.

param([switch]$Fix, [switch]$Sources)

$project = Split-Path -Parent $PSScriptRoot
Set-Location $project
$utf8 = New-Object Text.UTF8Encoding $false

$families = [ordered]@{
  '<algorithm>' = 'std::(sort|stable_sort|min|max|clamp|find|find_if|remove|remove_if|reverse|swap_ranges|count|count_if|any_of|all_of|none_of|lower_bound|upper_bound|unique|fill|copy|copy_if|transform|min_element|max_element|equal|rotate)\b'
  '<cmath>' = 'std::(abs|lround|round|floor|ceil|sqrt|fabs|atan2|atan|pow|sin|cos|tan|fmod|hypot|isnan|isfinite|exp|log|trunc|lrint|cbrt)\b'
  '<cstdio>' = 'std::(printf|snprintf|fprintf|sprintf|fopen|fclose|puts|FILE|fputs|fwrite|fread)\b'
  '<cstdlib>' = 'std::(atoi|atof|strtof|strtol|strtod|strtoul|getenv|rand|exit|abort|qsort|llabs)\b'
  '<cstring>' = 'std::(memcpy|memset|strlen|strcmp|strncmp|strncpy|strcpy|memcmp|memmove|strchr|strstr|strerror)\b'
  '<ctime>' = 'std::(time|localtime|gmtime|strftime|mktime|difftime|tm|time_t)\b'
  '<cctype>' ='std::(isspace|isalpha|isdigit|isalnum|tolower|toupper|isxdigit|ispunct|isupper|islower)\b'
  '<cstdint>' = '\b(u?int(8|16|32|64)_t)\b'
  '<cstddef>' = 'std::(size_t|ptrdiff_t|nullptr_t)\b'
  '<string>' = 'std::(string|to_string|stoi|stof|stod|getline)\b'
  '<vector>' = 'std::vector\b'
  '<utility>' = 'std::(move|pair|swap|forward|make_pair|exchange)\b'
  '<functional>' = 'std::(function|bind|ref|cref)\b'
  '<memory>' = 'std::(unique_ptr|shared_ptr|make_unique|make_shared|weak_ptr)\b'
  '<map>' = 'std::(map|multimap)\b'
  '<set>' = 'std::(set|multiset)\b'
  '<unordered_map>' = 'std::unordered_map\b'
  '<unordered_set>' = 'std::unordered_set\b'
  '<array>' = 'std::array\b'
  '<optional>' = 'std::(optional|nullopt)\b'
  '<limits>' = 'std::numeric_limits\b'
  '<chrono>' = 'std::chrono\b'
  '<sstream>' = 'std::(ostringstream|istringstream|stringstream)\b'
  '<numeric>' = 'std::(accumulate|iota|gcd)\b'
  '<deque>' = 'std::deque\b'
  '<list>' = 'std::list\b'
}

$files = Get-ChildItem src, tests -Recurse -Include $(if ($Sources) { '*.h', '*.cpp' } else { '*.h' })
$count = 0
foreach ($file in $files) {
    $text = [IO.File]::ReadAllText($file.FullName, $utf8)
    $missing = @()
    foreach ($header in $families.Keys) {
        if ($text -match $families[$header] -and $text -notmatch [regex]::Escape("#include $header")) {
            $missing += $header
        }
    }
    if ($missing.Count -eq 0) { continue }
    $count++
    $name = $file.FullName.Substring($project.Length + 1)
    Write-Host ("{0}: {1}" -f $name, ($missing -join ' '))
    if (-not $Fix) { continue }

    $lines = [System.Collections.Generic.List[string]]::new($text -split "`n")
    $last = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^#include <[a-z_]+>\r?$') { $last = $i } }
    $fresh = $false
    if ($last -lt 0) {
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].StartsWith('#include')) { $last = $i } }
        $fresh = $true
    }
    if ($last -lt 0) { continue }
    $insert = [System.Collections.Generic.List[string]]::new()
    if ($fresh) { $insert.Add("") }
    foreach ($header in $missing) { $insert.Add("#include $header") }
    $lines.InsertRange($last + 1, $insert)
    # keep a block of standard includes sorted
    $first = -1; $end = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^#include <[a-z_]+>$') { if ($first -lt 0) { $first = $i }; $end = $i }
    }
    if ($first -ge 0) {
        $block = $lines.GetRange($first, $end - $first + 1)
        if (-not ($block | Where-Object { $_ -notmatch '^#include <[a-z_]+>$' })) {
            $lines.RemoveRange($first, $block.Count)
            $lines.InsertRange($first, [string[]]($block | Sort-Object))
        }
    }
    [IO.File]::WriteAllText($file.FullName, ($lines -join "`n"), $utf8)
}
Write-Host ("{0} file(s) {1}" -f $count, $(if ($Fix) { "fixed" } else { "need a standard include" }))
exit $count
