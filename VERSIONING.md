# Política de versionamento da Emux

Este documento é normativo. Ele define o que cada número de versão significa,
quando ele muda, como as tags são criadas e como as versões são propagadas entre
o repositório, o CMake, o código C++ e o GitHub.

## 1. Princípio: uma única fonte de verdade

- [`version.json`](version.json) é a **única** fonte de verdade.
- O CMake lê esse ficheiro em [`cmake/Version.cmake`](cmake/Version.cmake) e gera
  [`Version.hpp`](cmake/Version.hpp.in) no diretório de build. O cabeçalho
  `include/Emux/Core/Version.hpp` **não é versionado** e não deve ser recriado à mão.
- A tag git `vX.Y.Z` tem de coincidir exatamente com o campo `compiler` de
  `version.json`. A verificação é feita por `tools/version.py` e pela CI.
- Nenhum número de versão deve ser duplicado em `CMakeLists.txt`, no README ou
  em código. Todos derivam de `version.json`.

## 2. Os números e os contratos que eles protegem

| Número | Campo | Onde aparece | Contrato |
| --- | --- | --- | --- |
| Versão do compilador | `compiler` | `version.json`, tag `vX.Y.Z`, `project(VERSION)`, `kEmuxVersion`, `emux --version` | SemVer do compilador |
| Formato de objeto | `object_format` | `version.json`, `kObjectFormatVersion`, chave de cache | Exir + `.emo` + ABI gerada |
| Versão da linguagem | derivada | documentação | enquanto 0.x, é `major.minor` do compilador |

Enquanto a Emux estiver em `0.y`, a linguagem acompanha a versão do compilador:
a Emux 0.2 implementa a linguagem Emux 0.2. A partir de 1.0, uma mudança
incompatível na linguagem exige `major` (ou uma edição, se um dia existir
convivência entre versões da linguagem).

## 3. Versão do compilador (SemVer)

### 3.1 Antes de 1.0 (estado atual)

Não há promessa de compatibilidade. Ainda assim, o número tem significado:

- **`patch` (`0.y.Z`)**: correções que não alteram sintaxe, semântica, CLI,
  `project.json`, formato Exir, layout do `.emo` nem ABI gerada. Inclui
  diagnósticos, mensagens e correções internas sem mudança de contrato.
- **`minor` (`0.Y.0`)**: qualquer mudança incompatível ou que possa quebrar
  código, scripts ou cache existentes. Exemplos: nova palavra reservada,
  sintaxe ou atributo, mudança de semântica, mudança de IR/ABI, remoção ou
  alteração de flags da CLI, mudança em campos de `project.json`.
- **`major` (`1.0.0`)**: primeira versão estável.

`0.0.z` não é usado para releases; o primeiro número minor planeado é `0.2`.

### 3.2 A partir de 1.0

Passa a valer SemVer clássico:

- **major**: quebra de compatibilidade de código-fonte, CLI, `project.json` ou
  contrato de linking.
- **minor**: funcionalidade nova compatível com código existente. Uma palavra
  reservada nova que invalide identificadores existentes conta como major.
- **patch**: correções compatíveis.

### 3.3 O que conta como API pública

1. Sintaxe, semântica e atributos da linguagem.
2. Flags, variáveis de ambiente e códigos de saída da CLI.
3. Esquema de `project.json`.
4. ABI e símbolos gerados (`[[export]]`, `[[import]]`, convenções de chamada).
5. Formatos `.exir` e `.emo`.
6. O SDK em `examples/emux/sdk`.

O texto exato de um diagnóstico não é API estável; pode mudar num `patch`.

### 3.4 Pré-releases

- Tags de candidato: `vX.Y.Z-rc.N` (ex.: `v0.3.0-rc.1`).
- Pré-releases servem para estabilizar; não introduzem funcionalidades novas.
- Um `-rc.N` pode ser sucedido por `-rc.N+1`; um número de `rc` nunca é
  reutilizado.
- A versão estável é tagada depois de o candidato ser validado.
- Metadados de build (`+alguma-coisa`) não entram em tags. O CMake usa apenas
  a parte numérica `X.Y.Z` em `project(VERSION)`.

## 4. `object_format`

`kObjectFormatVersion` identifica o contrato dos artefactos em cache. Ele muda
**+1** sempre que qualquer um destes contratos mudar:

