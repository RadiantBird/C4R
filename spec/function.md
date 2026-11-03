# 関数

C4Rの関数は、基本的にC/C++に近い構文を使用します。

```cpp
int32 add(int32 a, int32 b) {
    return a + b;
}
```

戻り値の型、関数名、引数、関数本体の順に記述します。

---

## 関数呼び出し

通常の関数呼び出しはC/C++と同様です。

```cpp
int32 result = add(10, 20);
```

引数が存在しない関数は、括弧を省略できます。

```cpp
void hello() {
    "Hello!\n";
}

hello;
```

もちろん通常通り、

```cpp
hello();
```

と書くこともできます。

---

## return

`return`を使用して関数から値を返します。

```cpp
int32 answer() {
    return 42;
}
```

`void`関数では値を返す必要はありません。

```cpp
void hello() {
    "Hello!\n";
}
```

C4Rでは制御構文が式として評価されている場合、`return`は現在評価中の値スコープを終了します。

値スコープが存在しない場合は、現在の関数から値を返します。

```cpp
int32 foo() {
    if (error) {
        return -1;
    }

    return 10;
}
```

この`if`は値を要求されていないため文として扱われ、`return -1`は`foo()`から値を返します。

[詳細](control.md)
---

## ローカル関数

C4Rでは、関数の中に関数を宣言できます。

```cpp
int32 main() {
    int32 alt() {
        return 10;
    }

    return alt();
}
```

ローカル関数は、その関数が宣言されたスコープ内でのみ使用できます。

関数のネスト階層に制限はありません。

```cpp
int32 a() {
    int32 b() {
        int32 c() {
            return 42;
        }

        return c();
    }

    return b();
}
```

関数も他の宣言と同様に、レキシカルスコープに従います。

---

## capture

ローカル関数から外側の関数にある変数を参照する場合、`capture`宣言が必要です。

```cpp
int32 foo() {
    int32 x = 10;

    int32 bar() {
        capture x;

        return x;
    }

    return bar();
}
```

複数の変数をキャプチャできます。

```cpp
int32 foo() {
    int32 x = 10;
    int32 y = 20;

    int32 add() {
        capture x, y;

        return x + y;
    }

    return add();
}
```

外側の関数スコープにある変数を`capture`なしで使用することはできません。

```cpp
int32 foo() {
    int32 x = 10;

    int32 bar() {
        return x; // error
    }

    return bar();
}
```

`capture`は名前の属するスコープを指定するための機能ではありません。

変数の名前解決は通常のレキシカルスコープに従います。

`capture`は、ローカル関数が外側の関数の実行環境を参照することを明示するための宣言です。

これにより、外側の変数を参照するための追加処理が暗黙的に発生することを防ぎます。

キャプチャを行わないローカル関数には、このための追加処理は必要ありません。

---

## エントリーポイント

`main`、`start`、`entry`など、エントリーポイントとして認識される名前を持ち、`int`を返す関数はプログラムのエントリーポイントとして使用できます。

```cpp
int main() {
    "Hello, world!\n";
    return 0;
}
```

明示的なエントリーポイントが存在しない場合、そのファイル全体をエントリーポイントとして実行またはコンパイルできます。

```cpp
"Hello, world!\n";
```

[詳細](runtime.md)