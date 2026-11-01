# The C4R Programming Language

C系言語のいいところとRustみたいな安全性をブレンドした言語。
- C
- C++
- C#
- C† (HolyC)
- Rust
-> C4R <- :DD 

Better C++をモットーにデザイン。
合言葉は"*Let's Get Control!*"のつもり。

---

# 思想

## Garbage Collection is just a garbage (ガベージコレクションはただのゴミ機能)

GCはなしです。手動でやりましょう。
でも、後述のしくみで自動にはできる。<- RAIIじゃん。

## Resource Acquisition Is Initialization

スコープで解放、コンストラクタからデストラクタまで。
ブロックは重要。
しかし、C++のように勝手に仕事をすることはしません。
コンストラクタからデストラクタまでを実現するには、
1. コンストラクタとデストラクタを型に宣言する
2. ブロックにデストラクタをつける

の2通りの方法があります。

### 1番目の方法:
```cpp
construct File {
    // ...
}

destruct File {
    // ...
}
```

### 2番目の方法:
```cpp
{
    File f = std::os.open("zipbomb.txt");
} destruct {
    f.close();
}
```

## Why no local function in C?

ローカル関数が書けます。
```c
int main() {
    int alt() {
        int opt() {
            return 0;
        }
        return opt();
    }
    return alt(); // 0
}
```
……GCC拡張ではないです。

## IsA made a gorilla the emperor or jungle (IsAはゴリラをジャングルの皇帝にした)

この言語には継承がないです。
代わりにHasA、つまりコンポジションを使います。

```cpp
class Leg {
public:
    void move() {
        "idk how to move lol\n";
    }
};

class Cat {
    Array<Leg, 4> Legs;
    void speak() {
        "Meow\n";
    }
};
```

これはObject Oriented Programmingの否定ではなく、
多重継承やタイトルにもあるような
余分すぎる数々のプロパティの排除ために
わざと継承を消しただけです。

### 熱帯人民帝国皇帝、ゴリラ

```cpp
class Banana {
public:
    bool food = true;
};

class Jungle : public Banana {
public:
    int trees = 10 ` 6;
    bool onFire = false;
};

class Gorilla : public Jungle {
public:
    void eatBanana() {
        "Why am I a food???\n";
    }
};
```

## アプリ層では快適に、システム層では余計なことをせずに

C++のように何でもかんでも裏の仕組みがあるのは良くありません。
```c
#pragma NO_STD
```
これを宣言することで裏のメモリ確保や何が起きてるのか意味不明な抽象化を一切動作させず、
完全に手動状態にします。
new / deleteで制御しよう。

## デフォルトで不変で良い

この言語はRust同様、デフォルトで変数は不変です。
可変にするには型名のあとに`mutable`をつけるだけです。

```cpp
int mutable a = 32;
int b = 42;
a <<= 1;
f"{a}\n"; // 64

b += 1900; // error
```
……C++にもmutableがあるらしいです。委員会は何を考えているんだろうか。

## 適度な省略！

さっきから出てる文字リテラル単体。
HolyCに影響されています。

```c
int main() {
    "Hello world!\n";
}
```

あと、関数は引数がないならシンボル名だけでもOK。
```c
void function() {
    "Function is not function, its feature.\n";
}
function;
```
この言語では、`main`, `start`, `entry`等の名前で`int`を返す関数の場合はエントリーポイントとして扱い、
何もない場合はそのファイル全体が`main`関数であるとして実行、もしくはコンパイルします。

## 論理演算子と連鎖比較でエレガントに

AND, OR, NOTだけで一応何でもできますけど、このせいでバグが起きてませんか？
NAND, NOR, XORがあるだけでかなり便利です。
そして、連鎖比較があれば、数学的に条件を書けます。

| 演算子 | 意味 |
| ---- | ---- |
| `&&` | AND |
| `\|\|` | OR |
| `!` | NOT |
| `!&&` | NAND |
| `!\|\|` | NOR |
| `^^` | XOR |

```cpp
if (isAdmin ^^ isUser) {
    "YOU ARE AN HACKER!!\n";
}
```

```cpp

if (wantFood !&& wantSleep) {
    "Go to bed <3\n";
}
```

```cpp
if (18 <= age <= 130) {
    "Welcome!\n";
}
```

## リテラル！
さっきからあるリテラルは文字列ですね。

### 文字列リテラル

```cpp
c"Hello";
```
これはC言語スタイルの文字列になります。
```cpp
Array<uint8> s = {72, 101, 108, 108, 111, 0};
```
って感じですね。
テンプレート省略できます(自明の時)

---

```cpp
"Hello";
```
これはただの文字列、もといutf-8の文字列になります。
```cpp
string s = "Hello";
```
って感じですね。

```cpp
List<uint8> c = {72, 101, 108, 108, 111};
```
こっちは可変長です。ヌル文字もありません。

s[0]でcharを取り出せます。
でもcharはASCIIではないです。
charは1コードポイントを表します。
なので実際には、

```cpp
List<List<uint8>> c = {{72}, {101}, ... {111}};
```
って感じですね。

### 数学リテラル

数学リテラルには`##`ではじめて`##`で終わるブロックを使います。
数学リテラル内では数字もしくは変数をスペースで区切った暗黙乗算ができます。
```cpp
float mutable pi = 3.141592;
float mutable r  = 5;
float ans = ##2 pi r##;
f"{ans}\n"; // 31.41592
```
今後機能が増えそうです。

### フォーマットリテラル

言わずもがな。
HolyCのprintf省略を現代風にreinterpret.
```cpp
f"{a} + {b} = {a + b}\n";
```
fで始めて引用符でかこみ、カッコで変数名とか書いてください。

## 最初からGUIができてもよくないですか

std::window,
std::render,
std::audio,
とか。
最初からこれができたら楽しいよね。
```cpp
{
    File f = std::os.open("shutdown.wav");
    std::window w {};
    while (!w.shouldClose()) {
        w.clear(0, 0, 0, 0);
        std::render(v0: 0.6, v1: 0.6, v2: 0.6);
    }
    std::audio.play(f);
} destruct {
    f.close();
}
```
これもきっと神の天啓なのでしょう。

---

おわり。いつか実装しよう。