- gramática, tokens, diretivas ou semântica das instruções Exir;
- layout do `.emo`: magic, cabeçalho, campos JSON, algoritmo de hash do código;
- ABI gerada: convenções de chamada, decoração de símbolos, layout de frames
  assumido pelos objetos em cache;
- composição da chave de cache (o que entra nela).

Regras:

- O bump acontece **no mesmo commit/PR** que introduz a mudança de contrato.
- Não se faz bump por refactor interno com o mesmo formato e a mesma ABI, por
  mudanças de mensagens, documentação ou performance.
- O número nunca desce nem é reutilizado.
- Consumidores devem rejeitar versões desconhecidas. O `.emo` já faz isso em
  [`src/Core/ObjectFile.cpp`](src/Core/ObjectFile.cpp).
- Como a chave de cache inclui a versão do compilador, o `object_format`, a
  arquitetura, os caminhos de include e o timestamp do próprio executável, um
  bump invalida os módulos em cache de forma conservadora.

Histórico:

| Versão | Significado |
| --- | --- |
| 1 | IR antiga, próxima de instruções de máquina |
| 2 | Exir v2 tipada e independente da arquitetura (2026-10-30) |
| 3 | Exir com a instrução `copy` para blocos `b` (2026-10-31) |
| 4 | Globais `[[const]]` com bytes iniciais em `.rdata` (2026-10-31) |
| 5 | Inteiros multi-limb acima de 64 bits na Exir e na ABI gerada (2026-11-01) |
| 6 | Instrução Exir `trunc`, tipo `q<bits>.<scale>` e larguras inteiras 1..65536 (2026-11-01) |
| 7 | Instrução Exir `bitcast` e codegen de float/ponto fixo sobre o runtime C (2026-11-01) |
| 8 | Nivel de otimizacao (`-O`/`--opt` e `build.optimize`) na composicao da chave de cache; `Exir::Optimize` roda DCE/peephole antes da geracao (2026-11-03) |
| 9 | Comparacoes (`eq/ne/lt/gt/le/ge`) e instrucoes de salto (`label`/`jump`/`branch`/`branchnot`) na gramatica da Exir; base do `if`/`else`/`while`/`for` (2026-11-04) |
| 10 | Blocos `b` como memoria crua, com views/slices e indice por byte (2026-11-05) |
| 11 | Diretiva `local` que declara os slots do corpo na frame (2026-11-07) |
| 12 | Instrucao `zero` e blocos locais dentro da frame; views de bloco verificadas (2026-11-08) |
| 13 | Instrucao `cmpmem` (comparacao byte a byte de dois blocos) na gramatica da Exir (2026-11-09) |
| 14 | Mig para a Exir v3 em memoria (IR tipada como objetos, nao texto) e expansao da STL (2026-11-09) |
| 15 | Constantes float/fixed codificadas no builder, instrucao `conv` e `Function::Convention` propagada; muda a ABI gerada (`ret N`, decoracao fastcall) (2026-11-13) |

O bump de `1` para `2` foi feito precisamente porque a Exir v2 mudou a
gramática e a semântica da IR guardada nos `.emo`; sem o bump, a cache poderia
reutilizar IR antiga com o backend novo. O bump de `2` para `3` acompanha a
nova instrução Exir `copy <dest>, <origem>, <bytes>`, sem a qual objetos em
cache não conseguem exprimir a cópia de strings para blocos `b`. O bump de `3`
para `4` acompanha a forma `global <nome> : <tipo> const <bytes>`, que marca um
global imutável e o coloca em `.rdata`. O bump de `4` para `5` acompanha a nova
semântica de tipos com mais de 64 bits: `const`, `load`, `store`, `cast` e as
operações binárias passaram a operar sobre `n` bytes contíguos (`n = bits/8`),
com slots de frame dimensionados pelo tipo e helpers multi-limb na ABI gerada;
IR antiga guardada em cache assumia um único slot de 8 bytes para qualquer
inteiro e não pode ser reaproveitada. O bump de `5` para `6` acompanha duas
mudanças de gramática: a instrução `trunc <tipo> <valor>`, que normaliza um
inteiro à sua largura (máscara dos bits acima de `Bits` e extensão de sinal no
slot), e o token de tipo `q<bits>.<scale>` do ponto fixo. A partir do formato 6,
qualquer largura inteira entre 1 e 65536 é válida; larguras que não coincidem
com a do slot (`u3`, `i7`, `u24`, `u40`, `u65`, `i100`, `i129`) dependem de
`trunc` para que a semântica de largura fixa se mantenha, por isso a IR antiga
não é equivalente. O bump de `6` para `7` acompanha a instrução
`bitcast <tipo> <valor>`, usada pelo `reinterpret_cast` entre tipos do mesmo
tamanho, e o codegen de float/ponto fixo: a IR passou a conter chamadas ao
runtime C (`__emux_f_*`, `__emux_q_*`, `__emux_wsrem`) com codificações
empacotadas de largura/escala e a ABI gerada passou a passar valores float e de
ponto fixo de até 64 bits em registos inteiros nas funções Emux. IR antiga não
tem essas instruções nem essa convenção, por isso não pode ser reutilizada. O
bump de `7` para `8` acompanha a mudança da composição da chave de cache: o nível
de otimização (`-O`/`--opt` e `build.optimize`) passou a entrar nela, porque
`Exir::Optimize` (dead code elimination em `-O1`, mais o peephole de casts
identidade em `-O2`) altera o IR e o código gerado para a mesma fonte. A gramática
Exir e o layout do `.emo` mantêm-se; um objeto em cache do formato 7 não pode ser
reaproveitado por uma build `-O1`/`-O2`, e vice-versa, por isso a invalidação é
conservadora.

