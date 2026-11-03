param([string]$compiler = "$PSScriptRoot/../build/Debug/C4R.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/control-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
function Test-Program($name, $source, $expected) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $compile = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $compile" }
    & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-Null
    if ($LASTEXITCODE -ne $expected) { throw "$name exit $LASTEXITCODE, expected $expected" }
}
function Test-Rejected($name, $source, $message) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $result = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($message)) { throw "$name should fail with $message`: $result" }
}
Test-Program if_return 'int32 main() { bool error = true; if (error) { return 7; } return 10; }' 7
Test-Program if_else 'int32 main() { int32 mutable x = 0; if (false) { x = 1; } else if (true) { x = 2; } else { x = 3; } return x; }' 2
Test-Program if_value 'int32 main() { int32 n = 1; int32 x = if (n == 0) { return 2; } else { return 4; }; return x; }' 4
Test-Program if_value_else_if 'int32 main() { int32 x = if (false) { return 1; } else if (true) { return 2; } else { return 3; }; return x; }' 2
Test-Program nested_value 'int32 main() { int32 x = if (true) { int32 y = if (false) { return 1; } else { return 5; }; return y + 2; } else { return 3; }; return x; }' 7
Test-Program nested_statement 'int32 main() { int32 x = { if (true) { return 5; } else { return 9; } }; return x; }' 5
Test-Program loops 'int32 main() { int32 mutable x = 0; for (int32 mutable i = 0; i < 5; i++) { if (i == 2) { continue; } x += i; } while (x < 10) { x++; } do { x++; } while (x < 11); return x; }' 11
Test-Program loop_break 'int32 main() { int32 mutable x = 0; for (;;) { x++; if (x == 3) { break; } } return x; }' 3
Test-Program block_scope 'int32 main() { int32 x = 3; { int32 x = 8; } int32 y = { int32 x = 4; return x; }; return x + y; }' 7
Test-Program implicit 'int32 mutable x = 0; while (x < 3) { x++; } if (x == 3) { return 6; } return 2;' 6
Test-Rejected no_else 'int32 main() { int32 x = if (true) { return 1; }; return x; }' 'requires else'
Test-Rejected missing_return 'int32 main() { int32 x = if (true) { return 1; } else { int32 y = 2; }; return x; }' 'must return'
Test-Rejected mismatched 'int32 main() { int32 x = if (true) { return 1; } else { return false; }; return x; }' 'types do not match'
Test-Rejected scope_leak 'int32 main() { if (true) { int32 x = 1; } return x; }' 'Undefined variable'
Test-Rejected for_scope 'int32 main() { for (int32 mutable i = 0; i < 1; i++) {} return i; }' 'Undefined variable'
Test-Rejected condition_type 'int32 main() { if (1) { return 1; } return 0; }' 'Condition must be bool'
Test-Rejected invalid_break 'int32 main() { break; return 0; }' 'outside loop'
Test-Rejected escaping_value 'int32 main() { while (true) { int32 x = { break; }; } return 0; }' 'Cannot leave a value scope'
Write-Host 'Control tests passed.'
