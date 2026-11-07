param([string]$compiler = "$PSScriptRoot/../build/Debug/C4R.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/type-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
function Test-Program($name, $source, $expected) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $output = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $output" }
    & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-Null
    if ($LASTEXITCODE -ne $expected) { throw "$name returned $LASTEXITCODE, expected $expected" }
}
function Test-Rejected($name, $source, $message) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $output = & $compiler $path 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or !$output.Contains($message)) { throw "$name should fail with $message`: $output" }
}
Test-Program independent 'struct Point { int mutable x = 4; int y; int get() { return x + y; } void inc() { x += 1; } }; int main() { Point mutable a {}; Point b {}; a.inc(); return a.get() + b.get(); }' 9
Test-Program access 'class A { int secret = 3; public: int a = 1; private int b = 2; int c = 4; private: int d = 5; public int e = 6; int f = 7; public int sum() { return secret + a + b + c + d + e + f; } }; int main() { A a {}; return a.a + a.c + a.e + a.sum(); }' 39
Test-Program nested 'struct Inner { int mutable n = 2; }; struct Outer { Inner mutable value; }; int main() { Outer mutable x {}; x.value.n += 3; return x.value.n; }' 5
Test-Program indirect 'class A { int mutable x = 1; void change() { x += 1; } public: void run() { change(); } int get() { return x; } }; int main() { A mutable a {}; a.run(); return a.get(); }' 2
Test-Program float_fields 'struct Vector2 { float x = 3.0; float y = 4.0; float lengthSquared() { return x * x + y * y; } }; int main() { Vector2 v {}; if (v.lengthSquared() == 25.0) { return 0; } return 1; }' 0
Test-Program omitted_parentheses 'struct A { int get() { return 3; } }; int main() { A a {}; return a.get; }' 3
Test-Rejected private_field 'class A { int x = 1; }; int main() { A a {}; return a.x; }' 'Private member'
Test-Rejected private_method 'struct A { private void f() {} }; int main() { A a {}; a.f(); return 0; }' 'Private member'
Test-Rejected tag_not_leaked 'class A { public: private int a; int b; }; int main() { A a {}; return a.a; }' 'Private member'
Test-Rejected immutable_instance 'struct A { int mutable x; }; int main() { A a {}; a.x = 1; return 0; }' 'Cannot modify immutable'
Test-Rejected immutable_field 'struct A { int x; }; int main() { A mutable a {}; a.x = 1; return 0; }' 'Cannot modify immutable'
Test-Rejected indirect_immutable 'struct A { int mutable x; void f() { x += 1; } void g() { f(); } }; int main() { A a {}; a.g(); return 0; }' 'Cannot call mutating method'
Test-Rejected private_tag_not_leaked 'class A { private: public int a; int b; }; int main() { A a {}; return a.b; }' 'Private member'
Test-Rejected recursive_field 'struct A { A a; }; int main() { return 0; }' 'incomplete field type'
Test-Rejected object_comparison 'struct A { int x; }; int main() { A a {}; A b {}; bool c = a == b; return 0; }' 'Unsupported operand type'
Test-Rejected object_assignment 'struct A { int x; }; int main() { A mutable a {}; A b {}; a = b; return 0; }' 'Assignment to a user-defined value'
Test-Rejected initializer_scope 'struct A { int x = value; }; int main() { int value = 3; A a {}; return a.x; }' 'Undefined variable'
Write-Host 'Type tests passed.'
