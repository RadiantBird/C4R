# 変数
変数は、
- `<TypeName> <Identifier>`
- `<TypeName> mutable <Identifier>`
- `const <TypeName> <Identifier>`

のあと、
`= <Value>`
で定義されます。
代入なしの初期化はまずは禁止とします。

良い例:
```
int32 i = 0;
int32 mutable apples = 12;
const int32 debug = 1;
```
悪い例:
```
int32 i;
int32 mutable apples;
const int32 debug;
```