param([string]$compiler = "$PSScriptRoot/../build/Debug/C4R.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/operator-tests'
New-Item -ItemType Directory -Force $dir | Out-Null

function Test-Program($name, $source, $expectedExit, $expectedOutput = '') {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $compile = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $compile" }
    $output = & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-String
    if ($LASTEXITCODE -ne $expectedExit) { throw "$name exit: $LASTEXITCODE, expected $expectedExit" }
    if ($output.Trim() -ne $expectedOutput) { throw "$name output: $output, expected $expectedOutput" }
}
function Test-Rejected($name, $source, $message) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $output = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or !$output.Contains($message)) { throw "$name should fail with $message`: $output" }
}

Test-Program arithmetic 'int32 main() { return (2 + 4 * 3) + (2 ` 3 ` 2) / 8 - 4 % 3; }' 77
Test-Program bitwise 'int32 main() { int32 mutable x = 3; x <<= 3; x |= 2; x ^= 3; x &= 31; x >>= 1; return ~(-x) & 31; }' 11
Test-Program mutation 'int32 main() { int32 mutable x = 2; int32 a = x++; int32 b = ++x; x += 3; x -= 1; x *= 2; x /= 3; x %= 4; return a * 20 + b * 4 + x; }' 56
Test-Program boolean 'int32 main() { bool a = 18 <= 25 <= 130; bool b = true ^^ false; bool c = true !&& false; bool d = false !|| false; f"{a}{b}{c}{d}\n"; return 0; }' 0 '1111'
Test-Program shortcircuit 'int32 main() { int32 mutable x = 0; bool a = false && (++x == 1); bool b = true || (++x == 1); bool c = false !&& (++x == 1); bool d = true !|| (++x == 1); bool e = 3 < 2 < ++x; return x; }' 0
Test-Program chain 'int32 main() { int32 mutable x = 0; bool a = 0 < ++x < 3; return x; }' 1
Test-Program mixed_chain 'int32 main() { bool a = 1 < 2 == 2; bool b = 2 == 2 < 3; f"{a}{b}\n"; return 0; }' 0 '11'
Test-Program implicit_main 'int32 mutable x = 2; x *= 3; f"{x}\n";' 0 '6'
Test-Rejected immutable 'int32 main() { int32 x = 1; x += 2; return x; }' 'Cannot modify immutable'
Test-Rejected lvalue 'int32 main() { return ++(1 + 2); }' 'Assignment requires a variable'
Test-Rejected types 'int32 main() { bool x = true + false; return 0; }' 'requires int32'
Test-Rejected negative_exponent 'int32 main() { return 2 ` -1; }' 'exponent must be nonnegative'
Write-Host 'Operator tests passed.'
