param([string]$compiler = "$PSScriptRoot/../build/Debug/c4rc.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/ownership-tests'
New-Item -ItemType Directory -Force $dir | Out-Null

function Test-Diagnostic($name, $source, $expected) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $ErrorActionPreference = 'Continue'
    $result = & $compiler --check $path 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -eq 0 -or !$result.Contains($expected)) { throw "$name expected $expected`: $result" }
}

Test-Diagnostic moved_read 'int main() { int x = 1; int y = move(x); return x; }' 'may have already been moved'
Test-Diagnostic moved_twice 'int main() { int x = 1; int y = move(x); int z = move(x); return z; }' 'may have already been moved'
Test-Diagnostic moved_branch 'int f(bool c) { int x = 1; if (c) { int y = move(x); } return x; } int main() { return 0; }' 'may have already been moved'
Test-Diagnostic moved_loop 'int f(bool c) { int x = 1; while (c) { int y = move(x); } return 0; } int main() { return 0; }' 'may have already been moved'
Test-Diagnostic field_move 'struct A { int x; }; int main() { A a {}; int y = move(a.x); return y; }' 'field moves are not supported'
Test-Diagnostic moved_format 'int main() { int x = 1; int y = move(x); f"{x}\n"; return y; }' 'may have already been moved'
function Test-Program($name, $source, $expected) {
    $path = Join-Path $dir "$name.c4r"
    [IO.File]::WriteAllText($path, $source)
    $result = & $compiler $path 2>&1 | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed: $result" }
    $output = & ([IO.Path]::ChangeExtension($path, '.exe')) | Out-String -Width 10000
    if ($LASTEXITCODE -ne 0 -or $output.Replace("`r`n", "`n") -ne $expected) { throw "$name output mismatch: $output" }
}
Test-Program move_scalar 'int main() { int x = 1; int y = move(x); if (y == 1) { "OK\n"; } return 0; }' "OK`n"
Test-Program moved_format_valid 'int main() { int x = 1; int y = move(x); f"{y}\n"; return 0; }' "1`n"
Test-Program move_object 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { A a = A(); A b = move(a); "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program return_move 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; A make() { A a = A(); return move(a); } int main() { A a = make(); "use\n"; return 0; }' "C`nuse`nD`n"
Test-Program parameter_move 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void use(A a) { "use\n"; } int main() { A a = A(); use(move(a)); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
Test-Program reinitialize 'struct A { int id; A(int n) { id = n; "C\n"; } ~A() { "D\n"; } int get() { return id; } }; int main() { A mutable a = A(1); A b = move(a); a = A(2); if (a.get() == 2 && b.get() == 1) { "use\n"; } return 0; }' "C`nC`nuse`nD`nD`n"
Test-Program conditional_move 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void f(bool c) { A a = A(); if (c) { A b = move(a); "inner\n"; } "outer\n"; } int main() { f(true); f(false); return 0; }' "C`ninner`nD`nouter`nC`nouter`nD`n"
Test-Program both_branches_reinitialize 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void f(bool c) { A mutable a = A(); if (c) { A b = move(a); } else { A b = move(a); } a = A(); "use\n"; } int main() { f(true); f(false); return 0; }' "C`nD`nC`nuse`nD`nC`nD`nC`nuse`nD`n"
Test-Program move_then_break 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; int main() { A a = A(); while (true) { A b = move(a); "inner\n"; break; } "outer\n"; return 0; }' "C`ninner`nD`nouter`n"
Test-Program value_if_move 'struct A { A() { "C\n"; } ~A() { "D\n"; } }; void f(bool c) { A a = A(); A b = if (c) { return move(a); } else { return A(); }; "use\n"; } int main() { f(true); f(false); return 0; }' "C`nuse`nD`nC`nC`nuse`nD`nD`n"
Test-Diagnostic value_if_moved_read 'int f(bool c) { int x = 1; int y = if (c) { return move(x); } else { return 2; }; return x; } int main() { return 0; }' 'may have already been moved'
Test-Diagnostic reinitialize_available 'struct A { A() {} ~A() {} }; int main() { A mutable a = A(); a = A(); return 0; }' 'target is not definitely moved'
Test-Diagnostic reinitialize_maybe_moved 'struct A { A() {} ~A() {} }; int f(bool c) { A mutable a = A(); if (c) { A b = move(a); } a = A(); return 0; } int main() { return 0; }' 'target is not definitely moved'
Write-Host 'Ownership diagnostic tests passed.'
Test-Program nested_no_capture 'int main() { int id(int n) { return n; } int x = 1; int y = move(x); if (id(y) == 1) { "OK\n"; } return 0; }' "OK`n"
Test-Diagnostic nested_capture 'int main() { int x = 1; int read() { capture x; return x; } int y = move(x); return read(); }' 'captured variables'
Test-Diagnostic move_global 'int x = 1; int main() { int y = move(x); return y; }' 'Moving global variables is not implemented yet'
Test-Diagnostic move_borrowed_parameter 'int consume(int& value) { return move(value); } int main() { int x = 1; return consume(x); }' 'Cannot move a borrowed parameter'
Test-Diagnostic mutable_borrow_immutable 'void update(int mutable& value) { value += 1; } int main() { int value = 1; update(value); return 0; }' 'Mutable reference requires a mutable object'
Test-Diagnostic conflicting_borrows 'void inspect(int mutable& left, int& right) { left += right; } int main() { int mutable value = 1; inspect(value, value); return 0; }' 'Conflicting borrows'
Test-Diagnostic move_borrowed_owner 'int main() { int value = 1; int& borrow = value; int moved = move(value); return moved; }' 'while it is borrowed'
Test-Diagnostic use_owner_during_mutable_borrow 'int main() { int mutable value = 1; int mutable& borrow = value; value = 2; return borrow; }' 'mutably borrowed'
Test-Diagnostic modify_owner_during_shared_borrow 'int main() { int mutable value = 1; int& borrow = value; value = 2; return borrow; }' 'immutably borrowed'
Test-Diagnostic move_borrowed_local 'int main() { int value = 1; int& borrow = value; int moved = move(borrow); return moved; }' 'Cannot move a borrowed variable'
Test-Diagnostic mutable_local_borrow_immutable 'int main() { int value = 1; int mutable& borrow = value; return borrow; }' 'Mutable borrow requires a mutable object'
Test-Diagnostic borrow_temporary 'void inspect(int& value) {} int main() { inspect(1); return 0; }' 'Reference argument requires an addressable object'
Test-Diagnostic reference_return_local 'int&@a invalid(int&@a value) { int local = 3; return local; } int main() { int x = 1; return invalid(x); }' 'expired or unrelated object'
Test-Diagnostic move_reference_return_alias 'int&@a identity(int&@a value) { return value; } int main() { int mutable value = 1; int& alias = identity(value); int moved = move(value); return alias; }' 'while it is borrowed'
Test-Diagnostic reference_return_without_source 'int&@a invalid(int&@b value) { return value; } int main() { int x = 1; return invalid(x); }' 'Return lifetime is not tied to a reference parameter'
Test-Diagnostic ambiguous_reference_lifetime 'int&@a choose(int&@a first, int&@a second) { return first; } int main() { int x = 1; return choose(x, x); }' 'exactly one reference parameter'
Test-Diagnostic pointer_return_local 'int* invalid() { int value = 1; return &value; } int main() { return 0; }' 'expired local object'
Test-Diagnostic pointer_return_local_alias 'int* invalid() { int value = 1; int* pointer = &value; return pointer; } int main() { return 0; }' 'expired local object'
Test-Diagnostic pointer_to_value_parameter 'int* invalid(int value) { return &value; } int main() { int value = 1; invalid(value); return 0; }' 'expired local object'
Test-Diagnostic pointer_return_escaped_branch_alias 'int global = 0; int* invalid(bool c) { int* mutable pointer = &global; if (c) { int local = 1; pointer = &local; } return pointer; } int main() { return 0; }' 'expired local object'
Test-Diagnostic pointer_return_unknown_origin 'int global = 1; int* source() { return &global; } int* invalid() { int* pointer = source(); return pointer; } int main() { return 0; }' 'expired local object'
Test-Diagnostic consumable_requires_move 'void consume(int&& value) {} int main() { int value = 1; consume(value); return 0; }' 'Consumable argument requires direct construction or explicit move'
Test-Diagnostic consumable_global 'int&& value = 1; int main() { return 0; }' 'Global consumables are not implemented yet'
Test-Diagnostic consumable_use_after_move 'void consume(int&& value) { int other = move(value); int bad = value; } int main() { consume(1); return 0; }' 'may have already been moved'
Test-Diagnostic method_mutation_during_shared_borrow 'struct Box { int mutable value = 0; public: void increment() { value += 1; } }; int main() { Box mutable box {}; Box& borrow = box; box.increment(); return borrow.value; }' 'Cannot call mutating method on immutable instance'
Test-Program immutable_borrow 'void inspect(int& value) { if (value == 7) { "read\n"; } } int main() { int value = 7; inspect(value); return 0; }' "read`n"
Test-Program mutable_borrow 'void update(int mutable& value) { value += 1; } int main() { int mutable value = 7; update(value); if (value == 8) { "updated\n"; } return 0; }' "updated`n"
Test-Program local_immutable_borrow 'int main() { int mutable value = 7; int& first = value; int& second = value; if (first == second) { "shared\n"; } return 0; }' "shared`n"
Test-Program local_mutable_borrow 'int main() { int mutable value = 7; int mutable& borrow = value; borrow += 1; if (borrow == 8) { "local\n"; } return 0; }' "local`n"
Test-Program borrow_ends_at_block 'int main() { int mutable value = 7; { int mutable& borrow = value; borrow += 1; } value += 1; if (value == 9) { "scope\n"; } return 0; }' "scope`n"
Test-Program mutable_reference_to_object 'struct Box { int mutable value = 0; public: void increment() { value += 1; } }; void update(Box mutable& box) { box.increment(); } int main() { Box mutable box {}; update(box); if (box.value == 1) { "object\n"; } return 0; }' "object`n"
Test-Program borrowed_destructible_is_not_owned 'struct Resource { Resource() { "C\n"; } ~Resource() { "D\n"; } }; void inspect(Resource& value) { "I\n"; } int main() { Resource value = Resource(); Resource& alias = value; inspect(alias); "A\n"; return 0; }' "C`nI`nA`nD`n"
Test-Program move_after_call_borrow 'int inspect(int& value) { return value; } int main() { int value = 9; inspect(value); int result = move(value); if (result == 9) { "released\n"; } return 0; }' "released`n"
Test-Program reference_return 'int&@a identity(int&@a value) { return value; } int main() { int mutable value = 12; int& alias = identity(value); if (alias == 12) { "lifetime\n"; } return 0; }' "lifetime`n"
Test-Program reference_return_chain 'int&@a identity(int&@a value) { return value; } int&@a forward(int&@a value) { return identity(value); } int main() { int mutable value = 12; int& alias = forward(value); if (alias == 12) { "chain\n"; } return 0; }' "chain`n"
Test-Program mutable_reference_return 'int mutable&@a identity(int mutable&@a value) { return value; } int main() { int mutable value = 12; int mutable& alias = identity(value); alias += 1; if (value == 13) { "mutable lifetime\n"; } return 0; }' "mutable lifetime`n"
Test-Program pointer_argument_and_return 'int* identity(int* value) { return value; } int main() { int value = 12; int* pointer = identity(&value); return 0; }' ''
Test-Program pointer_to_global 'int global = 12; int* address() { return &global; } int main() { int* pointer = address(); return 0; }' ''
Test-Program consumable_scalar 'void consume(int&& value) { if (value == 9) { "consumed\n"; } } int main() { int value = 9; consume(move(value)); consume(5 + 4); return 0; }' "consumed`nconsumed`n"
Test-Program consumable_local 'int main() { int source = 11; int mutable&& token = move(source); if (token == 11) { "readable\n"; } int result = move(token); if (result == 11) { "transferred\n"; } return 0; }' "readable`ntransferred`n"
Test-Program consumable_return 'int&& take(int value) { return move(value); } int main() { int result = take(6); if (result == 6) { "returned\n"; } return 0; }' "returned`n"
Test-Program consumable_object 'struct Resource { Resource() { "C\n"; } ~Resource() { "D\n"; } }; void consume(Resource&& value) { "use\n"; } int main() { Resource value = Resource(); consume(move(value)); "after\n"; return 0; }' "C`nuse`nD`nafter`n"
