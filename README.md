# Montador e Ligador — Software Básico (UnB)

Este projeto implementa, em **C (padrão C99)**, um **Montador (`montador`)** e um **Ligador (`ligador`)** para a linguagem assembly hipotética da disciplina de *Software Básico* da Universidade de Brasília (UnB), conforme definido na especificação (`especs.tex`).

---

## 📋 O que o Projeto Faz

### 1. Montador (`montador`)
Traduz programas escritos na linguagem assembly hipotética (`.asm`) para arquivos objeto (`.obj`):
- **Pré-processamento**: Processa as diretivas `EQU` (substituição de constantes simbólicas declaradas antes das seções) e `IF` (inclusão condicional da próxima linha lógica dentro da seção `TEXT`).
- **Montagem em Duas Passagens (com bufferização de seções)**:
  - Aceita `SECTION TEXT` / `SECAO TEXT` e `SECTION DATA` / `SECAO DATA` em qualquer ordem no código-fonte, garantindo que no arquivo objeto final as palavras de `DATA` (`CONST` e `SPACE`) fiquem sempre posicionadas após as instruções de `TEXT`.
  - Suporta rótulos em linhas isoladas (`ROTULO:\n INSTRUCAO`), indexação de vetores (`X+2` ou `X + 2`) e comentários iniciados por `;`.
  - **Modo Programa Independente** (sem `BEGIN`/`END`): Gera um arquivo `.obj` contendo apenas a linha de código de máquina separada por espaços, pronto para execução no simulador.
  - **Modo Módulo Relocável** (com `BEGIN`/`END`, `PUBLIC`, `EXTERN`): Gera um arquivo `.obj` contendo a Tabela de Definições (`D,`), a Tabela de Uso (`U,`), o Mapa de Bits de Relocação (`R,`) e o código objeto do módulo.
- **Análise Completa de Erros**: Analisa todo o arquivo-fonte sem interromper no primeiro erro, reportando na saída padrão a linha original e o tipo de cada erro encontrado (`LEXICO`, `SINTATICO` ou `SEMANTICO`).

### 2. Ligador (`ligador`)
Recebe de **1 a 3 módulos** (`.asm` ou `.obj`) na linha de comando, invoca automaticamente o `./montador` para montar os arquivos `.asm` em `.obj`, resolve referências cruzadas (`PUBLIC` / `EXTERN`) através da Tabela Global de Símbolos (TSG), aplica os fatores de correção de relocação de cada módulo e gera o arquivo executável final (`.lig`).

---

## 📁 Estrutura de Arquivos

| Arquivo | Descrição |
| :--- | :--- |
| `common.h` | Cabeçalho compartilhado com constantes, tabela de instruções, estruturas e funções utilitárias. |
| `montador.c` | Implementação do pré-processador e montador. |
| `ligador.c` | Implementação do ligador de módulos (até 3 arquivos). |
| `Makefile` | Script de compilação (`make` e `make clean`). |
| `especs.tex` | Especificação oficial do trabalho. |
| `asm_sem_ligacao` / `obj_sem_lig_sem_exec` | Exemplo de entrada e saída esperada para montagem sem ligação. |
| `asm_ligacao` / `obj_com_lig` | Exemplo de entrada e saída esperada para montagem de módulo com tabelas `D`, `U` e `R`. |
| `simulador/simulador` | Simulador executável (Linux x86_64) fornecido para testar arquivos `.obj` e `.lig`. |

---

## 🛠️ Compilação

Certifique-se de ter o `gcc` e o `make` instalados. Para compilar tanto o `montador` quanto o `ligador`, execute na raiz do projeto:

```bash
make
```

Isso gerará os binários `./montador` e `./ligador`.

Para remover os binários compilados e arquivos `.obj` / `.lig` gerados:

```bash
make clean
```

---

## 🚀 Como Usar

### 1. Montar um único arquivo (`.asm` -> `.obj`)

Passe o arquivo de entrada como argumento para `./montador`:

```bash
./montador meu_programa.asm
```

