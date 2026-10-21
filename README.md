# Emux

Emux é uma linguagem de programação focada no desenvolvimento de emuladores e sistemas de médio nível (entre Assembly e C++).

## Compilação e Uso

```bash
# Compilar para x86_64 (padrão)
./emux arquivo.ex -o output

# Compilar para x86 (32 bits)
./emux arquivo.ex -a x86 -o output
```

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
