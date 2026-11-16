# Ownership, borrowing, and lifetimes

## Ownership transfer

`move(value)` explicitly transfers a variable's value. The source becomes moved and cannot be read or moved again until it is reinitialized. Field moves and moves from globals are rejected. Destructible values can be passed or returned only through direct construction or explicit `move`; named values are not implicitly copied or moved. Ownership is tracked per function through branches and loops, and the LLVM cleanup flag follows the analyzed move state so that exactly one live owner runs a destructor.

`T&&` is C4R's consumable-value marker. A consumable parameter, local, or return requires a direct value expression or explicit `move`. Parameters receive an owned value and follow the ordinary value ABI; no special runtime object is introduced. A consumable parameter may be read repeatedly until explicitly moved. Global and field consumables are not supported and are rejected.

## Borrowing

`T&` creates a shared borrow and `T mutable&` an exclusive borrow. Shared borrows may coexist. An exclusive borrow prevents all access through the owner or another alias while it is live; a shared borrow prevents writes and exclusive borrows. Borrowed parameters and local aliases do not own or destroy their referents. A move of a borrowed binding or of an owner with a live borrow is rejected. Reference fields and global reference variables are currently rejected.

Borrow scopes are lexical. Local aliases remain live to the end of their declaring block; call-site argument borrows last for that call. These rules are checked before IR generation and add no runtime borrow tracking.

## Lifetimes and raw pointers

Reference-returning functions require explicit lifetime identifiers on every reference parameter and on the return type. The return lifetime must match exactly one reference parameter; direct returns and chains of such calls are checked against that source. Returning a local or otherwise unrelated object is rejected. More general lifetime relations, lifetime bounds, and inferred lifetime relationships across multiple inputs are not implemented.

Raw `T*` values are not ownership-bearing. Direct addresses of globals and parameter pointers can be returned. Direct addresses of local variables, local pointer aliases, and pointer results with unknown provenance are conservatively rejected when returned. This is not a complete proof of raw-pointer safety: dereference, pointer arithmetic, and full path-sensitive pointer provenance are not implemented. Unknown pointer provenance is rejected at return rather than treated as safe.

## Diagnostics and unsupported operations

Moves of fields and globals, reference fields, global references, and consumable fields are explicitly diagnosed as unsupported. Destructible globals remain unsupported. Borrow and lifetime checking is compile-time only; no reference counting or garbage collection is generated.
