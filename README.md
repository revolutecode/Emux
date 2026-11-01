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
        "semicolons": "optional"
    }
}
```

`jobs` é opcional (0 = núcleos disponíveis) e o `-j` da linha de comando tem prioridade. `subsystem` aceita `console` (padrão) ou `windows`; o `-s/--subsystem` sobrepõe o valor do ficheiro. `semicolons` aceita `optional` (padrão) ou `required`; o `--semicolons` sobrepõe o valor do ficheiro.

As funções definidas em qualquer ficheiro de `sources` ficam visíveis para os restantes sem precisar de `@include`.

### Cache incremental

Cada módulo é compilado para um objecto próprio em `<output>.emux/obj/<arch>/<modulo>.emo` e só é regenerado quando o seu conteúdo muda. Alterações a uma assinatura de função, ao tipo de uma variável global ou a um `using` (alvo ou restrição) invalidam todos os módulos; alterações apenas ao corpo recompilam só esse módulo, e a ligação é evitada quando nada mudou. A cache é invalidada automaticamente ao recompilar o próprio `emux`, mudar de arquitetura, mudar o formato de objeto ou usar `--force`. A chave inclui a versão do compilador, o `object_format`, a arquitetura e os caminhos de include.


## Versionamento

A versão vive num único sítio: [`version.json`](version.json). O CMake gera `Version.hpp` a partir dele, e a tag git `vX.Y.Z` tem de coincidir com o campo `compiler`. O `object_format` identifica o contrato Exir/`.emo`/ABI gerada e sobe quando esse contrato muda; foi o que aconteceu com a Exir v2 (de 1 para 2), com a instrução `copy` (de 2 para 3), com os globais `const` (de 3 para 4), com os inteiros multi-limb acima de 64 bits (de 4 para 5) e com a instrução `trunc` e os tipos `q<bits>.<scale>` (de 5 para 6), invalidando a cache de forma conservadora.

```bash
python tools/version.py show                  # versões atuais e tag esperada
python tools/version.py bump minor            # 0.3.0 -> 0.4.0
python tools/version.py check --tag v0.3.0    # valida uma tag de release
python tools/version.py tag                   # cria a tag anotada (sem push)
```

As regras completas - quando subir `patch`, `minor` ou `major`, quando subir `object_format`, como criar e publicar tags e o que é proibido - estão em [VERSIONING.md](VERSIONING.md).

### Binários pré-compilados

Publicar uma tag `vX.Y.Z` dispara [`.github/workflows/release.yml`](.github/workflows/release.yml), que compila em modo Release, corre `tests/smoke.sh` e anexa à GitHub Release um pacote `emux-X.Y.Z-windows-x86_64.zip` (mais `SHA256SUMS.txt`). O pacote contém `emux.exe`, as DLLs de runtime do MinGW (libstdc++, libgcc, libwinpthread) e a documentação.

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

O número após a letra define o tamanho em **bits** (letra minúscula: `u32`, `i16`, `b64`) ou em **bytes** (letra maiúscula: `U4`, `I8`, `B16`). Num bloco, `bN` são N bits e `BN` são N bytes.

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

`bN` declara uma região estática de N bits; `BN` usa N bytes. Ao contrário de um ponteiro, o bloco é dono do armazenamento: a variável é a própria região, reservada em `.bss`, e não um endereço.

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

- Blocos são variáveis globais estáticas, donas do armazenamento e não-const por omissão (ver `[[const]]` abaixo).
- Em contexto de valor (`return text`, `First(text)`, `text + 1`) o bloco decai para `u8*`, como um array em C++. `&text` dá o mesmo `u8*` e `*text` lê/escreve o primeiro byte.
- Em atribuição (`text = ...`) o lado direito tem de ser um literal de string (cópia com o NUL incluído) ou outro bloco que caiba. A cópia é emitida como `copy` na Exir e baixa para `rep movsb`.
- Os tamanhos são verificados em tempo de compilação: `text = "ABC"` num `B2` falha com `does not fit in buffer`.
- Parâmetros e retornos declarados como bloco decaem para `u8*`, tal como em C++. `sizeof` ainda não existe; o tamanho vem do tipo declarado.

### Declarações imutáveis (`[[const]]`)

`[[const]]` numa declaração torna a variável imutável e coloca os bytes iniciais em `.rdata` (só leitura):

```emux
[Main]

