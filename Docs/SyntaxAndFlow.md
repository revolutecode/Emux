# Sintaxe e Estrutura da Linguagem Emux

Este guia cobre a gramática essencial, controle de fluxo, escopos, funções e atributos da linguagem Emux.

---

## 1. Escopos e Seções

Emux organiza o código global em seções identificadas por colchetes:
```emux
[Main]

var total : i32 = 0

func Start() i32 {
    return total
}
```

- Cada seção cria um namespace/escopo global.
- `[Main]` é a seção padrão onde reside o ponto de entrada `Start()` ou `_Start()`.

---

## 2. Declaração de Variáveis

### Variáveis de Seção / Globais (`var`)
```emux
var contador : i32 = 0
[[const]] var limite : u32 = 1000
```
- Globais vivem em `.bss` (não inicializadas), `.data` (mutáveis inicializadas) ou `.rdata` (`[[const]]`).
- `var` só é permitido no escopo da seção, fora do corpo de funções.

### Variáveis de Função / Stack (`local`)
Dentro de funções, variáveis na pilha devem ser declaradas obrigatoriamente com a palavra-chave `local`:
```emux
func Calcular(i32 base) i32 {
    local fator : i32 = 2
    local resultado = base * fator
    return resultado
}
```

---

## 3. Controle de Fluxo

### Condicionais (`if` / `else`)
```emux
if (x > 10) {
    # branch verdadeiro
} else if (x == 10) {
    # branch intermediario
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

# Laço for com acumulador
for (local j : i32 = 0; j < 10; j += 1) {
    if (j == 5) {
        continue
    }
}
```

### Operadores de Curto-Circuito
- `&&` e `||` realizam avaliação em curto-circuito (*short-circuit*).

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

- `[[const]]`: Declaração imutável colocada em `.rdata`.
- `[[restrict]]`: Força verificação estrita de identidade de tipos no `using`.
- `[[unrestrict]]`: Permite conversão estrutural livre entre tipos semelhantes.
- `[[nowarn]]`: Suprime avisos do analisador semântico para a entidade anotada.
- `[[noexcept]]`: Anotação informando que a rotina não falha.
- `[[import("dll")]]`: Importa uma função externa de biblioteca compartilhada (ex: `user32`, `kernel32`).
- `[[export]]`: Exporta o símbolo para a tabela de exportação do PE/ELF.
- `[[cdecl]]`, `[[stdcall]]`, `[[fastcall]]`: Especifica a convenção de chamada da função.
