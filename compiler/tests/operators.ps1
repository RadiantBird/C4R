param([string]$compiler = "$PSScriptRoot/../build/Debug/c4rc.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/operator-tests'
New-Item -ItemType Directory -Force $dir | Out-Null

function Test-Program($name, $source, $expectedExit, $expectedOutput = '') {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $compile = & $compiler $path 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $compile" }
    $output = & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-String -Width 10000
    if ($LASTEXITCODE -ne $expectedExit) { throw "$name exit: $LASTEXITCODE, expected $expectedExit" }
    if ($output.Trim() -ne $expectedOutput) { throw "$name output: $output, expected $expectedOutput" }
}
function Test-Rejected($name, $source, $message) {
    $ErrorActionPreference = 'Continue'
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $output = & $compiler $path 2>&1 | Out-String -Width 10000
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
Test-Program float_arithmetic 'int main() { float mutable x = 1.5; x += 2.0; float y = -x * 2.0 / 7.0; if (y == -1.0 && x > 3.0) { return 0; } return 1; }' 0
Test-Program float_format 'int main() { float x = 1.5; f"{x}\n"; return 0; }' 0 '1.5'
Test-Program checked_division 'int div(int a, int b) { return a / b; } int rem(int a, int b) { return a % b; } int main() { return div(17, 3) + rem(17, 3); }' 7
Test-Program signed_division 'int div(int a, int b) { return a / b; } int rem(int a, int b) { return a % b; } int main() { if (div(-7, 3) == -2 && rem(-7, 3) == -1) { return 0; } return 1; }' 0
Test-Program float_division 'float f(float x) { return x / 0.0; } int main() { if (f(1.0) > 0.0) { return 0; } return 1; }' 0
Test-Program skipped_division 'int f(bool test, int divisor) { if (test && (1 / divisor > 0)) { return 1; } return 0; } int main() { bool a = false && (1 / 0 == 0); bool b = 3 < 2 < 1 / 0; if (false) { int x = 1 / 0; } while (false) { int x = 1 / 0; } return f(false, 0); }' 0
Test-Program checked_shifts 'int shl(int a, int b) { return a << b; } int shr(int a, int b) { return a >> b; } int main() { if (shl(1, 31) < 0 && shr(-8, 2) == -2 && shl(2, 0) == 2) { return 0; } return 1; }' 0
Test-Program skipped_shift 'int f(bool test, int count) { if (test && ((1 << count) > 0)) { return 1; } return 0; } int main() { bool a = false && ((1 << 32) == 0); bool b = 3 < 2 < (1 << -1); if (false) { int x = 1 << 32; } return f(false, 32); }' 0
Test-Rejected float_mixed 'int main() { float x = 1.0 + 1; return 0; }' 'Operand types do not match'
Test-Rejected immutable 'int32 main() { int32 x = 1; x += 2; return x; }' 'Cannot modify immutable'
Test-Rejected lvalue 'int32 main() { return ++(1 + 2); }' 'Assignment requires a variable'
Test-Rejected types 'int32 main() { bool x = true + false; return 0; }' 'requires int32'
Test-Rejected negative_exponent 'int32 main() { return 2 ` -1; }' 'exponent must be nonnegative'
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class C4RTestErrorMode {
    [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
}
'@
$previousErrorMode = [C4RTestErrorMode]::SetErrorMode(2)
try {
    foreach ($test in @(
        @{ Name = 'divide_zero'; Source = 'int f(int a, int b) { return a / b; } int main() { return f(4, 0); }' },
        @{ Name = 'remainder_zero'; Source = 'int f(int a, int b) { return a % b; } int main() { return f(4, 0); }' },
        @{ Name = 'divide_overflow'; Source = 'int f(int a, int b) { return a / b; } int main() { return f(-2147483647 - 1, -1); }' },
        @{ Name = 'remainder_overflow'; Source = 'int f(int a, int b) { return a % b; } int main() { return f(-2147483647 - 1, -1); }' },
        @{ Name = 'compound_divide_zero'; Source = 'int f(int b) { int mutable a = 4; a /= b; return a; } int main() { return f(0); }' },
        @{ Name = 'compound_remainder_overflow'; Source = 'int f(int b) { int mutable a = -2147483647 - 1; a %= b; return a; } int main() { return f(-1); }' },
        @{ Name = 'constant_minus_one_overflow'; Source = 'int f(int a) { return a / -1; } int main() { return f(-2147483647 - 1); }' },
        @{ Name = 'shift_left_large'; Source = 'int f(int b) { return 1 << b; } int main() { return f(32); }' },
        @{ Name = 'shift_right_negative'; Source = 'int f(int b) { return 1 >> b; } int main() { return f(-1); }' },
        @{ Name = 'compound_shift_large'; Source = 'int f(int b) { int mutable a = 4; a <<= b; return a; } int main() { return f(33); }' }
    )) {
        $path = Join-Path $dir ($test.Name + '.c4r')
        [IO.File]::WriteAllText($path, $test.Source)
        $compile = & $compiler $path 2>&1 | Out-String -Width 10000
        if ($LASTEXITCODE -ne 0) { throw "$($test.Name) compilation failed: $compile" }
        $process = Start-Process -FilePath ([IO.Path]::ChangeExtension($path, '.exe')) -NoNewWindow -PassThru -Wait
        if ($process.ExitCode -ne -1073741795) { throw "$($test.Name) should trap with illegal-instruction status, got $($process.ExitCode)" }
    }
} finally { [void][C4RTestErrorMode]::SetErrorMode($previousErrorMode) }
Write-Host 'Operator tests passed.'