[[const]] var answer : i32 = -42
[[const]] var text : B8 = "AB"

func Start() i32 {
    return 0 - answer   // 42
}
```

Regras:

- O inicializador tem de ser literal: inteiro (sinal opcional) para escalares e string para blocos `b`.
- O valor tem de caber no tipo: `[[const]] var small : u8 = 300` falha com `does not fit`.
- A memória inicial é emitida como `db` em `.rdata`; blocos completam com zeros até à capacidade e escalares usam bytes little-endian.
- Sem inicializador, o bloco/escalar fica a zero em `.rdata`.
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

Em runtime, `uN`/`iN` aceitam qualquer `N` entre 1 e 65536. Um valor de até 64 bits vive num slot do tamanho do registo (8, 16, 32 ou 64 bits); acima disso é multi-limb, guardado em `ceil(N/8)` bytes contíguos, em complemento para dois e little-endian. O armazenamento acompanha o tipo exato: globais ocupam o número de bytes do tipo em `.bss` (`resb` para tipos largos), `[[const]]` vai para `.rdata` com os bytes iniciais, e os temporários recebem um slot de frame do tamanho do tipo (arredondado a 8 bytes) em vez dos 8 bytes de um escalar.

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

Acima de 64 bits o suporte é exclusivo de x86_64; em x86, declarar ou converter para um tipo acima de 64 bits é diagnosticado, tal como o são parâmetros, retornos e argumentos largos nas duas arquiteturas (a ABI gerada continua a passar inteiros num único slot). Larguras até 64 bits, incluindo as não nativas, funcionam nas duas arquiteturas (em x86, 33..63 bits usam um par `edx:eax`). Larguras fora de 1..65536 (ex.: `u0`, `u65537`) são rejeitadas pelo parser de tipos.

### Float e ponto fixo (preparação)

Os tipos `fN` (float de `N` bits) e `q<bits>.<scale>` (ponto fixo com `scale` bits fracionários, ex.: `q32.16`) já são reconhecidos pelo parser, resolvidos pela semântica e serializados na Exir; `q<bits>.<scale>` é também o token de tipo do formato de objeto 6. Ainda não há codegen: qualquer uso de runtime — atribuição, aritmética, casts, parâmetros, retornos ou argumentos — falha com um diagnóstico explícito de "not supported yet", em vez de ser silenciosamente tratado como inteiro. Declarações de variáveis, globais e ponteiros para estes tipos são aceites, o que permite preparar a representação e os testes sem gerar código inválido. O parser valida `0 < scale < bits` e limita a largura a 65536 bits.

### Exir v2 (IR lógica)

O pipeline é Lexer → Preprocessor → Parser → SemanticAnalyzer → IRBuilder → Exir v2 → backend → NASM → gcc.

A Exir deixou de ser uma lista de instruções de máquina e passou a ser uma IR lógica, tipada e independente da arquitetura, inspirada no TAC do LLVM:

- temporários `%tN` guardam resultados e cada instrução carrega o seu tipo;
- parâmetros e variáveis globais vivem em memória, com `load`/`store`/`storev`/`addr` explícitos;
- blocos `b` copiam-se com `copy <destino>, <origem>, <bytes>`;
- globais `[[const]]` usam `global <nome> : <tipo> const <bytes>` e são emitidos em `.rdata`;
- conversões são explícitas (`cast`), tal como a normalização de largura (`trunc <tipo> <valor>`);
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

Cada parâmetro e temporário ocupa um slot de 8 bytes na frame; não há ainda alocação de registradores. Essa escolha privilegia a correção e deixa a otimização para uma fase posterior.

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

A STL vive em [`stl/`](stl) na raiz do projecto e é procurada automaticamente: primeiro junto ao `emux.exe` (`<dir do exe>/stl`), depois na raiz do repositório em builds de desenvolvimento. Também pode ser usada com `@include` explícito:

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

Os wrappers aceitam ponteiros (`u8*`); passe um bloco `b` directamente (decai
para `u8*`) ou use `&variavel` para o endereço de uma variável declarada em
Emux. Projectos multificheiro podem incluir a raiz via `project.include`; em
modo de fonte única a STL é procurada junto ao `emux.exe` e na raiz do
repositório.
