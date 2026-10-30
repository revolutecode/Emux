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

O bump de `1` para `2` foi feito precisamente porque a Exir v2 mudou a
gramática e a semântica da IR guardada nos `.emo`; sem o bump, a cache poderia
reutilizar IR antiga com o backend novo.

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
8. Cria a GitHub Release com as notas da versão (recomendado).

Não se faz bump de `compiler` em cada mudança: o número é decidido no corte da
release. A exceção é `object_format`, que é um invariante do formato e sobe no
PR que o altera.

Não há cadência fixa: uma release é cortada quando existe um conjunto coerente
de mudanças e a CI está verde. Mudanças apenas de documentação, CI ou
formatação não justificam uma release.

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
python tools/version.py bump object           # 2 -> 3 (mudança de contrato)
python tools/version.py set 0.3.0-rc.1        # define a versão (ex.: candidato)
python tools/version.py tag                   # cria a tag anotada vX.Y.Z (sem push)
```

A CI em [`.github/workflows/version.yml`](.github/workflows/version.yml) corre
`check` em pushes e pull requests para `main` e valida a igualdade entre a tag e
`version.json` quando uma tag `v*` é publicada.

## 8. Estado atual

- Compilador: `0.2.0`.
- Formato de objeto: `2` (Exir v2).
- Ainda não existem tags no repositório; a primeira será `v0.2.0`, criada a
  partir do commit que introduziu esta política.
