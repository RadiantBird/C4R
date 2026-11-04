param([string]$compiler = "$PSScriptRoot/../build/Debug/C4R.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/function-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
function Test-Program($name, $source, $expectedExit, $expectedOutput = '') {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $compile = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $compile" }
    $output = & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-String
    if ($LASTEXITCODE -ne $expectedExit) { throw "$name exit $LASTEXITCODE, expected $expectedExit" }
    if ($output.Trim().Replace("`r`n", "`n") -ne $expectedOutput.Replace("`r`n", "`n")) { throw "$name output '$output', expected '$expectedOutput'" }
}
function Test-Rejected($name, $source, $message) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $result = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($message)) { throw "$name should fail with $message`: $result" }
}
Test-Program arguments 'int32 main() { return add(10, twice(5)); } int32 add(int32 a, int32 b) { return a + b; } int32 twice(int32 x) { return x * 2; }' 20
Test-Program local 'int32 main() { int32 a() { int32 b() { return 42; } return b(); } return a(); }' 42
Test-Program bare 'void hello() { "hello\n"; } hello; hello();' 0 "hello`nhello"
Test-Program capture 'int32 main() { int32 x = 10; int32 y = 20; int32 add() { capture x, y; return x + y; } return add(); }' 30
Test-Program mutable_capture 'int32 main() { int32 mutable x = 10; void increment() { capture x; x++; } increment(); increment; return x; }' 12
Test-Program capture_shadow 'int32 main() { int32 x = 7; int32 read() { capture x; return x; } { int32 x = 99; return read(); } }' 7
Test-Program nested_capture 'int32 main() { int32 x = 7; int32 a() { capture x; int32 b() { capture x; return x; } return b(); } return a(); }' 7
Test-Program recursive_capture 'int32 main() { int32 x = 2; int32 f(int32 n) { capture x; if (n == 0) { return x; } return f(n - 1) + x; } return f(3); }' 8
Test-Program local_scope 'int32 main() { int32 mutable x = 1; { int32 value() { return 2; } x += value(); } { int32 value() { return 4; } x += value(); } return x; }' 7
Test-Program mutable_parameter 'int32 f(int32 mutable x) { x++; return x; } int32 main() { int32 x = 4; return f(x) + x; }' 9
Test-Program start_entry 'int32 start() { return 13; }' 13
Test-Program branch_return 'int32 main() { int32 f() { if (true) { return 8; } return 3; } int32 x = if (true) { return f(); } else { return 4; }; return x; }' 8
Test-Program assignment_value_scope 'int32 main() { int32 mutable x = 1; x += { int32 y = 3; return y; }; return x; }' 4
Test-Program shadow_outer_variable 'int32 main() { int32 value = 2; int32 f() { int32 value() { return 5; } return value(); } return f(); }' 5
Test-Program capture_parameter 'int32 f(int32 mutable x) { int32 g() { capture x; x += 3; return x; } return g(); } int32 main() { return f(4); }' 7
Test-Rejected missing_capture 'int32 main() { int32 x = 1; int32 f() { return x; } return f(); }' 'requires capture'
Test-Rejected unknown_capture 'int32 main() { int32 f() { capture x; return 1; } return f(); }' 'Cannot capture undefined'
Test-Rejected missing_enclosing_capture 'int32 main() { int32 x = 1; int32 a() { int32 b() { capture x; return x; } return b(); } return a(); }' 'requires capture in enclosing'
Test-Rejected immutable_capture 'int32 main() { int32 x = 1; void f() { capture x; x++; } f(); return x; }' 'Cannot modify immutable'
Test-Rejected out_of_scope 'int32 main() { { int32 f() { return 1; } } return f(); }' 'Undefined function'
Test-Rejected argument_count 'int32 f(int32 x) { return x; } int32 main() { return f(); }' 'Wrong number of arguments'
Test-Rejected argument_type 'int32 f(int32 x) { return x; } int32 main() { return f(true); }' 'Argument type mismatch'
Test-Rejected duplicate_params 'int32 f(int32 x, bool x) { return 0; } int32 main() { return 0; }' 'Duplicate parameter'
Test-Rejected global_capture 'int32 f() { capture x; return 0; } int32 main() { return 0; }' 'capture requires a local function'
Test-Rejected duplicate_function 'int32 f() { return 0; } int32 f() { return 1; } int32 main() { return 0; }' 'Duplicate function'
Test-Rejected wrong_main 'void main() {}' 'main must return int'
Test-Rejected void_operand 'void f() {} int32 main() { return f() == f(); }' 'Void expression cannot'
Test-Rejected void_value_scope 'void f() {} int32 main() { int32 x = { return f(); }; return x; }' 'Value scope cannot return void'
Write-Host 'Function tests passed.'
