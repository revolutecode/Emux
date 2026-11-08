# Emux

Emux é uma linguagem de programação focada no desenvolvimento de emuladores e sistemas de médio nível (entre Assembly e C++).

## Compilação e Uso

```bash
# Compilar para x86_64 (padrão)
./emux arquivo.ex -o output

# Compilar para x86 (32 bits)
./emux arquivo.ex -a x86 -o output

# Compilar um projecto multi-ficheiro
./emux --config project.json

# Limitar o paralelismo (padrão: núcleos disponíveis)
./emux --config project.json -j 2

# Ignorar a cache e recompilar tudo
./emux --config project.json --force

# Escolher o subsystem PE (console ou windows)
./emux arquivo.ex -o output -s windows

# Exigir ';' no fim de cada instrução (padrão: opcional)
./emux arquivo.ex -o output --semicolons required

# Otimizar: -O1 (dead code elimination) ou -O2 (mais peephole); padrão -O0
./emux arquivo.ex -o output -O 2
```

Gerar e ligar binários x86 requer um `gcc` com alvo i686 (no MSYS2: `pacman -S mingw-w64-i686-gcc`). Coloque `<msys2>/mingw32/bin` antes de `<msys2>/ucrt64/bin` no `PATH`. O `tests/smoke.sh` deteta esse toolchain e executa os casos x86 em runtime; sem ele, valida apenas a montagem com `nasm -f win32`.

Projectos multi-ficheiro usam um `project.json`:

```json
{
    "project": {
        "name": "app",
        "type": "executable",
        "include": ["."],
        "sources": ["main.ex", "math.ex"]
    },
    "build": {
        "arch": "64",
        "output": "app",
        "jobs": 4,
        "subsystem": "console",
        "semicolons": "optional",
        "optimize": 0
    }
}
```