O bump de `8` para `9` acompanha uma mudança real da gramática da Exir: os
opcodes de comparação `eq/ne/lt/gt/le/ge` e os kinds de instrução
`label <nome>`, `jump <nome>` e `branch`/`branchnot <tipo> <c>, <alvo>`, que
sustentam o `if`/`else`/`while`/`for`. Em `-O0`, e para todos os programas sem
controlo de fluxo, o código gerado continua byte a byte idêntico; mas um `.emo`
ou `.exir` gravado por esta build e que contenha comparações ou saltos não é
interpretável por um parser anterior. Como o compilador se mantém em `0.5.0`
(sem bump de versão, porque não foi pedido corte de release), o `object_format`
é o único mecanismo que invalida a cache antiga.

O bump de `14` para `15` acompanha duas mudanças de semântica da Exir v3 que
alteram o código gerado para a mesma fonte. Primeiro, os literais `float`/
`fixed` passam a chegar ao IR já codificados no padrão de bits do destino
(`store f32 1092616192` é `10.0f`, não o inteiro `10`), e as mudanças de classe
numérica ou de escala passam a ser uma instrução `conv` explícita em vez de uma
reatribuição de tipo. Segundo, `Exir::Function::Convention` é agora preenchida
pelo builder a partir de `[[stdcall]]`/`[[fastcall]]`, pelo que o gerador emite
`ret N` e decora os símbolos (`@nome@N`) em conformidade. Um `.emo` gravado
pelo formato 14 contém IR que o backend de hoje interpreta com os valores
errados, por isso a cache tem de ser invalidada.

## 5. Tags e releases

### 5.1 Formato

- `vX.Y.Z` para estáveis e `vX.Y.Z-rc.N` para candidatos.
- Tag **anotada**, com mensagem `Emux X.Y.Z`.
- A tag aponta para o commit de release já presente em `main`.

### 5.2 Regras

- Só se cria tag a partir de `main` (com exceção explícita para um ramo de
  release, usando `--allow-branch`).
- A árvore de trabalho tem de estar limpa.
- `version.json` tem de coincidir com a tag.
- A CI de versões tem de estar verde e os testes locais (`tests/smoke.sh`) têm
  de passar.
- Tags são imutáveis: nunca se move, apaga, reescreve ou reutiliza. Se uma
  release saiu errada, publica-se a release seguinte (`patch` ou `rc`).
- O push é um passo manual e explícito; `tools/version.py tag` cria a tag
  apenas localmente e mostra o comando de push.

### 5.3 Fluxo de release

1. Se o PR alterou o contrato Exir/`.emo`/ABI, corre
   `python tools/version.py bump object` nesse mesmo PR.
2. No corte da release, com `main` limpa, corre
   `python tools/version.py bump minor` (ou `patch`/`major`).
3. Atualiza a documentação visível ao utilizador (README, Ideias.txt, SDK).
4. Corre os testes e `python tools/version.py check`.
5. Faz commit de release (ex.: `chore: release 0.3.0`).
6. Cria a tag com `python tools/version.py tag`.
7. Publica primeiro os commits e depois a tag:
   `git push origin main` e `git push origin v0.3.0`.
