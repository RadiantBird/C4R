param([string]$compiler = "$PSScriptRoot/../build/Debug/c4rc.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/multifile-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
$main = Join-Path $dir 'main.c4r'
$other = Join-Path $dir 'other.c4r'
$output = Join-Path $dir 'app.exe'
[IO.File]::WriteAllText($main, 'int main() { Point p {}; return add(p.value(), 2); }')
[IO.File]::WriteAllText($other, 'struct Point { int value() { return 3; } }; int add(int a, int b) { return a + b; }')
$existingObject = [IO.Path]::ChangeExtension($main, '.obj')
[IO.File]::WriteAllText($existingObject, 'preserve existing object')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 5) { throw "Incorrect program result: $LASTEXITCODE" }
}
if ([IO.File]::ReadAllText($existingObject) -ne 'preserve existing object') { throw 'Compiler overwrote an unrelated object file' }
$compile = & $compiler --input $main -i $other --output $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Repeated input options failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 5) { throw "Incorrect repeated input result: $LASTEXITCODE" }
foreach ($entry in @('start', 'entry')) {
    [IO.File]::WriteAllText($main, "int $entry() { return add(2, 3); }")
    [IO.File]::WriteAllText($other, 'int add(int a, int b) { return a + b; }')
    $compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "$entry entry point compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 5) { throw "Incorrect $entry entry point result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { Point p {}; return add(p.value(), 2); }')
[IO.File]::WriteAllText($other, 'struct Point { int value() { return 3; } }; int add(int a, int b) { return a + b; }')
Push-Location $dir
try {
    foreach ($inputs in @(@($main, $other), @($other, $main))) {
        $compile = & $compiler -i $inputs[0] $inputs[1] 2>&1 | Out-String -Width 10000
        if ($LASTEXITCODE -ne 0) { throw "Default output compilation failed: $compile" }
        & '.\app.exe' | Out-Null
        if ($LASTEXITCODE -ne 5) { throw "Incorrect default output result: $LASTEXITCODE" }
    }
} finally { Pop-Location }
[IO.File]::WriteAllText($main, 'struct Outer { Inner item; }; int main() { Outer o {}; return o.item.n; }')
[IO.File]::WriteAllText($other, 'struct Inner { int n = 4; };')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Cross-file layout compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 4) { throw "Incorrect cross-file layout result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'struct Item { int value = seed(); }; int main() { Item item {}; return item.value; }')
[IO.File]::WriteAllText($other, 'int seed() { return 11; }')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Cross-file field initializer failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 11) { throw "Incorrect cross-file initializer result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { Point p {}; Point q = copy(p); return q.value; }')
[IO.File]::WriteAllText($other, 'struct Point { int value = 12; }; Point copy(Point p) { return p; }')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Cross-file user type signature failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 12) { throw "Incorrect cross-file type result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { A a = A(7); return a.get(); }')
[IO.File]::WriteAllText($other, 'struct A { int x; A(int n) { x = n; } ~A() {} int get() { return x; } };')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Cross-file lifecycle compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 7) { throw "Incorrect cross-file lifecycle result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { A a = make(); return use(A(2)) + a.get(); }')
[IO.File]::WriteAllText($other, 'struct A { int x; A(int n) { x = n; } ~A() {} int get() { return x; } }; A make() { return A(5); } int use(A a) { return a.get(); }')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Cross-file ownership compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 7) { throw "Incorrect cross-file ownership result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { inc(); return get(); }')
[IO.File]::WriteAllText($other, 'int mutable value = 2 + 3; void inc() { value += 1; } int get() { return value; }')
foreach ($inputs in @(@($main, $other), @($other, $main))) {
    $compile = & $compiler -i $inputs[0] $inputs[1] -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Global compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 6) { throw "Incorrect global result: $LASTEXITCODE" }
}
[IO.File]::WriteAllText($main, 'int main() { int getLocal() { return value; } return getLocal(); }')
[IO.File]::WriteAllText($other, 'int value = 7;')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Nested global access failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 7) { throw "Incorrect nested global result: $LASTEXITCODE" }
[IO.File]::WriteAllText($main, 'int main() { if (a || !b || c || d || !e || !f || g || h) { return 1; } return 0; }')
[IO.File]::WriteAllText($other, 'bool a = false && (1 / 0 == 0); bool b = true || (1 / 0 == 0); bool c = 3 < 2 < 1 / 0; bool d = (1.0 > 2.0) && (1 / 0 == 0); bool e = (1.0 / 0.0 > 0.0) || (1 / 0 == 0); bool f = false !&& (1 / 0 == 0); bool g = true !|| (1 / 0 == 0); bool h = false && ((1 << 32) == 0);')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Short-circuit global initialization failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Incorrect short-circuit global result: $LASTEXITCODE" }
function Test-Rejected($a, $b, $expected) {
    [IO.File]::WriteAllText($main, $a)
    [IO.File]::WriteAllText($other, $b)
    $ErrorActionPreference = 'Continue'
    $result = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($expected)) { throw "Expected $expected`: $result" }
}
function Test-PreIr($a, $b, $expected) {
    [IO.File]::WriteAllText($main, $a)
    [IO.File]::WriteAllText($other, $b)
    $ErrorActionPreference = 'Continue'
    $result = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($expected) -or $result.Contains('Generating IR')) {
        throw "Expected pre-IR error $expected`: $result"
    }
}
Test-Rejected 'int main() { return missing(); }' 'int f() { return 1; }' 'Undefined function'
Test-Rejected 'int main() { return 0; } int f() { return 1; }' 'int f() { return 2; }' 'Duplicate function'
Test-Rejected 'int f() { return 0; }' 'int g() { return 0; }' 'exactly one explicit entry point'
Test-Rejected 'int main() { return 0; }' 'int start() { return 0; }' 'exactly one explicit entry point'
Test-Rejected 'int main() { A a {}; return a.x; }' 'class A { int x = 1; };' 'Private member'
Test-Rejected 'int main() { return 0; } int value = 1;' 'int value = 2;' 'Duplicate declaration'
Test-Rejected 'int main() { return 0; } int value = get();' 'int get() { return 1; }' 'constant expression'
Test-Rejected 'int main() { return 0; } int value = other;' 'int other = 1;' 'constant expression'
Test-Rejected 'int main() { value = 2; return 0; }' 'int value = 1;' 'Cannot modify immutable'
Test-Rejected 'int main() { int value = 3; int f() { return value; } return f(); }' 'int value = 2;' 'requires capture'
Test-Rejected 'int main() { return f(true); }' 'int f(int value) { return value; }' 'Argument type mismatch'
Test-PreIr 'int main() { A a {}; return a.x; }' 'class A { int x = 1; };' 'Private member'
Test-PreIr 'int main() { A a {}; a.secret(); return 0; }' 'class A { void secret() {} };' 'Private member'
Test-PreIr 'int main() { A a {}; a.x = 1; return 0; }' 'struct A { int mutable x; };' 'Cannot modify immutable'
Test-PreIr 'int main() { A a {}; a.run(); return 0; }' 'struct A { int mutable x; void run() { change(); } void change() { x += 1; } };' 'Cannot call mutating method'
Test-PreIr 'int main() { A a {}; return a.read(true); }' 'struct A { int read(int x) { return x; } };' 'Argument type mismatch'
Test-PreIr 'int main() { return missing(); }' 'int f() { return 1; }' 'Undefined function'
Test-PreIr 'int main() { return missing; }' 'int f() { return 1; }' 'Undefined variable'
Test-PreIr 'int main() { Missing value {}; return 0; }' 'int f() { return 1; }' 'Unknown type: Missing'
Test-PreIr 'int main() { return 0; }' 'int f(Missing value) { return 1; }' 'Unknown type: Missing'
$hidden = Join-Path $dir 'unlisted.c4r'
[IO.File]::WriteAllText($hidden, 'int hidden() { return 8; }')
Test-PreIr 'int main() { return hidden(); }' 'int unrelated() { return 1; }' 'Undefined function'
$compile = & $compiler -i $main $other $hidden -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Explicit third input failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 8) { throw "Incorrect explicit third input result: $LASTEXITCODE" }
Test-PreIr 'int main() { int x = 1; int f() { return x; } return f(); }' 'int unused() { return 0; }' 'requires capture'
Test-PreIr 'int main() { int x = 1; int f() { capture x; return x; } int g() { return f(); } return g(); }' 'int unused() { return 0; }' 'Capture is not available'
Test-PreIr 'int main() { if (1) { return 1; } return 0; }' 'int unused() { return 0; }' 'Condition must be bool'
Test-PreIr 'int main() { int x = true; return x; }' 'int unused() { return 0; }' 'Initializer type mismatch'
Test-PreIr 'int main() { A a {}; a.run; return 0; }' 'struct A { int mutable x; void run() { x += 1; } };' 'Cannot call mutating method'
Test-PreIr 'int main() { int f(int x) { return x; } return f(true); }' 'int unused() { return 0; }' 'Argument type mismatch'
Test-PreIr 'int main() { int x = if (true) { return 1; } else { return false; }; return x; }' 'int unused() { return 0; }' 'return types do not match'
Test-PreIr 'int main() { int x = if (true) { return 1; } else { int y = 2; }; return x; }' 'int unused() { return 0; }' 'must return'
Test-PreIr 'int main() { while (true) { int x = { break; }; } return 0; }' 'int unused() { return 0; }' 'Cannot leave a value scope'
Test-PreIr 'int main() { return x; }' 'int x = 1 / (3 - 3);' 'Invalid global constant initializer'
Test-PreIr 'int main() { A a {}; return a.n; }' 'struct A { int n = 1 / 0; };' 'Integer division or remainder by zero'
Test-PreIr 'int main() { int mutable n = 1; n %= 0; return n; }' 'int unused() { return 0; }' 'Integer division or remainder by zero'
Test-PreIr 'int main() { return (-2147483647 - 1) / -1; }' 'int unused() { return 0; }' 'Integer division or remainder overflow'
Test-PreIr 'int main() { return 1 << 32; }' 'int unused() { return 0; }' 'Integer shift count out of range'
Test-PreIr 'int main() { int mutable x = 1; x >>= -1; return x; }' 'int unused() { return 0; }' 'Integer shift count out of range'
Test-PreIr 'int main() { return x; }' 'int x = 1 << 32;' 'Integer shift count out of range'
Test-PreIr 'int main() { return x; }' 'int x = (-2147483647 - 1) % -1;' 'Invalid global constant initializer'
Test-PreIr 'int main() { return x; }' 'int x = 2 ` -1;' 'exponent must be nonnegative'
Test-PreIr 'int main() { return f(); }' 'int f() { int x = 1; }' 'Function must return a value'
Test-PreIr 'int main() { return 0; int x = 1; }' 'int unused() { return 0; }' 'Unreachable statement'
Test-PreIr 'int main() { int x = 1; int f() { { capture x; } return 1; } return f(); }' 'int unused() { return 0; }' 'capture must be declared directly'
Test-PreIr 'int main() { int x = 1; return x(); }' 'int unused() { return 0; }' 'Variable is not callable'
Test-PreIr 'int main() { return ++(1 + 2); }' 'int unused() { return 0; }' 'Assignment requires a variable'
Test-PreIr 'int main() { return 2 ` -1; }' 'int unused() { return 0; }' 'exponent must be nonnegative'
Test-PreIr 'int main() { A a {}; a.one(); return 0; }' 'struct A { int mutable x; void one() { two(); } void two() { if (false) { one(); } x += 1; } };' 'Cannot call mutating method'
Test-PreIr 'int main() { int f(void x) { return 0; } return 0; }' 'int unused() { return 0; }' 'Parameter cannot have void type'
Test-PreIr 'int main() { int x {}; return 0; }' 'int unused() { return 0; }' 'Brace initialization requires'
Test-PreIr 'int main() { A mutable a {}; A b {}; a = b; return 0; }' 'struct A { int x; };' 'Assignment to a user-defined value'
Test-PreIr 'int main() { return 2147483648; }' 'int unused() { return 0; }' 'Integer literal out of range'
$hugeFloat = ('9' * 400) + '.0'
Test-PreIr ('int main() { float x = ' + $hugeFloat + '; return 0; }') 'int unused() { return 0; }' 'Floating literal out of range'
foreach ($expr in @('make().x', 'make().get()')) {
    [IO.File]::WriteAllText($main, "int main() { return $expr; }")
    [IO.File]::WriteAllText($other, 'struct A { int x = 1; int get() { return x; } }; A make() { A a {}; return a; }')
    $compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "Temporary member compilation failed: $compile" }
    & $output | Out-Null
    if ($LASTEXITCODE -ne 1) { throw "Incorrect temporary member result: $LASTEXITCODE" }
}
Test-PreIr 'int main() { return 0; }' 'struct A { void f() {} void g() { void nested() { f(); } nested(); } };' 'Undefined function'
Test-Rejected 'int main() {' 'return 0; }' 'Unexpected EOF in block'
[IO.File]::WriteAllText($main, 'int main() { return 0; }')
[IO.File]::WriteAllText($other, 'int helper() { return 0; }')
$ErrorActionPreference = 'Continue'
$sameOutput = & $compiler -i $main $other -o $main 2>&1 | Out-String -Width 10000
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -eq 0 -or !$sameOutput.Contains('Output file is also an input file') -or [IO.File]::ReadAllText($main) -ne 'int main() { return 0; }') {
    throw "Input/output collision was not safely rejected: $sameOutput"
}
Test-PreIr 'int f() { return 0; }' 'int g() { return 0; }' 'exactly one explicit entry point'
Test-PreIr 'int main() { return 0; }' 'int start() { return 0; }' 'exactly one explicit entry point'
[IO.File]::WriteAllText($main, 'int main() { object.inc(); return object.read(); }')
[IO.File]::WriteAllText($other, 'struct Counter { int mutable n = 2; void inc() { n += 1; } int read() { return n; } }; Counter mutable object {};')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Global object compilation failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 3) { throw "Incorrect global object result: $LASTEXITCODE" }
[IO.File]::WriteAllText($main, 'int main() { Counter c {}; return c.read(); }')
[IO.File]::WriteAllText($other, 'int value = 9; struct Counter { int value = 3; int read() { return value; } };')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Field/global shadow compilation failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 3) { throw "Incorrect field/global shadow result: $LASTEXITCODE" }
[IO.File]::WriteAllText($main, 'int main() { A a {}; return a.read(); }')
[IO.File]::WriteAllText($other, 'int x = 9; struct A { int x = 3; int read() { int nested() { capture x; return x; } return nested(); } };')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Global capture with field shadow failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 9) { throw "Incorrect captured global result: $LASTEXITCODE" }
[IO.File]::WriteAllText($main, 'int main() { return use(4); }')
[IO.File]::WriteAllText($other, 'int value = 9; int use(int value) { int read() { capture value; return value; } { int value = 1; return read(); } }')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Resolved shadowed capture failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 4) { throw "Incorrect resolved shadowed capture result: $LASTEXITCODE" }
[IO.File]::WriteAllText($main, 'int main() { A a {}; return a.read(); }')
[IO.File]::WriteAllText($other, 'int value() { return 7; } struct A { int value() { return 3; } int read() { return value(); } };')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Resolved global function call failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 7) { throw "Incorrect resolved global function result: $LASTEXITCODE" }
[IO.File]::WriteAllText($other, 'struct A { int value() { return 3; } int read() { int value() { return 5; } return value(); } };')
$compile = & $compiler -i $main $other -o $output 2>&1 | Out-String -Width 10000
if ($LASTEXITCODE -ne 0) { throw "Resolved local function call failed: $compile" }
& $output | Out-Null
if ($LASTEXITCODE -ne 5) { throw "Incorrect resolved local function result: $LASTEXITCODE" }
Write-Host 'Multifile tests passed.'
