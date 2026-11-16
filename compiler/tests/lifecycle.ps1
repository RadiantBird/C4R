param([string]$compiler = "$PSScriptRoot/../build/Debug/c4rc.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/lifecycle-tests'
New-Item -ItemType Directory -Force $dir | Out-Null

function Test-Rejected($name, $source, $message) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $ErrorActionPreference = 'Continue'
    $result = & $compiler --check $path 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($message)) { throw "$name should fail with $message`: $result" }
}
function Test-Program($name, $source, $expected) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $result = & $compiler $path 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $result" }
    $output = & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0 -or $output.Replace("`r`n", "`n") -ne $expected) { throw "$name output mismatch: $output" }
}

Test-Rejected duplicate_constructor 'struct A { A(int x) {} A(int32 y) {} }; int main() { return 0; }' 'Duplicate constructor signature'
Test-Rejected duplicate_destructor 'struct A { ~A() {} ~A() {} }; int main() { return 0; }' 'Duplicate destructor'
Test-Rejected destructor_parameter 'struct A { ~A(int x) {} }; int main() { return 0; }' 'Destructor cannot have parameters'
Test-Rejected destructor_name 'struct A { ~B() {} }; int main() { return 0; }' 'Special member name must match'
Test-Rejected no_implicit_constructor 'struct A {}; int main() { A a = A(); return 0; }' 'No matching constructor'
Test-Program apple 'class Apple { int mutable amount = 0; public: Apple(int amt) { amount = amt; "Apple constructed!\n"; } ~Apple() { "Apple destructed!\n"; } void fall() { if (amount > 0) { "ouch!\n"; amount--; } else { "no apple lol\n"; } } }; int main() { Apple mutable ap = Apple(3); ap.fall(); }' "Apple constructed!`nouch!`nApple destructed!`n"
Test-Program reverse_return 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int f() { A a = A(); { A b = A(); return 0; } } int main() { return f(); }' "C`nC`nD`nD`n"
Test-Program distinct_reverse_return 'struct A { ~A() { "A\n"; } }; struct B { ~B() { "B\n"; } }; int f() { A a {}; B b {}; return 0; } int main() { return f(); }' "B`nA`n"
Test-Program loop_cleanup 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { int mutable i = 0; while (i < 3) { A a = A(); i++; if (i == 1) { continue; } break; } return 0; }' "C`nD`nC`nD`n"
Test-Program overloads 'struct A { int mutable x = 0; A() { x = 1; } A(int n) { x = n; } int get() { return x; } }; int main() { A a {}; A b = A(2); if (a.get() == 1 && b.get() == 2) { "OK\n"; } return 0; }' "OK`n"
Test-Program private_constructor_internal 'class A { A() { "private\n"; } public: A(int n) {} void use() { A a = A(); "use\n"; } }; int main() { A a = A(1); a.use(); return 0; }' "private`nuse`n"
Test-Program member_order 'struct M { ~M() { "M\n"; } }; struct O { M first; M second; ~O() { "O\n"; } }; int main() { O o {}; return 0; }' "O`nM`nM`n"
Test-Program distinct_member_order 'struct A { ~A() { "A\n"; } }; struct B { ~B() { "B\n"; } }; struct O { A first; B second; ~O() { "O\n"; } }; int main() { O o {}; return 0; }' "O`nB`nA`n"
Test-Program member_constructor 'struct M { M() { "M+\n"; } ~M() { "M-\n"; } }; struct O { M m; O() { "O+\n"; } ~O() { "O-\n"; } }; int main() { O o = O(); return 0; }' "M+`nO+`nO-`nM-`n"
Test-Program field_explicit_constructor 'struct M { M(int x) { "M+\n"; } ~M() { "M-\n"; } }; struct O { M m = M(1); ~O() { "O-\n"; } }; int main() { O o {}; return 0; }' "M+`nO-`nM-`n"
Test-Program argument_before_field 'int arg() { "arg\n"; return 1; } int seed() { "field\n"; return 2; } struct A { int x = seed(); A(int n) { "body\n"; } }; int main() { A a = A(arg()); return 0; }' "arg`nfield`nbody`n"
Test-Program initialization_order 'int first() { "arg1\n"; return 1; } int second() { "arg2\n"; return 2; } int field1() { "field1\n"; return 1; } int field2() { "field2\n"; return 2; } struct A { int a = field1(); int b = field2(); A(int x, int y) { "body\n"; } }; int main() { A a = A(first(), second()); return 0; }' "arg1`narg2`nfield1`nfield2`nbody`n"
Test-Program temporary_method 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 1; } }; int main() { if (A().get() == 1) { "OK\n"; } return 0; }' "C`nD`nOK`n"
Test-Program temporary_short_circuit 'struct A { A() { "C\n"; } ~A() { "D\n"; } bool get() { return true; } }; int main() { if (false && A().get()) { "bad\n"; } if (true && A().get()) { "OK\n"; } return 0; }' "C`nD`nOK`n"
Test-Program temporary_statement 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { A(); "after\n"; return 0; }' "C`nD`nafter`n"
Test-Program temporary_for_init 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { for (A(); false;) {} "after\n"; return 0; }' "C`nD`nafter`n"
Test-Program temporary_loop 'struct A { A() { "C\n"; } ~A() { "D\n"; } bool get() { return true; } }; int main() { int mutable i = 0; while (i < 3) { if ((i == 1) && A().get()) { "yes\n"; } i++; } return 0; }' "C`nD`nyes`n"
Test-Program temporary_reverse 'struct A { A() { "A+\n"; } ~A() { "A-\n"; } int get() { return 1; } }; struct B { B() { "B+\n"; } ~B() { "B-\n"; } int get() { return 2; } }; int main() { int n = A().get() + B().get(); return 0; }' "A+`nB+`nB-`nA-`n"
Test-Program parameter_ownership 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void use(A a) { "use\n"; } int main() { use(A()); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
Test-Program parameter_method_constructor 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; struct B { B(A a) { "ctor\n"; } void use(A a) { "method\n"; } }; int main() { B b = B(A()); b.use(A()); return 0; }' "C`nctor`nD`nC`nmethod`nD`n"
Test-Rejected parameter_copy 'struct A { ~A() {} }; void use(A a) {} int main() { A a {}; use(a); return 0; }' 'requires direct construction'
Test-Program direct_return 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 4; } }; A make() { return A(); } int main() { A a = make(); if (a.get() == 4) { "use\n"; } return 0; }' "C`nuse`nD`n"
Test-Program returned_temporary 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 4; } }; A make() { return A(); } int main() { if (make().get() == 4) { "use\n"; } return 0; }' "C`nD`nuse`n"
Test-Program method_return 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; struct B { A make() { return A(); } }; int main() { B b {}; A a = b.make(); "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program return_to_parameter 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } void use(A a) { "use\n"; } int main() { use(make()); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
Test-Program return_to_field 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } struct B { A a = make(); ~B() { "B\n"; } }; int main() { B b {}; "use\n"; return 0; }' "C`nuse`nB`nD`n"
Test-Program return_short_circuit 'struct A { A() { "C\n"; } ~A() { "D\n"; } bool get() { return true; } }; A make() { return A(); } int main() { if (false && make().get()) { "bad\n"; } if (true && make().get()) { "ok\n"; } return 0; }' "C`nD`nok`n"
Test-Program parameter_reverse 'struct A { A() { "A+\n"; } ~A() { "A-\n"; } }; struct B { B() { "B+\n"; } ~B() { "B-\n"; } }; void use(A a, B b) { "use\n"; return; } int main() { use(A(), B()); "after\n"; return 0; }' "A+`nB+`nuse`nB-`nA-`nafter`n"
Test-Program parameter_and_local_order 'struct A { A() { "A+\n"; } ~A() { "A-\n"; } }; struct B { ~B() { "B-\n"; } }; void use(A a) { B b {}; "use\n"; } int main() { use(A()); "after\n"; return 0; }' "A+`nuse`nB-`nA-`nafter`n"
Test-Program return_cleans_locals 'struct A { A() { "A+\n"; } ~A() { "A-\n"; } }; struct B { ~B() { "B-\n"; } }; A make() { B b {}; return A(); } int main() { A a = make(); "use\n"; return 0; }' "A+`nB-`nuse`nA-`n"
Test-Rejected return_copy 'struct A { ~A() {} }; A make() { A a {}; return a; } int main() { return 0; }' 'requires direct construction'
Test-Rejected private_return_destructor 'class A { ~A() {} public: A() {} A make() { return A(); } }; int main() { A a = A().make(); return 0; }' 'Private destructor'
Test-Rejected global_destructor 'struct A { ~A() {} }; A a {}; int main() { return 0; }' 'Global variables with destructors'
Test-Rejected nested_destructible_copy 'struct A { ~A() {} }; struct B { A a; }; int main() { B a {}; B b = a; return 0; }' 'Copying a destructible value'
Test-Program value_scope_destructible 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 1; } }; int main() { int n = (if (true) { return A(); } else { return A(); }).get(); "after\n"; return 0; }' "C`nD`nafter`n"
Test-Program value_scope_branch 'struct A { A() { "A+\n"; } ~A() { "A-\n"; } int get() { return 1; } }; struct B { ~B() { "B-\n"; } }; int main() { int n = (if (false) { return A(); } else { B b {}; return A(); }).get(); "after\n"; return 0; }' "A+`nB-`nA-`nafter`n"
Test-Program value_scope_loop 'struct A { A() { "C\n"; } ~A() { "D\n"; } bool get() { return true; } }; int main() { int mutable i = 0; while (i < 2) { if ((i == 1) && (if (true) { return A(); } else { return A(); }).get()) { "yes\n"; } i++; } return 0; }' "C`nD`nyes`n"
Test-Program value_scope_to_local 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { A a = if (true) { return A(); } else { return A(); }; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program value_scope_to_argument 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void use(A a) { "use\n"; } int main() { use(if (false) { return A(); } else { return A(); }); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
Test-Program value_scope_call 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 1; } }; A make() { return A(); } int main() { int x = (if (true) { return make(); } else { return A(); }).get(); "after\n"; return 0; }' "C`nD`nafter`n"
Test-Program value_scope_nested 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { A a = if (true) { return if (false) { return A(); } else { return A(); }; } else { return A(); }; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program value_scope_field 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } struct B { A a = if (true) { return make(); } else { return A(); }; ~B() { "B\n"; } }; int main() { B b {}; "use\n"; return 0; }' "C`nuse`nB`nD`n"
Test-Program local_capture_return 'struct A { int x; A(int n) { x = n; "C\n"; } ~A() { "D\n"; } int get() { return x; } }; int main() { int n = 3; A make() { capture n; return A(n); } A a = make(); if (a.get() == 3) { "use\n"; } return 0; }' "C`nuse`nD`n"
Test-Program bare_return 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } int main() { A a = make; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program bare_method_return 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; struct B { A make() { return A(); } }; int main() { B b {}; A a = b.make; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program bare_parameter 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } void use(A a) { "use\n"; } int main() { use(make); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
Test-Program bare_field 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } struct B { A a = make; ~B() { "B\n"; } }; int main() { B b {}; "use\n"; return 0; }' "C`nuse`nB`nD`n"
Test-Program bare_temporary_receiver 'struct A { A() { "C\n"; } ~A() { "D\n"; } int get() { return 1; } }; A make() { return A(); } int main() { if (make.get() == 1) { "use\n"; } return 0; }' "C`nD`nuse`n"
Test-Program bare_implicit_method 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; struct B { A make() { return A(); } void use() { A a = make; "use\n"; } }; int main() { B b {}; b.use(); return 0; }' "C`nuse`nD`n"
Test-Program bare_value_scope 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { return A(); } int main() { A a = if (true) { return make; } else { return A(); }; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program bare_local_capture 'struct A { A(int n) { "C\n"; } ~A() { "D\n"; } }; int main() { int n = 1; A make() { capture n; return A(n); } A a = make; "use\n"; return 0; }' "C`nuse`nD`n"
Test-Rejected bare_shadow_copy 'struct A { A() {} ~A() {} }; A make() { return A(); } int main() { A make {}; A copy = make; return 0; }' 'Copying a destructible value'
Test-Rejected bare_member_copy 'struct A { ~A() {} }; struct B { A a; }; int main() { B b {}; A a = b.a; return 0; }' 'Copying a destructible value'
Test-Rejected value_scope_copy 'struct A { ~A() {} int get() { return 1; } }; int main() { A a {}; int n = ({ return a; }).get(); return n; }' 'requires direct construction'
Test-Rejected temporary_mutation 'struct A { int mutable x; A() {} bool set() { x = 1; return true; } }; int main() { if (A().set()) {} return 0; }' 'immutable'
Test-Program immutable_init 'struct A { int x = 0; A(int n) { x = n; } int get() { return x; } }; int main() { A a = A(3); if (a.get() == 3) { "OK\n"; } return 0; }' "OK`n"
Test-Program trivial_assignment 'struct P { int mutable x = 0; }; int main() { P mutable a {}; P mutable b {}; a.x = 3; b = a; a.x = 4; if (b.x == 3) { "OK\n"; } return 0; }' "OK`n"
Test-Rejected private_destructor 'class A { ~A() {} public: A() {} }; int main() { A a = A(); return 0; }' 'Private destructor'
Test-Rejected destructible_copy 'struct A { ~A() {} }; int main() { A a {}; A b = a; return 0; }' 'Copying a destructible value'
Test-Rejected missing_default 'struct A { A(int n) {} }; int main() { A a {}; return 0; }' 'No matching constructor'
Test-Rejected private_constructor 'class A { A() {} }; int main() { A a = A(); return 0; }' 'Private constructor'
Test-Rejected wrong_argument 'struct A { A(int n) {} }; int main() { A a = A(true); return 0; }' 'No matching constructor'
Test-Rejected missing_argument 'struct A { A(int n) {} }; int main() { A a = A(); return 0; }' 'No matching constructor'
Test-Rejected extra_argument 'struct A { A(int n) {} }; int main() { A a = A(1, 2); return 0; }' 'No matching constructor'
Test-Rejected nested_private_destructor 'class M { ~M() {} }; struct O { M m; }; int main() { O o {}; return 0; }' 'Private destructor'
Test-Rejected immutable_instance 'struct P { int mutable x; }; int main() { P a {}; P b {}; a = b; return 0; }' 'Cannot modify immutable'
Test-Rejected immutable_field 'struct P { int x; }; int main() { P mutable a {}; P b {}; a = b; return 0; }' 'immutable field'
Test-Rejected private_field_assignment 'class P { int mutable x; }; int main() { P mutable a {}; P b {}; a = b; return 0; }' 'Private member'
Test-Rejected destructible_assignment 'struct A { int mutable x; ~A() {} }; int main() { A mutable a {}; A b {}; a = b; return 0; }' 'Copying a destructible value'
Test-Rejected member_missing_constructor 'struct M { M(int x) {} }; struct O { M m; }; int main() { O o {}; return 0; }' 'No matching constructor'
Test-Rejected member_private_constructor 'class M { M() {} }; struct O { M m; }; int main() { O o {}; return 0; }' 'Private constructor'
Test-Rejected unsupported_inheritance 'class A {}; class B : public A {}; int main() { return 0; }' 'Unexpected token'
Test-Rejected unsupported_virtual 'struct A { virtual void f() {} }; int main() { return 0; }' 'Unknown type: virtual'
Test-Rejected unsupported_block_destruct 'int main() { {} destruct {} return 0; }' 'Unexpected token'
Write-Host 'Lifecycle tests passed.'
