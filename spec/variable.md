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
```cpp
int32 i = 0;
int32 mutable apples = 12;
const int32 debug = 1;
```

悪い例:
```cpp
int32 i;
int32 mutable apples;
const int32 debug;
```

明示的なエントリーポイントを持つプログラムでは、トップレベルの変数はグローバル変数です。
入力ファイルが複数ある場合も、すべてのファイルから参照できます。
グローバル変数の初期値には、リテラルと定数式を使用します。関数呼び出しや他の変数への参照は使用できません。
これにより、入力ファイルの指定順に依存する初期化順序は発生しません。

明示的なエントリーポイントのない単一ファイルでは、従来どおりファイル全体を暗黙のエントリーポイントとして扱います。