- O montador gerará automaticamente o arquivo `meu_programa.obj` (substituindo a extensão por `.obj`, ou adicionando `.obj` caso o arquivo não possua extensão).
- Exemplo com os arquivos de referência do projeto:
  ```bash
  ./montador asm_sem_ligacao
  # Gera asm_sem_ligacao.obj

  ./montador asm_ligacao
  # Gera asm_ligacao.obj (com tabelas D, U, R)
  ```
- Caso existam erros léxicos, sintáticos ou semânticos, eles serão exibidos no terminal indicando a linha e a natureza do erro, e o arquivo `.obj` não será gerado.

### 2. Montar e ligar múltiplos módulos (`.asm` -> `.lig`)

Passe de 1 a 3 arquivos para `./ligador`:

```bash
./ligador prog1.asm prog2.asm prog3.asm
```

- O `ligador` chamará `./montador` internamente para cada arquivo `.asm`, lerá os arquivos `.obj` correspondentes e criará o executável final com o nome do primeiro arquivo e extensão `.lig` (neste exemplo, `prog1.lig`).

### 3. Executar no Simulador

Para testar um arquivo objeto independente (`.obj` sem ligação) ou um executável ligado (`.lig`) no simulador:

```bash
chmod +x simulador/simulador
./simulador/simulador asm_sem_ligacao.obj
```

Ou para um programa ligado:

```bash
./simulador/simulador prog1.lig
```

---

## 📖 Resumo da Linguagem Suportada

### Conjunto de Instruções

| Mnemônico | Opcode | Operandos | Tamanho (Palavras) | Operação |
| :--- | :---: | :---: | :---: | :--- |
| `ADD` | `1` | 1 | 2 | `ACC <- ACC + mem(OP)` |
| `SUB` | `2` | 1 | 2 | `ACC <- ACC - mem(OP)` |
| `MULT` | `3` | 1 | 2 | `ACC <- ACC * mem(OP)` |
| `DIV` | `4` | 1 | 2 | `ACC <- ACC / mem(OP)` |
| `JMP` | `5` | 1 | 2 | `PC <- OP` |
| `JMPN` | `6` | 1 | 2 | `Se ACC < 0, então PC <- OP` |
| `JMPP` | `7` | 1 | 2 | `Se ACC > 0, então PC <- OP` |
| `JMPZ` | `8` | 1 | 2 | `Se ACC == 0, então PC <- OP` |
| `COPY` | `9` | 2 | 3 | `mem(OP2) <- mem(OP1)` (separados por espaço, sem vírgula) |
| `LOAD` | `10` | 1 | 2 | `ACC <- mem(OP)` |
| `STORE` | `11` | 1 | 2 | `mem(OP) <- ACC` |
| `INPUT` | `12` | 1 | 2 | `mem(OP) <- entrada padrão` |
| `OUTPUT` | `13` | 1 | 2 | `saída padrão <- mem(OP)` |
| `STOP` | `14` | 0 | 1 | Encerra a execução |

### Diretivas

- **`SECTION TEXT` / `SECAO TEXT`**: Inicia a seção de código.
- **`SECTION DATA` / `SECAO DATA`**: Inicia a seção de dados.
- **`SPACE [N]`**: Reserva `1` (ou `N`) palavras de memória na seção `DATA`, inicializadas com `0`.
- **`CONST <N>`**: Reserva `1` palavra de memória na seção `DATA` com o valor inteiro decimal `<N>` (positivo ou negativo).
- **`EQU <N>`**: Associa um rótulo a uma constante inteira antes de qualquer seção (ex.: `FLAG: EQU 1`).
- **`IF <OP>`**: Na seção `TEXT`, inclui a próxima instrução somente se `<OP>` for diferente de `0`.
- **`BEGIN` / `END`**: Delimitam um módulo para ligação (suporta `MOD: BEGIN`, `BEGIN: MOD` ou `BEGIN`).
- **`PUBLIC <ROTULO>`**: Torna um símbolo definido no módulo visível para outros módulos.
- **`EXTERN` (`<ROTULO>: EXTERN` ou `EXTERN <ROTULO>`)**: Declara que um símbolo é definido em outro módulo.