8. O push da tag dispara a release: a CI compila, testa e publica os
   binários pré-compilados (ver 5.4).

Não se faz bump de `compiler` em cada mudança: o número é decidido no corte da
release. A exceção é `object_format`, que é um invariante do formato e sobe no
PR que o altera.

Não há cadência fixa: uma release é cortada quando existe um conjunto coerente
de mudanças e a CI está verde. Mudanças apenas de documentação, CI ou
formatação não justificam uma release.

### 5.4 Artefactos de release

A release é produzida por [`.github/workflows/release.yml`](.github/workflows/release.yml),
disparada por um push de tag `v*`:

1. valida a tag contra `version.json`;
2. configura e compila com `CMAKE_BUILD_TYPE=Release`;
3. corre `tests/smoke.sh` contra o binário Release;
4. empacota `emux-X.Y.Z-windows-x86_64.zip` com `emux.exe`, as DLLs de runtime
   do MinGW usadas pelo binário (libstdc++, libgcc, libwinpthread), `README.md`
   e `VERSIONING.md`;
5. gera `SHA256SUMS.txt` e anexa ambos à GitHub Release, marcando `-rc.N` como
   pré-release.

O pacote usa o ambiente MSYS2 UCRT64. O compilador pré-compilado não embute o
assembler nem o linker: continua a invocar `nasm` e `gcc` do sistema, tal como
a compilação local. Para produzir executáveis x86 de 32 bits, o MinGW 32-bit
tem de estar disponível no `PATH`.

O workflow usa os pacotes nativos `mingw-w64-ucrt-x86_64-cmake` e
`mingw-w64-ucrt-x86_64-ninja`, não as variantes MSYS de `cmake`/`ninja`: a
variante MSYS ficou presa no probe de flags do asmjit em runners alojados. Cada
passo tem `timeout-minutes`, para que uma configuração presa falhe em vez de
ficar indefinidamente em execução.

Os binários são artefactos derivados da tag; nunca são o que define a versão.
Uma release malsucedida não se corrige movendo a tag: publica-se a release
seguinte.

## 6. Proibições

- Editar ou voltar a versionar `include/Emux/Core/Version.hpp`.
- Escrever a versão à mão em `CMakeLists.txt` ou duplicá-la noutro ficheiro.
- Criar tag com a árvore suja, fora de `main` ou sem CI verde.
- Reutilizar, mover ou apagar tags.
- Fazer bump de `object_format` sem mudança de contrato (invalida cache sem
  motivo) ou mudar o contrato sem bump (cache com código antigo).
- Fazer `git push --force` de tags ou de `main`.

## 7. Ferramentas

Fonte única: [`version.json`](version.json).

```bash
python tools/version.py show                  # mostra versões e tag esperada
python tools/version.py check                 # valida JSON, CMake e cabeçalho gerado
python tools/version.py check --tag v0.3.0    # valida uma tag de release
python tools/version.py bump patch            # 0.2.0 -> 0.2.1
python tools/version.py bump minor            # 0.2.0 -> 0.3.0
python tools/version.py bump major            # 0.2.0 -> 1.0.0
python tools/version.py bump object           # 3 -> 4 (mudança de contrato)
python tools/version.py set 0.3.0-rc.1        # define a versão (ex.: candidato)
python tools/version.py tag                   # cria a tag anotada vX.Y.Z (sem push)
```

A CI em [`.github/workflows/version.yml`](.github/workflows/version.yml) corre
`check` em pushes e pull requests para `main` e valida a igualdade entre a tag e
`version.json` quando uma tag `v*` é publicada. A CI em
[`.github/workflows/release.yml`](.github/workflows/release.yml) compila, testa
e publica os binários da tag.

## 8. Estado atual

- Compilador: `0.6.x`.
- Formato de objeto: `15` (Exir v3 em memória, constantes float/fixed
  codificadas no builder, instrução `conv` e convenção de chamada propagada).
- Tags publicadas: `v0.2.0` até `v0.6.0`. A `v0.2.1` não tem release: o job de
  CI ficou preso no configure e não publicou artefactos. A `v0.2.2` contém o
  mesmo código e é a primeira release com binários pré-compilados, conforme a
  regra de release falhada em 5.4.
- A migração para a Exir v3 em memória entrou no `object_format` `14`; a
  correção de tipos numéricos no builder e a propagação da convenção de
  chamada entram no `object_format` `15`.
