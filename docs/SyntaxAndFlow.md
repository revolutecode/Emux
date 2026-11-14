# Sintaxe e Estrutura da Linguagem Emux

Este guia cobre a gramática essencial, controle de fluxo, escopos, funções e atributos da linguagem Emux.

---

## 1. Escopos e Seções

Emux organiza o código global em seções identificadas por colchetes:
```emux
[Main]

global total : i32 = 0

global Start() i32 {
    return total
}
```

- Cada seção cria um namespace/escopo global.
- `[Main]` é a seção padrão onde reside o ponto de entrada `Start()` ou `_Start()`.
- O acesso a símbolos de outras seções ou módulos segue a sintaxe `Secao::simbolo` (ex.: `std::println`).

---

## 2. Declarações e Visibilidade (`global` e `local`)

O Emux unifica as declarações em torno do escopo e visibilidade, eliminando redundâncias.

### 2.1 Funções

Funções são declaradas diretamente com o modificador de visibilidade desejado:
```emux
[Main]

# Função pública/exportada (visível para outros módulos e ligada no linker)
global Calcular(i32 base, i32 multiplicador) i32 {
    return base * multiplicador
}

# Função privada/local (visível apenas dentro deste módulo/arquivo)
local AuxiliarInterno(i32 valor) i32 {
    return valor + 1
}

global Start() i32 {
    return Calcular(10, 2)
}
```

> **Nota de Compatibilidade:** A palavra-chave legada `func` continua sendo aceita como atalho para `global`.

### 2.2 Variáveis de Seção / Globais

No escopo de seção (nível de módulo):
```emux
# Variável exportada para o linker
global contador : i32 = 0

# Variável privada ao módulo atual (não exportada no objeto NASM)
local estado_interno : u32 = 100

# Variável global constante imutável (reside em .rdata)
[[const]] global LIMITE : u32 = 1000
```
- Variáveis globais e de seção residem em `.bss` (não inicializadas), `.data` (mutáveis inicializadas) ou `.rdata` (`[[const]]`).
- A palavra-chave legada `var` é tratada como `global` no nível de seção.

### 2.3 Variáveis de Função / Stack (`local`)

Dentro do corpo de funções, todas as variáveis locais da moldura de pilha (*stack frame*) devem ser declaradas obrigatoriamente com `local` e tipo explícito:
```emux
global Processar(i32 base) i32 {
    local fator : i32 = 2
    local resultado : i32 = base * fator
    return resultado
}
```
- Declarações dentro de funções sempre exigem tipo explícito (`local nome : Tipo = valor`).
- A palavra-chave `global` é **proibida** dentro do corpo de funções.

---

## 3. Controle de Fluxo

### Condicionais (`if` / `else`)
```emux
if (x > 10) {
    # branch verdadeiro
} else if (x == 10) {
    # branch intermediário
} else {
    # branch falso
}
```

### Laços de Repetição (`while` e `for`)

```emux
# Laço while
while (i < 100) {
    i += 1
    if (i == 50) {
        break
    }
}

# Laço for
# O cabeçalho do for aceita uma expressão de inicialização, condição e passo:
local j : i32 = 0
for (j = 0; j < 10; j += 1) {
    if (j == 5) {
        continue
    }
}
```

### Operadores Lógicos e Curto-Circuito
- `&&` e `||` realizam avaliação em curto-circuito (*short-circuit*).
- Operandos lógicos devem ser inteiros ou ponteiros.

---

## 4. Enums

```emux
enum Status : u32 {
    Ok = 0,
    Warn = 1,
    Fail = 2
}

# Com auto-incremento customizado
enum Passo : i32 {
    [[step(2)]]
    A = 10,  # 10
    B,       # 12
    C        # 14
}
```

---

## 5. Atributos da Linguagem (`[[...]]`)

Os atributos personalizam comportamentos do compilador e geração de código:

- `[[const]]`: Declaração imutável colocada na seção `.rdata`.
- `[[restrict]]`: Força verificação estrita de identidade de tipos no `using`.
- `[[unrestrict]]`: Permite conversão estrutural livre entre tipos semelhantes.
- `[[nowarn]]`: Suprime avisos do analisador semântico para a entidade anotada.
- `[[noexcept]]`: Anotação informando que a rotina não lança exceções.
- `[[import("dll")]]`: Importa uma função externa de biblioteca compartilhada (ex: `user32`, `kernel32`).
- `[[export]]`: Exporta o símbolo explicitamente para a tabela de exportação do executável.
- `[[cdecl]]`, `[[stdcall]]`, `[[fastcall]]`: Especifica a convenção de chamada nativa.
- `[[frame]]`: Garante slot de moldura estável para variável local de pilha.
- `[[noframe]]`: Garante que a função não cria frame de pilha (funções folhas puras).
