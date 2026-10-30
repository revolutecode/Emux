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
        "subsystem": "console"
    }
}
```

`jobs` é opcional (0 = núcleos disponíveis) e o `-j` da linha de comando tem prioridade. `subsystem` aceita `console` (padrão) ou `windows`; o `-s/--subsystem` sobrepõe o valor do ficheiro.

As funções definidas em qualquer ficheiro de `sources` ficam visíveis para os restantes sem precisar de `@include`.

### Cache incremental

Cada módulo é compilado para um objecto próprio em `<output>.emux/obj/<arch>/<modulo>.emo` e só é regenerado quando o seu conteúdo muda. Alterações a uma assinatura de função ou ao tipo de uma variável global invalidam todos os módulos; alterações apenas ao corpo recompilam só esse módulo, e a ligação é evitada quando nada mudou. A cache é invalidada automaticamente ao recompilar o próprio `emux`, mudar de arquitetura ou usar `--force`.


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
- `b` ou `B` para blocos de memória brutos

O número após a letra define o tamanho em **bits** (se minúscula `u`, `i`) ou em **bytes** (se maiúscula `U`, `I`).

### Ponteiros

Um `*` após o tipo declara um ponteiro: `u8*` (ponteiro para byte), `void*`, `i64**`. Ponteiros ocupam 64 bits em x86_64 e 32 em x86.

- `&variavel` obtém o endereço de uma variável ou de um elemento de secção.
- `*ponteiro` lê o valor apontado; `*ponteiro = valor` escreve nele.

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

Ponteiros podem ser parâmetros e retorno de funções (`func Get() i32* { return &value }`). O acesso passa por `dload`/`dstore` no IR, por isso a largura lida/escrita segue o tipo apontado (8, 16, 32 ou 64 bits).

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

Valores com mais de 32 bits (incluindo ponteiros em x86_64) são guardados em temporários ocultos. Em x86_64 o backend usa registradores nativos; em x86 as operações são decompostas em pares de 32 bits, incluindo multiplicação e divisão. `div64`/`divu64` usam helpers gerados automaticamente (`__emux_idiv64`/`__emux_udiv64`) para que `i64`/`u64` produzam o mesmo resultado nas duas arquiteturas.

Parâmetros e retornos `i64`/`u64` também são suportados: no x86 um argumento largo ocupa 8 bytes na pilha (par hi/lo) e o retorno usa `edx:eax`; no x86_64 usa um único registrador de 64 bits. O call site coage cada argumento ao tipo declarado do parâmetro, incluindo literais pequenos passados a parâmetros largos.

### Inteiros arbitrários (fase 1)

Literais com mais de 64 bits já são reconhecidos pelo analisador (via `BigInt`), mas o backend NASM atual recusa-os com um diagnóstico explícito (`... excede 64 bits`). O suporte completo de aritmética de precisão arbitrária fica para uma fase seguinte.

### Atributos

Atributos são escritos entre `[[ ]]` antes de uma declaração:

```emux
[[import("kernel32")]]
func Sleep(u32 milliseconds) void

[[stdcall]]
func Legacy(u32 x) u32
```

- `[[import("lib")]]` declara uma função nativa **sem corpo**. O nome exportado pela biblioteca é o próprio nome da função (sem o escopo), e a biblioteca entra automaticamente no link (`-llib`). O `kernel32` é sempre ligado.
- `[[export("simbolo")]]` publica a função ou variável com esse nome nativo. Sem valor, usa o nome original.
- `[[cdecl]]`, `[[stdcall]]` e `[[fastcall]]` escolhem a convenção de chamada (padrão: `cdecl`); apenas uma é permitida por função.

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

### SDK Windows

`examples/emux/sdk/windows.ex` traz wrappers prontos para a API do Windows:

```emux
@include "emux/sdk/windows.ex"

[Main]

func Start() i32 {
    windows::Sleep(0)
    return 7
}
```

Inclua `examples/` em `project.include` e chame `windows::console_out`, `windows::console_in`, `windows::newfile`, `windows::read` e `windows::write`. Os wrappers aceitam ponteiros (`u8*`); use `&variavel` para passar o endereço de um buffer ou variável declarada em Emux, ou `exir { bytes ... }` para dados embutidos no código.