`jobs` é opcional (0 = núcleos disponíveis) e o `-j` da linha de comando tem prioridade. `subsystem` aceita `console` (padrão) ou `windows`; o `-s/--subsystem` sobrepõe o valor do ficheiro. `semicolons` aceita `optional` (padrão) ou `required`; o `--semicolons` sobrepõe o valor do ficheiro. `optimize` aceita `0` (padrão, preserva tudo), `1` (dead code elimination) ou `2` (mais o peephole de casts identidade); o `-O`/`--opt` da linha de comando sobrepõe o valor do ficheiro (ver [Otimização](#otimização)). `include` é opcional: serve apenas para diretórios extra do projeto, porque a STL é sempre procurada junto ao `emux.exe`.

As funções definidas em qualquer ficheiro de `sources` ficam visíveis para os restantes sem precisar de `@include`.

### Cache incremental

Cada módulo é compilado para um objecto próprio em `<output>.emux/obj/<arch>/<modulo>.emo` e só é regenerado quando o seu conteúdo muda. Alterações a uma assinatura de função, ao tipo de uma variável global ou a um `using` (alvo ou restrição) invalidam todos os módulos; alterações apenas ao corpo recompilam só esse módulo, e a ligação é evitada quando nada mudou. A cache é invalidada automaticamente ao recompilar o próprio `emux`, mudar de arquitetura, mudar o formato de objeto, mudar o nível de otimização ou usar `--force`. A chave inclui a versão do compilador, o `object_format`, a arquitetura, o nível de otimização e os caminhos de include.


## Versionamento

A versão vive num único sítio: [`version.json`](version.json). O CMake gera `Version.hpp` a partir dele, e a tag git `vX.Y.Z` tem de coincidir com o campo `compiler`. O `object_format` identifica o contrato Exir/`.emo`/ABI gerada e sobe quando esse contrato muda; foi o que aconteceu com a Exir v2 (de 1 para 2), com a instrução `copy` (de 2 para 3), com os globais `const` (de 3 para 4), com os inteiros multi-limb acima de 64 bits (de 4 para 5), com a instrução `trunc` e os tipos `q<bits>.<scale>` (de 5 para 6), com o codegen de float/ponto fixo e a instrução `bitcast` (de 6 para 7), com o nível de otimização na chave de cache (de 7 para 8) e com as comparações e as instruções de controlo de fluxo na Exir — `eq/ne/lt/gt/le/ge` e `label/jump/branch/branchnot` (de 8 para 9), os blocos `b` como memória crua com views e fatiamento (de 9 para 10), a directiva `local` que declara os slots do corpo (de 10 para 11) e a instrução `zero` com os blocos locais dentro da frame (de 11 para 12), invalidando a cache de forma conservadora.

```bash
python tools/version.py show                  # versões atuais e tag esperada
python tools/version.py bump minor            # 0.3.0 -> 0.4.0
python tools/version.py check --tag v0.3.0    # valida uma tag de release
python tools/version.py tag                   # cria a tag anotada (sem push)
```

As regras completas - quando subir `patch`, `minor` ou `major`, quando subir `object_format`, como criar e publicar tags e o que é proibido - estão em [VERSIONING.md](VERSIONING.md).

### Binários pré-compilados

Publicar uma tag `vX.Y.Z` dispara [`.github/workflows/release.yml`](.github/workflows/release.yml), que compila em modo Release, corre `tests/smoke.sh` e anexa à GitHub Release um pacote `emux-X.Y.Z-windows-x86_64.zip` (mais `SHA256SUMS.txt`). O pacote contém `emux.exe`, a pasta `stl/` ao lado do executável, as DLLs de runtime do MinGW (libstdc++, libgcc, libwinpthread) e a documentação.

O `emux.exe` pré-compilado continua a delegar a montagem e a ligação em ferramentas externas: precisa de `nasm` e de um `gcc` MinGW no `PATH`. Para gerar executáveis x86 (32 bits) é necessário um MinGW 32-bit (cdecl/stdcall/fastcall); para x86_64, um MinGW UCRT64 64-bit.


## Estrutura da Linguagem

O código é dividido em secções (ex: `[Main]`), que atuam como escopos.

```emux
[Main]

func Start() u8 {
    return 0
}
```

### Secções

Secções definem módulos e escopos com a sintaxe `[NomeDaSeccao]`. A secção `[Main]` com sua função `Start` (ou `_Start`) é o ponto de entrada (entry point) do programa.

### Declaração de Variáveis (`var`)

Em Emux, variáveis devem ser declaradas utilizando a palavra-chave `var` no escopo da seção:

`var nome : tipo`

Dentro do corpo de uma função `var` é erro: as variáveis de corpo declaram-se com `local` (ver "Variáveis de corpo" abaixo).

Exemplo:

```emux
[Vars]

var a : u8
var b : i16

[Main]

var c : i32

func Start() u8
{
    Vars::a = 5
    Vars::b = -10
    c = Vars::a + Vars::b
    return 0
}
```

Os tipos são definidos após os `:`, utilizando:
- `u` para inteiros sem sinal (unsigned)
- `i` para inteiros com sinal (signed)
- `f` para números de ponto flutuante (float)
- `b` para blocos de memória brutos

O número após a letra define o tamanho em **bits** (letra minúscula: `u32`, `i16`, `b64`) ou em **bytes** (letra maiúscula: `U4`, `I8`, `B16`).

### Variáveis de corpo (`local`)

`var` declara no escopo da secção — é uma global, com armazenamento estático. Dentro de uma função usa-se `local`, que reserva um slot na frame da chamada:

`local nome : tipo` | `local nome : tipo = expressão`

```emux
[Main]

func Area(i32 base, i32 altura) i32 {
    local dobro : i32 = base * altura
    local r : i32 = 0
    if (dobro > 100) {
        r = 1
    }
    return r
}

func Start() i32 {
    local acc : i32
    local i : i32 = 0
    while (i < 4) {
        acc += i
        i = i + 1
    }
    return acc + Area(3, 4)
}
```

- A frame existe automaticamente sempre que uma função declara locais; não há keyword na função para isso. Um `local` é um slot `[rbp-offset]`, não um símbolo estático, por isso não pode ser `[[export]]`-ado nem visto por outros módulos.
- Sem inicializador, o slot é escrito antes da primeira leitura: escalares recebem `0` e blocos são zerados byte a byte (a stack não vem limpa como a `.bss`).
- Diferente de `var`, o inicializador de um `local` pode ser qualquer expressão — aritmética, chamada, `&outra` — porque corre em runtime. Num `var` continua a ser preciso um literal, já que os bytes entram no objecto.
- Blocos também podem ser locais: `local buf : B32 = "hello"` copia os bytes para o slot na entrada, `local dst : B8 = src` copia de outro bloco, e `buf[i]`, `buf + K`, `&buf` e a conversão para `T*` funcionam como nos globais. O slot tem a capacidade do tipo (arredondada a múltiplos de 8), e os limites continuam a ser verificados em tempo de compilação.
- O âmbito de um `local` é o corpo da função, incluindo os blocos de `if`/`while`/`for`: um local declarado dentro de um `if` continua visível depois dele (o escopo por bloco ainda não é aplicado), mas só é inicializado se esse caminho for executado.
- `&local` é permitido e é assim que um ponteiro para o slot se obtém; esse ponteiro fica pendurado depois do `return`, como em C.

### Tipos padrão e `using`

`bool`, `usize`, `isize`, `uptr` e `iptr` são **usings embutidos com restrição de tipo**, não primitivos:

- `bool` = `u1` (ocupa 1 byte, só o bit 0 é significativo);
- `usize`/`uptr` = `u<largura do ponteiro>` (64 bits em x86_64, 32 em x86);
- `isize`/`iptr` = `i<largura do ponteiro>`.

Um `using` cria um tipo distinto da sua representação, como em C++, mas com conversão direta para o tipo base. Dois usings restritos com a mesma representação são incompatíveis entre si:

```emux
using Counter = u32
using Offset = u32

var count : Counter
var offset : Offset

func Bump(Counter value) Counter {
    return value + 1
}

func Start() i32 {
    count = 40
    offset = 2
    count = Bump(count)
    return count + offset
}
```

`Bump(offset)` falha com `Impossible conversion of Offset to Counter`: a representação é igual, mas a identidade não. Um `using` pode ser marcado `[[unrestrict]]` para ser um alias estrutural transparente:

```emux
[[unrestrict]]
using Number = u32
```

Parâmetros de tipos nominais são restritos por omissão (exigem o mesmo `using`); parâmetros de tipos anónimos usam semelhança estrutural (basta a representação). Os atributos `[[restrict]]` e `[[unrestrict]]` sobre um parâmetro forçam a escolha:

```emux
func Echo([[unrestrict]] Counter value) Counter {
    return value
}
```

Literais inteiros podem estreitar para o alvo se o valor couber na representação (por exemplo `flag = 1` para `bool`), mas variáveis não têm conversões implícitas de estreitamento.

### Enums

Um `enum` declara constantes nomeadas. É um `using` restrito sobre o tipo dos elementos, por isso `Color` converte para `u8` mas não para outro tipo nominal:

```emux
enum Color : u8 {
    Red,
    Green,
    Blue
}

func Start() u8 {
    return static_cast<u8>(Color::Blue)
}
```

- O tipo por omissão é `u32`. Aceitam-se inteiros, ponto fixo, float e blocos `b`.
- Sem `= valor`, o elemento usa o auto-incremento a partir de `0`. Float não tem auto-incremento: exige valor em cada elemento.
- O valor pode ser qualquer expressão constante, incluindo elementos anteriores do mesmo enum (`C = A + 5`).
- Os elementos são referidos como `Color::Red`; dentro do próprio enum basta `Red`.

`[[step]]` controla o auto-incremento e `[[unrestrict]]` remove a identidade do tipo, tal como num `using`:

```emux
[[step(2)]]
enum Even : u8 { Two, Four, Six }

[[step(off)]]
enum Flags : u8 { None = 0, Read = 1, Write = 4 }
```

`[[step(off)]]` exige valor explícito em todos os elementos.

Sem nome, os elementos ficam diretamente no escopo:

```emux
enum {
    Ok,
    Warn = 5,
    Fail
}
```

Um enum de bloco `b` guarda as strings em `.rdata` e cada elemento é um ponteiro para o respetivo bloco:

```emux
enum Msg : B16 {
    Hello = "hi",
    Bye = "adeus"
}

func Start() u8 {
    return *Msg::Hello    // 'h'
}
```

Enums não geram código: os elementos escalares são constantes dobradas em literais e só os blocos `b` ocupam memória.

### Ponto e vírgula (`;`)

O `;` no fim de uma instrução é opcional por padrão, como no C++ antigo:

```emux
func Start() i32 {
    return 42
}

func Strict() i32 {
    return 42;
}
```

As duas formas são aceites. Para exigir o terminador (como C++ moderno), use `--semicolons required` na linha de comando ou `"semicolons": "required"` em `build` no `project.json`. O modo `required` reporta `Expected ';' after statement.` para cada instrução sem `;`; declarações de função e blocos `exir { ... }` continuam sem terminador.

### Ponteiros

Um `*` após o tipo declara um ponteiro: `u8*` (ponteiro para byte), `void*`, `i64**`. Ponteiros ocupam 64 bits em x86_64 e 32 em x86.

- `&variavel` obtém o endereço de uma variável ou de um elemento de secção.
- `*ponteiro` lê o valor apontado; `*ponteiro = valor` escreve nele.
- Aritmética escalada como em C: `p + n`/`p - n` avançam `n` elementos do tipo apontado, `p[i]` equivale a `*(p + i)` e `p - q` devolve a distância em elementos (inteiro com sinal). `p++`, `p--`, `p += n` e `p -= n` também funcionam.

```emux
[Main]

var value : i32
var slot : i32*

func Store(i32* target, i32 amount) void {
    *target = amount
}

func Start() i32 {
    Store(&value, 40)
    slot = &value
    *slot = *slot + 2
    return value
}
```

Ponteiros podem ser parâmetros e retorno de funções (`func Get() i32* { return &value }`). O acesso passa por `load`/`store` na Exir, por isso a largura lida/escrita segue o tipo apontado (8, 16, 32 ou 64 bits).

### Atribuição composta e incremento

`x += y`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=` e `>>=` combinam a leitura e a escrita do alvo. Funcionam em variáveis, em `*p` e em `p[i]`; em ponteiros, `+=` e `-=` escalam pelo tamanho do elemento apontado. `x++` e `x--` são atalhos de `x += 1`/`x -= 1` e, tal como a atribuição, usam-se como instrução (não produzem valor dentro de uma expressão).

O lado que recebe é uma expressão de endereço, não apenas um nome: além de `nome`, `nome[i]` e `*nome`, valem `*(expr) = v` e `expr[i] = v` com qualquer expressão dentro — `*(text + 3) = 90`, `*(p + 2) += 1`, `Primeiro(p)[0] = 65`. Qualquer outro alvo (um literal, uma soma sem `*`, o resultado de uma chamada) é erro: `The left-hand side of an assignment must be a variable, an index or a dereference expression.`

### Comparações

`==`, `!=`, `<`, `>`, `<=` e `>=` comparam dois inteiros ou dois ponteiros e devolvem `0` ou `1` num `u8` (a mesma convenção do operador `!`). A precedência segue o C: igualdade (`==`, `!=`) liga-se mais fraco que os relacionais (`<`, `>`, `<=`, `>=`), que por sua vez ligam mais fraco que os shifts. O sinal do tipo dos operandos escolhe a comparação: em `i32`, `-1 < 1` é verdadeiro; o mesmo valor como `u32` seria falso. Comparações entre tipos mistos (inteiro com ponteiro), em `float`/ponto fixo ou em inteiros largos (>64 bits) ainda não são suportadas. O resultado pode ser usado em aritmética (`r = r + (a < b)`) e, mais tarde, como condição de `if`/`while`.

```emux
[Main]

var a : i32
var b : i32
var r : i32

func Start() i32 {
    a = -1
    b = 1
    r = (a < b) + (a == b)   // 1 + 0
    return r                 // 1
}
```

### Controlo de fluxo

`if`/`else`, `else if`, `while` e `for` aceitam qualquer expressão inteira ou ponteiro como condição — em particular o `u8` de uma comparação (`0` é falso, qualquer outro valor é verdadeiro). Os blocos usam `{ }`; `for (init; cond; step)` tem as três secções opcionais (condição ausente significa ciclo infinito).

```emux
[Main]

var i : i32
var sum : i32

func Start() i32 {
    sum = 0
    if (sum == 0) {
        sum = 1
    } else if (sum < 5) {
        sum = 2
    } else {
        sum = 3
    }

    i = 1
    while (i <= 5) {         // soma 1..5 = 15
        sum = sum + i
        i = i + 1
    }

    for (i = 0; i < 10; i = i + 1) {
        if (i % 2 == 0) {    // soma os pares 0..9 = 20
            sum = sum + i
        }
    }
    return sum
}
```

Ainda não há `break`/`continue` nem variáveis locais dentro das funções (as variáveis são globais de secção). Internamente, cada construção traduz-se em rótulos e saltos (`label`/`jump`/`branch`/`branchnot`) na Exir.

### Strings

Um literal de string é um `u8*` para bytes estáticos terminados por NUL:

```emux
[Main]

var text : u8*

func Start() i32 {
    text = "AB"
    return *text   // 65 ('A')
}
```

O conteúdo é emitido como um bloco `data` na Exir e numa secção `.data` do objecto. Literais iguais dentro do mesmo módulo partilham o mesmo bloco (interning) e os rótulos gerados (`__emux_strN`) são privados do módulo, para não colidirem na ligação de projectos multificheiro. Escapes suportados: `\0`, `\n`, `\r`, `\t`, `\\`, `\"` e `\xNN` (exatamente dois dígitos hexadecimais).

### Blocos de memória (`b`)

`bN` declara uma região estática de N bits; `BN` usa N bytes. Ao contrário de um ponteiro, o bloco é dono do armazenamento: a variável é a própria região — reservada em `.bss` quando nasce sem inicializador, e colocada em `.data` quando declarada com um literal de string — e não um endereço.

```emux
[Main]

var text : B8
var copy : B8

func Start() i32 {
    text = "AB"     // copia 3 bytes: 'A', 'B' e o NUL
    copy = text     // copia os 8 bytes do bloco
    return *copy    // 65 ('A')
}
```

Regras:

- Blocos são variáveis globais estáticas, donas do armazenamento e não-const por omissão (ver `[[const]]` abaixo). O bloco é memória crua: não tem tipo de elemento (isso é trabalho para arrays, no futuro).
- Em contexto de ponteiro (`p = text`, `First(text)`, `return text`) o bloco converte para **qualquer** `T*`, apontando para o primeiro byte, desde que `sizeof(T) <= tamanho do bloco`; caso contrário é erro de compilação (`Buffer of N bytes is too small to convert to 'T*'`). `static_cast<T*>(&text)` explicita a mesma conversão. Para `usize` ou outro inteiro não há conversão implícita: use `&text` com um cast explícito.
- `&text` é o endereço do primeiro byte, `*text` lê/escreve o primeiro byte e `text[i]` acede byte a byte (`*text` equivale a `text[0]`). Índices constantes fora do bloco são erro de compilação.
- `text + K` produz uma *view* (slice) do bloco deslocada K bytes, **sem** decair: mantém a categoria de bloco e o tamanho restante, que serve de limite em tempo de compilação. O offset `K` tem de ser uma constante inteira não-negativa; fatiar para além do fim do bloco é erro (`Buffer slice is out of bounds`). Slices encadeiam-se (`text + 3 + 4` = 7 bytes). A view só decai para `u8*` quando usada onde um ponteiro é esperado — por exemplo `io::println(text + 1)` imprime `"ello, World"`. `&text` continua a ser o endereço do primeiro byte (offset 0).
- A view é um lvalue: `*(text + K) = 90` escreve no byte deslocado, `*(text + K) += 1` lê-e-escreve, e `*(text + K)` lê. Uma view com zero bytes restantes (ex.: `text + 8` num `B8`) não pode ser desreferenciada — erro (`empty buffer view`).
- `static_cast<T>(text + K)` lê `sizeof` bytes a partir da view, little-endian, como um `load` da largura física de `T` (um `q16.8` ocupa 2 bytes, um `i129` ocupa 17). O limite é o tamanho **restante** da view: `static_cast<u32>(text + 6)` num `B8` falha com `reads 4 bytes, but the buffer has only 2 bytes remaining`. A escrita larga (`static_cast<T>(text + K) = v`) ainda não existe.
- `[[restrict]] var text : B8` bloqueia apenas a conversão implícita para `T*` (o cast explícito continua possível); `text[i]`, `&text` e `*text` continuam permitidos.
- Em atribuição (`text = ...`) o lado direito tem de ser um literal de string (cópia com o NUL incluído) ou outro bloco que caiba. A cópia é emitida como `copy` na Exir e baixa para `rep movsb`.
- Um bloco pode nascer inicializado sem `[[const]]`: `var text : B13 = "Hello, World"` compila, os bytes ficam em `.data` e a região continua escrevível. O literal tem de caber na capacidade (`does not fit in buffer`).
- Os tamanhos são verificados em tempo de compilação: `text = "ABC"` num `B2` falha com `does not fit in buffer`.
- Parâmetros e retornos declarados como bloco convertem para `u8*` na ABI, pelo que atravessam a chamada como qualquer ponteiro. `sizeof` ainda não existe; o tamanho vem do tipo declarado.
- Um bloco também pode ser local (`local buf : B16 = "oi"`): o slot da frame tem a capacidade do tipo, o inicializador é copiado para ele na entrada da função, e sem inicializador os bytes são postos a zero. Ler, escrever, fatiar e passar como ponteiro funciona como nos blocos estáticos — com a diferença de que o armazenamento morre no `return`.

### Declarações imutáveis (`[[const]]`)

`[[const]]` numa declaração torna a variável imutável e coloca os bytes iniciais em `.rdata` (só leitura). Sem `[[const]]` o mesmo inicializador é permitido e os bytes vão para `.data`, escrevíveis:

```emux
[Main]

[[const]] var answer : i32 = -42
[[const]] var text : B8 = "AB"

func Start() i32 {
    return 0 - answer   // 42
}
```

Regras:

- O inicializador tem de ser literal: inteiro (sinal opcional) para escalares e string para blocos `b`. Tipos que não são escalar nem bloco (por exemplo `u8*`) não aceitam inicializador.
- O valor tem de caber no tipo: `[[const]] var small : u8 = 300` falha com `does not fit`.
- A memória inicial é emitida como `db` — em `.rdata` para `[[const]]`, em `.data` para a variante mutável; blocos completam com zeros até à capacidade e escalares usam bytes little-endian.
- Sem inicializador, um bloco/escalar `[[const]]` fica a zero em `.rdata`; mutável, é reservado em `.bss`.
- Qualquer atribuição à variável (`x = ...`, `text = ...`) ou escrita através de um bloco const (`*text = ...`) é erro de compilação. A constness de um ponteiro não se transmite ao pointee: `[[const]] var p : u8*` continua a permitir `*p = ...`.
- `[[const]]` numa atribuição (`[[const]] x = 1`) continua a ser apenas uma dica de dobramento de constantes; a imutabilidade vem da declaração.

### Convenções de chamada (ABI)

Em x86 há três convenções, escolhidas por atributo:

```emux
[[cdecl]]
func Padrao(i32 a, i32 b) i32 { return a + b }

[[stdcall]]
func Win32(i32 a, i32 b) i32 { return a - b }

[[fastcall]]
func Rapida(u32 a, u32 b, u32 c) u32 { return a + b + c }
```

- `cdecl` (padrão): argumentos na pilha, o chamador limpa a pilha; símbolo `_nome`.
- `stdcall`: argumentos na pilha, a função limpa (`ret N`); símbolo `_nome@N`.
- `fastcall`: os dois primeiros argumentos de até 32 bits vão em `ecx`/`edx`, o resto na pilha; símbolo `@nome@N`, a função limpa.

Em x86_64 existe uma única ABI (Microsoft x64): os quatro primeiros argumentos vão em `rcx`, `rdx`, `r8`, `r9`, o resto na pilha, e os símbolos não são decorados. Os atributos são aceites e não alteram o código nessa arquitetura.

Os atributos também valem para imports nativos, permitindo escolher a convenção e o mangle corretos:

```emux
[[import("kernel32")]]
[[stdcall]]
func Sleep(u32 milliseconds) void
```

### Matemática de 64 bits

Valores com mais de 32 bits (incluindo ponteiros em x86_64) são guardados em slots de 8 bytes na frame. Em x86_64 o backend usa registradores nativos; em x86 as operações são decompostas em pares de 32 bits, incluindo multiplicação e divisão. A divisão de 64 bits usa helpers gerados automaticamente (`__emux_idiv64`/`__emux_udiv64`) para que `i64`/`u64` produzam o mesmo resultado nas duas arquiteturas.

Parâmetros e retornos `i64`/`u64` também são suportados: no x86 um argumento largo ocupa 8 bytes na pilha (par hi/lo) e o retorno usa `edx:eax`; no x86_64 usa um único registrador de 64 bits. O call site coage cada argumento ao tipo declarado do parâmetro, incluindo literais pequenos passados a parâmetros largos.

### Inteiros de precisão arbitrária

Em tempo de compilação, literais e expressões constantes são avaliados com precisão arbitrária (`BigInt`), sem perda em nenhum passo intermédio. `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `<<` e `>>` (com `>>` aritmético em tipos com sinal) produzem o valor exato, e `static_cast` trunca como em runtime:

```emux
return static_cast<i32>(1000000000000000000000000000000 /
                        100000000000000000000000000000)  // 10
```

Um resultado constante que caiba em 64 bits alimenta variáveis, parâmetros e retornos normais; o tipo de uma expressão constante com mais de 64 bits é normalizado para o tipo mínimo que representa o resultado.

Em runtime, `uN`/`iN` aceitam qualquer `N` entre 1 e 2³²−1 (o limite é o `uint32_t` que guarda a largura, alinhado com o Zig). Um valor de até 64 bits vive num slot do tamanho do registo (8, 16, 32 ou 64 bits); acima disso é multi-limb, guardado em `ceil(N/8)` bytes contíguos, em complemento para dois e little-endian. O armazenamento acompanha o tipo exato: globais ocupam o número de bytes do tipo em `.bss` (`resb` para tipos largos), `[[const]]` vai para `.rdata` com os bytes iniciais, e os temporários recebem um slot de frame do tamanho do tipo (arredondado a 8 bytes) em vez dos 8 bytes de um escalar.

`+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `!` e `<<`/`>>` baixam para helpers multi-limb gerados no próprio assembly (`__emux_wadd`, `__emux_wmul`, `__emux_wsdiv`, `__emux_wsrem`, `__emux_wshl`, `__emux_wsar`, ...), que operam byte a byte com carry; `>>` é lógico em tipos sem sinal e aritmético com sinal, e o resto (`%`) segue o sinal do dividendo, como em C. `static_cast` entre larguras alarga com extensão de sinal/zero e trunca, e uma atribuição de um valor largo a um destino estreito trunca como em C:

```emux
var g : u128
var p : u128*

func Start() i32 {
    g = 18446744073709551616              // 2^64
    p = &g
    *p = *p + 5
    return static_cast<i32>(g & 255)      // 5
}
```

Larguras que não coincidem com o slot (`u3`, `i7`, `u24`, `u40`, `u48`, `u56`, ou `u65`, `i100`, `i129` acima de 64 bits) são normalizadas pela instrução Exir `trunc`: o resultado de cada operação, cast ou leitura é mascarado aos `N` bits e, em tipos com sinal, o bit de sinal é estendido pelo byte alto. A IR fica explícita sobre essa semântica em vez de depender de lixo nos bits altos; os literais já chegam canónicos, truncados à largura do tipo.

```emux
var a : u3

func Start() i32 {
    a = 7
    a = a + 1
    return static_cast<i32>(a)      // 0: 8 não cabe em 3 bits
}
```

Acima de 64 bits o suporte é exclusivo de x86_64; em x86, declarar ou converter para um tipo acima de 64 bits é diagnosticado, tal como o são parâmetros, retornos e argumentos largos nas duas arquiteturas (a ABI gerada continua a passar inteiros num único slot). Larguras até 64 bits, incluindo as não nativas, funcionam nas duas arquiteturas (em x86, 33..63 bits usam um par `edx:eax`). Larguras fora de 1..2³²−1 (ex.: `u0`, `u4294967296`) são rejeitadas pelo parser de tipos.

### Float e ponto fixo

`fN` é um float de `N` bits com expoente escolhido pela largura (`5` bits até 16, `8` até 32, e depois até 32 bits para larguras maiores) e mantissa nos restantes; `q<bits>.<scale>` é ponto fixo em complemento para dois com `scale` bits fracionários (ex.: `q32.16`). `N` tem de ser múltiplo de 8 e estar entre 16 e 2³²−1; o parser valida `0 < scale < bits` e o limite de largura.

```emux
var preco : q32.16
var taxa : f32

func Start() i32 {
    preco = 7
    preco = preco * 2            // 14.0, truncado na escala
    taxa = static_cast<f32>(preco) / 4
    return static_cast<i32>(taxa) // 3
}
```

Atribuições, literais, `+`, `-`, `*`, `/` e `-` unário funcionam para ambos os tipos; `%` existe para ponto fixo (resto do valor bruto, com o sinal do dividendo) e é rejeitado em float. Os operandos de ponto fixo têm de partilhar a mesma escala (use `static_cast` para reescalar), mas um literal ou inteiro do lado oposto é convertido para o tipo do outro. `static_cast` converte entre inteiro, ponto fixo e float (truncando para zero na saída para inteiro), e `reinterpret_cast` entre tipos do mesmo tamanho baixa para a instrução Exir `bitcast`, sem conversão de valor.

A aritmética corre num runtime C (`runtime/emux_rt.c`) gerado no assembly do módulo e compilado com o `gcc` do link, com arredondamento ao par mais próximo (RNE), flush-to-zero para denormais e Inf/NaN canónico. Converter NaN ou Inf para inteiro dá `0`; dividir ponto fixo por zero dá `0` (a semântica já rejeita divisão por zero em expressões constantes). Isto evita reimplementar IEEE 754 e larguras arbitrárias em NASM e mantém o mesmo resultado em qualquer largura suportada.

Em x86_64, valores de até 64 bits são passados e devolvidos em registos inteiros nas funções Emux (o backend trata-os como bits crus), e os de mais de 64 bits por ponteiro/cópia. O runtime é exclusivo de x86_64: em x86, declarar ou usar float/ponto fixo de runtime é diagnosticado. Imports nativos não aceitam float/ponto fixo em parâmetros, retornos ou argumentos — a ABI C usaria `xmm` e o valor seria corrompido silenciosamente; passe um ponteiro em vez disso.

### Literais decimais

Um literal com ponto (`1.5`, `0.5`, `-2.25`) é um valor racional em tempo de compilação: `1.5` vale `15/10`. Não existe um tipo decimal em runtime nem instruções novas — o literal converte-se, no local de uso, para a representação de armazenamento do tipo alvo (inteiro escalado por `2^scale` em ponto fixo, bits IEEE-like em float), com arredondamento ao valor mais próximo.

O tipo efetivo do literal segue o contexto, tal como um literal inteiro estreita para o alvo:

- atribuição usa o tipo da variável; argumento usa o tipo declarado do parâmetro; retorno usa o tipo da função; `static_cast<q32.16>(1.5)` usa o alvo do cast; num binário usa o tipo fixo/float do outro operando;
- sem contexto explícito, o decimal é `f64`.

```emux
[Main]

var preco : q32.16
var taxa : f32

func Aplica(q32.16 valor) q32.16 {
    return valor * 1.5
}

func Start() i32 {
    preco = 10
    preco = Aplica(preco)      // 15.0
    taxa = 2.5
    return static_cast<i32>(preco) + static_cast<i32>(taxa)  // 17
}
```

Atribuir um decimal a um alvo inteiro é um erro explícito, para não truncar em silêncio: use uma variável `fixed`/`float` ou um `static_cast` (`Decimal literal cannot be assigned to 'i32'; use a fixed or float variable, or static_cast.`). Um `[[const]]` de ponto fixo ou float aceita literal decimal, gravando os bytes de armazenamento já convertidos.



O pipeline é Lexer → Preprocessor → Parser → SemanticAnalyzer → IRBuilder → Exir v2 → `Exir::Optimize` (DCE/peephole, opcional) → backend → NASM → gcc.

A Exir deixou de ser uma lista de instruções de máquina e passou a ser uma IR lógica, tipada e independente da arquitetura, inspirada no TAC do LLVM:

- temporários `%tN` guardam resultados e cada instrução carrega o seu tipo;
- parâmetros e variáveis globais vivem em memória, com `load`/`store`/`storev`/`addr` explícitos;
- blocos `b` copiam-se com `copy <destino>, <origem>, <bytes>` e zeram-se com `zero <destino>, <bytes>`;
- um `local` do corpo é uma directiva dentro do `func` (`local <nome> : <tipo>`), seguida do `storev`/`copy`/`zero` do seu inicializador; o backend dá-lhe um slot, pelo que as referências ao nome caem em `[rbp-offset]` em vez de `[rel símbolo]`;
- globais `[[const]]` usam `global <nome> : <tipo> const <bytes>` e são emitidos em `.rdata`;
- conversões são explícitas (`cast`), tal como a normalização de largura (`trunc <tipo> <valor>`) e a reinterpretação de bits (`bitcast <tipo> <valor>`);
- chamadas indicam tipo de retorno, alvo, ABI e tipos dos argumentos;
- diretivas de módulo (`module`, `import`, `global`, `extern`, `export`, `data`) descrevem a interface entre módulos.

```text
func main_AddOne : i32 cdecl 0
param a : i32
%t0 = load i32 a
%t1 = add i32 %t0, 1
ret i32 %t1
end
```

Esta representação é o ponto onde entram, mais tarde, multithreading/mutex/atomic como operações da linguagem: o backend só precisa de conhecer a lógica tipada, não a sintaxe original. Os inteiros de largura arbitrária já assentam aqui: `const`, `load`, `store`, `cast`, `trunc` e as operações binárias tratam `n = ceil(bits/8)` bytes contíguos, e a gramática inclui os tipos `q<bits>.<scale>` para o ponto fixo. `trunc` torna explícita a semântica de largura fixa: mascara o valor aos bits do tipo e, em tipos com sinal, estende o bit de sinal pelo slot.

### Backends

O backend NASM está dividido em quatro peças:

- `NasmGeneratorBase` — tabelas de símbolos, decoração de nomes, secções e emissão do módulo;
- `X64Generator` — baixagem para Microsoft x64 (`rcx`/`rdx`/`r8`/`r9`, shadow space);
- `X86Generator` — baixagem para Win32 (cdecl/stdcall/fastcall, pares de 32 bits para valores largos);
- `NasmToolchain` — invocações de `nasm`/`gcc` e o stub de entrada.

Cada parâmetro, local e temporário ocupa um slot de 8 bytes na frame; blocos locais e inteiros largos ocupam a sua dimensão, arredondada a múltiplos de 8. Não há alocação de registradores. Essa escolha privilegia a correção; as otimizações atuais atuam ao nível da Exir (antes do backend), não na distribuição por registradores.

### Otimização

A otimização acontece em duas camadas, ambas ligadas por `-O <0|1|2>` (ou `--opt`) na linha de comando, que sobrepõe `"optimize": <0|1|2>` em `build` no `project.json`. **A omissão é `-O0`** (preserva tudo).

A primeira camada corre no `IRBuilder`, **antes de o código virar Exir**: elimina na origem o que é inequivocamente não usado, para que nem chegue ao IR. A segunda camada é uma fase de texto sobre a Exir, entre `IRBuilder::Build` e a geração de assembly, que serve de rede de segurança para o código morto que só é visível depois de o IR existir (temporários). O IR otimizado é o que vai para o `.emo` e para o artefacto `.exir`, por isso cache, depuração e assembly ficam sempre coerentes entre si.

- **`-O0`** — nenhuma transformação; o IR sai do builder tal como foi construído, imports não usados e loads descartados incluídos.
- **`-O1`** — *dead code elimination*. Na origem (IRBuilder): um `[[import]]` nunca chamado neste módulo não é emitido, e uma expressão-instrução descartada e livre de efeitos (ex: `static_cast<i32>(g)`, cujo único resultado seria um `load` morto) nem chega ao Exir. Sobre o texto (Exir): descarta, em ponto fixo, as instruções puras (`const`, `cast`, `bitcast`, `trunc`, `addr`, `load` e os operadores aritméticos/lógicos/unários) cujo temporário de destino nunca é lido. Instruções com efeito observável (`store`, `storev`, `call`, `ret`, `copy`, `asm`) e as diretivas de módulo são sempre preservadas. Matar um consumidor pode matar os seus produtores, por isso a análise repete até estabilizar.
- **`-O2`** — `-O1` mais um peephole local: elimina `cast`/`bitcast` de identidade (mesmo tipo de origem e destino), encaminhando o operando diretamente para os consumidores do destino.

Globais e funções não usadas **não** são podadas por módulo: outro módulo pode referenciá-las via `extern`, pelo que só os imports (cada módulo emite o seu próprio `extern` nativo) são um caso seguro de eliminação na origem.

Como o nível altera o código gerado, entra na composição da chave de cache (`opt:<n>`); mudar de `-O` recompila os módulos, e por isso o `object_format` subiu de 7 para 8. O caminho `-O0` mantém o comportamento anterior byte a byte, pelo que a suite continua verde por omissão; os níveis superiores são verificados por testes de preservação de comportamento (mesmos *exit codes* sob `-O2`) e por casos dedicados que comprovam que um `load` morto é eliminado e que um import não usado é podado (com o import usado preservado).

### Atributos

Atributos são escritos entre `[[ ]]` antes de uma declaração:

```emux
[[import("kernel32")],[stdcall]]
func Sleep(u32 milliseconds) void

[[cdecl][noexcept]]
func Legacy(u32 x) u32
```

Vários atributos podem ser agrupados num único `[[ ]]`. A vírgula é opcional:
`[[import("kernel32")],[stdcall]]`, `[[import("kernel32")][stdcall]]` e
`[[import("kernel32"), stdcall]]` são equivalentes.

- `[[import("lib")]]` declara uma função nativa **sem corpo**. O nome exportado pela biblioteca é o próprio nome da função (sem o escopo), e a biblioteca entra automaticamente no link (`-llib`). O `kernel32` é sempre ligado.
- `[[export("simbolo")]]` publica a função ou variável com esse nome nativo. Sem valor, usa o nome original.
- `[[cdecl]]`, `[[stdcall]]` e `[[fastcall]]` escolhem a convenção de chamada (padrão: `cdecl`); apenas uma é permitida por função.
- `[[step(n)]]` define o auto-incremento dos elementos de um `enum` (padrão: `1`); `[[step(off)]]` desliga-o e exige valor explícito em todos os elementos.
- `[[restrict]]` e `[[unrestrict]]` aplicam-se a `using` e a `enum`, controlando a identidade do tipo. Em variáveis de bloco `bN`/`BN`, `[[restrict]]` bloqueia a conversão implícita do bloco para `T*`.
- `[[frame]]` aplicado a um `local` exige que o slot seja estável na frame — nunca coalescido nem movido. Numa função é a mesma promessa, extensiva a todos os seus locais. Numa `var` é erro, porque uma global não tem slot nenhum.
- `[[noframe]]` aplicado a uma função proíbe o corpo de declarar locais; a função não reserva frame para variáveis (os slots da ABI dos argumentos continuam a existir). Aplicar `[[noframe]]` a uma `var` ou a um `local` é erro, tal como combinar `[[frame]]` e `[[noframe]]` na mesma função.

A análise semântica verifica os nomes, a convenção de chamada e recusa imports com corpo, imports simultaneamente exportados e imports do entry point.

```emux
[[import("kernel32")]]
func Sleep(u32 milliseconds) void

[[export("minha_api")]]
func Start() i32 {
    Sleep(0)
    return 0
}
```

### STL Windows

A STL vive em [`stl/`](stl) na raiz do projecto e acompanha sempre o executável: o build (e o pacote de release) copia-a para `<dir do emux.exe>/stl`, que é um caminho de include por omissão. O `<dir do emux.exe>` também é pesquisado, pelo que `@include "std.ex"` e `@include "stl/std.ex"` funcionam sem configuração; `project.json` só precisa de `include` para diretórios extra. Não há fallback para a árvore de código: se a pasta `stl` não estiver ao lado do `emux.exe`, o include falha com a lista de caminhos procurados. Também pode ser usada com `@include` explícito:

```emux
@include "stl/windows.ex"

[Main]

func Start() i32 {
    windows::Sleep(0)
    return 7
}
```

A STL está dividida em três módulos, todos com guard de inclusão
(`@ifndef`/`@define`), pelo que podem ser incluídos em qualquer ordem sem
duplicar declarações:

- `stl/windows.ex` — API do kernel32 (`[[import("kernel32")]]` + `[[stdcall]]`,
  para decorar corretamente os símbolos também no alvo x86) e wrappers
  `windows::console_out`, `console_err`, `console_in`, `newfile`, `read` e
  `write`. `HANDLE`, `LPVOID`, `LPCVOID` e `POINTER` são `usize`, ou seja, têm a
  largura da plataforma em vez de assumir 64 bits.
- `stl/io.ex` — `[io]` com `print`, `println`, `eprint`, `eprintln`, `write`,
  `write_err` e `read`. As funções de texto usam `lstrlenA`; `Newline` é um
  bloco `[[const]]` com `"\r\n"`.
- `stl/file.ex` — `[file]` com `using FILE = usize` e operações sobre ficheiros:
  `open`, `open_read_write`, `create`, `create_new`, `append`, `read`, `write`,
  `write_text`, `close`, `flush`, `size`, `seek`, `seek_begin`, `seek_end`,
  `truncate`, `remove`, `rename`, `copy`, `attributes`, `set_attributes`,
  `make_directory` e `remove_directory`. Os modos e origens de seek estão
  expostos como constantes (`file::AccessRead`, `file::CreateAlways`,
  `file::SeekEnd`, ...).

`stl/std.ex` inclui os três e é o ponto de entrada recomendado:

```emux
@include "std.ex"

[Main]

var path : B32
var handle : file::FILE
var buffer : B16

func Start() i32 {
    path = "saida.txt"
    handle = file::create(path)
    file::write_text(handle, "Emux")
    file::close(handle)
    handle = file::open(path)
    file::read(handle, buffer, 16)
    file::close(handle)
    io::println("feito")
    return *buffer
}
```

Os wrappers aceitam ponteiros (`u8*`); passe um bloco `b` directamente (converte
para `u8*`, com verificação de tamanho) ou use `&variavel` para o endereço de
uma variável declarada em Emux. Projectos multificheiro podem incluir a raiz via `project.include`; em
modo de fonte única a STL é procurada junto ao `emux.exe` e na raiz do
repositório.
