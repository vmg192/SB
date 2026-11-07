# Implementation Plan: Assembler (`montador`) and Linker (`ligador`) in C

## Goal Description
Implement in **C (C99/C11)** the complete translation toolchain described in `especs.tex` for the UnB *Software Básico* hypothetical assembly language:
1. **`montador`**: A preprocessor (`EQU`, `IF`) and 2-pass (section-buffered) assembler that translates `.asm` source files into `.obj` text files, supporting both standalone programs (like `asm_sem_ligacao` -> `obj_sem_lig_sem_exec`) and relocatable modules with `BEGIN`, `END`, `PUBLIC`, and `EXTERN` (like `asm_ligacao` -> `obj_com_lig`), while reporting all lexical, syntactic, and semantic errors across the entire file with original line numbers.
2. **`ligador`**: A linker accepting 1 to 3 `.asm` files (`./ligador prog1.asm [prog2.asm] [prog3.asm]`), invoking `./montador` on each input file to produce `.obj` files, and linking them into a single executable `.lig` file containing a single space-separated line of machine words with `DATA` sections placed after `TEXT` sections.

---

## Answers to Your Questions & Key Design Decisions

### 1. Why Plain C is Ideal Here
Since labels are bounded to 30 characters (`especs.tex` line 35) and the instruction set has 14 instructions and 8 directives, **Plain C (C99)** is very clean and natural for this project. Using straightforward `struct` tables and standard C library functions (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`) keeps the code easy for you to read, debug, and explain without needing any C++ features.

### 2. What `especs.tex` says about Output Filenames & Extensions
- **For `montador` (`especs.tex` line 64):**
  *"O programa deve chamar 'montador' e receber o arquivo de entrada (com extensão) por argumento na linha de comando (ex: `./montador myprogram.asm`). O programa deve dar como saída o arquivo com o mesmo nome MUDANDO a extensão para OBJ."*
- **For `ligador` (`especs.tex` line 84):**
  *"O ligador deve funcionar `./ligador prog1.asm prog2.asm prog3.asm` (no máximo 3 programas). O ligador deve internamente chamar o montador para criar os arquivos objetos separadamente para cada arquivo ASM... o seu próprio ligador deve também criar o arquivo .LIG que é o executável no mesmo formato..."*
- **How we will handle extensions safely on Linux:**
  In the specification prose, extensions are written in uppercase (`OBJ`, `.LIG`, `ASM`), while command-line examples use lowercase (`myprogram.asm`, `prog1.asm`). By default, we will replace the extension of `myprogram.asm` with `.obj` (and name the linked executable `<prog1_basename>.lig`), or if the input file had no extension (like the example files `asm_sem_ligacao`), append `.obj` / `.lig`.

---

## Architecture & Translation Pipeline

1. **`./montador prog.asm`**:
   - **Stage 1 — Lexer & Line Normalizer**: Case-folding to uppercase, stripping `;` comments, joining multiline standalone labels (`LABEL:\n INSTR ...`) while preserving original line numbers.
   - **Stage 2 — Preprocessor (`EQU` & `IF`)**: Validates `EQU` before any section, substitutes `EQU` symbols, evaluates `IF <cond>` inside `SECTION TEXT` to include or skip the next logical line.
   - **Stage 3 — Pass 1 (Symbol, Definition `D` & Extern Tables)**: Buffers `TEXT` and `DATA` statements so that `DATA` always gets addresses after `TEXT` (even if `SECAO DATA` is written before `SECAO TEXT` in the `.asm` file).
   - **Stage 4 — Pass 2 (Code Synthesis, Use Table `U`, Relocation Bitmap `R`, Error Checks)**: Resolves operand addresses and array offsets (`X+2` or `X + 2`), reports all `LEXICO`, `SINTATICO`, and `SEMANTICO` errors with original line numbers, and writes `.obj`.

2. **`./ligador p1.asm [p2.asm] [p3.asm]`**:
   - **Step 1**: Invokes `./montador` on each input `.asm` file to generate `.obj` files.
   - **Step 2 — Pass 1 (Global Symbol Table & Section Correction Factors)**: Computes relocation offsets so all modules' `TEXT` words come first and all modules' `DATA` words come after `TEXT`, building the Global Symbol Table from each module's `D` table.
   - **Step 3 — Pass 2 (Relocation & External Resolution)**: Relocates internal addresses using `R`, resolves external symbols using `U`, and writes the single-line executable `<p1>.lig`.

---

## 1. Instruction & Directive Specification

| Mnemonic | Opcode | Operands | Size (Words) | Description |
| :--- | :---: | :---: | :---: | :--- |
| `ADD` | `1` | 1 | 2 | `ACC <- ACC + mem(OP)` |
| `SUB` | `2` | 1 | 2 | `ACC <- ACC - mem(OP)` |
| `MULT` | `3` | 1 | 2 | `ACC <- ACC * mem(OP)` |
| `DIV` | `4` | 1 | 2 | `ACC <- ACC / mem(OP)` |
| `JMP` | `5` | 1 | 2 | `PC <- OP` |
| `JMPN` | `6` | 1 | 2 | `if (ACC < 0) PC <- OP` |
| `JMPP` | `7` | 1 | 2 | `if (ACC > 0) PC <- OP` |
| `JMPZ` | `8` | 1 | 2 | `if (ACC == 0) PC <- OP` |
| `COPY` | `9` | 2 | 3 | `mem(OP2) <- mem(OP1)` (space-separated, no comma) |
| `LOAD` | `10` | 1 | 2 | `ACC <- mem(OP)` |
| `STORE` | `11` | 1 | 2 | `mem(OP) <- ACC` |
| `INPUT` | `12` | 1 | 2 | `mem(OP) <- stdin` |
| `OUTPUT` | `13` | 1 | 2 | `stdout <- mem(OP)` |
| `STOP` | `14` | 0 | 1 | Halts execution |

**Directives:**
- `SECTION TEXT` / `SECAO TEXT` and `SECTION DATA` / `SECAO DATA` (accepted in any order in source; `TEXT` always placed before `DATA` in object output).
- `SPACE [N]`: Allocates 1 word (or `N` words if an integer argument is given) initialized to `0`.
- `CONST <N>`: Allocates 1 word initialized to signed decimal integer `<N>`.
- `EQU <N>`: Defines a preprocessing constant before any section (`LABEL: EQU 1`).
- `IF <SYM>`: Conditionally includes the next logical statement in `SECTION TEXT` if `<SYM>` evaluates to non-zero.
- `BEGIN` (supports `MOD: BEGIN`, `BEGIN: MOD`, and `BEGIN`), `END`, `PUBLIC <SYM>` (or `<SYM>: PUBLIC`), and `<SYM>: EXTERN` (or `EXTERN <SYM>`).

---

## Proposed Files to Create

### 1. `common.h` (Common Header & Data Structures)
- Defines constants (`MAX_LABEL_LEN = 30`, `MAX_LINE_LEN`, `MAX_TOKENS`, `MAX_WORDS`, `MAX_SYMBOLS`).
- Defines instruction table struct (`InstructionInfo`), symbol table entries (`Symbol`), definition table entries (`DefEntry`), use table entries (`UseEntry`), and error reporting types (`ERR_LEXICO`, `ERR_SINTATICO`, `ERR_SEMANTICO`).
- Utility functions for string normalization (uppercase conversion, trimming, filename extension replacement).

### 2. `montador.c` (Assembler)
Implements the complete assembler in clean stages while preserving original `.asm` line numbers for every statement:
1. **Line Reader & Lexical Analyzer**:
   - Strips `;` comments, normalizes whitespace, converts tokens to uppercase.
   - Joins standalone label lines (`LABEL:\n INSTR ...`) with the following non-empty statement while tracking the label's original declaration line.
   - Validates lexical rules on identifiers (max 30 chars, `[A-Za-z_][A-Za-z0-9_]*`) and numeric literals (valid signed decimal integers for `CONST`, positive decimal integers for `SPACE` and array offsets).
   - Normalizes array offset expressions so both `ADD X+2` and `ADD X + 2` (and `COPY X + 1 Y + 2`) are parsed into `(symbol, offset)` operand pairs.
   - Rejects commas in `COPY` (e.g. `COPY A, B`) as an error per `especs.tex` line 59.
2. **Preprocessing Stage (`EQU` and `IF`)**:
   - Validates that `EQU` declarations appear **only before any `SECTION`/`SECAO`**.
   - Substitutes `EQU` identifiers in operand positions.
   - Evaluates `IF <cond>` statements (validating that `IF` is inside `SECTION TEXT`) and skips the immediately following logical statement when `<cond> == 0`.
3. **Pass 1 (Section Layout & Symbol Table)**:
   - Separates statements into `TEXT` and `DATA` buffers so that `SECTION DATA` can appear either before or after `SECTION TEXT` in the input file, while `TEXT` addresses start at `0` and `DATA` addresses start at `text_size`.
   - Detects missing `SECTION TEXT`, duplicate labels, multiple labels on the same line, invalid section names, and directives placed in the wrong section.
   - Records `BEGIN`, `END`, `EXTERN`, and `PUBLIC` directives.
4. **Pass 2 (Code Generation & Semantic Validation)**:
   - Validates instruction and directive operand counts.
   - Resolves symbol addresses and array offsets (`addr(SYM) + offset`), checking for undeclared symbols.
   - Populates the Use Table (`U`), Definition Table (`D`), and Relocation Bitmap (`R`) if the program is a module (`BEGIN`/`END`).
   - Continues through the entire file collecting all errors (`LEXICO`, `SINTATICO`, `SEMANTICO`) with their original line numbers.
   - Writes the `.obj` file:
     - **Standalone mode** (no `BEGIN`/`END`): Single space-separated line of numbers matching `obj_sem_lig_sem_exec`.
     - **Module mode** (has `BEGIN`/`END`): Header lines (`D, <SYM> <ADDR>`, `U, <SYM> <ADDR>`, `R, <BITS...>`) followed by the space-separated code/data line matching `obj_com_lig`.

### 3. `ligador.c` (Linker)
1. **CLI & Subprocess Invocation**:
   - Accepts 1 to 3 `.asm` (or `.obj`) arguments: `./ligador prog1.asm [prog2.asm] [prog3.asm]`.
   - For each `.asm` argument, invokes `./montador <file>` via `system()` to generate the corresponding `.obj` file.
2. **Module Loading & Section Ordering**:
   - Parses each `.obj` file's `D`, `U`, `R`, and code/data words.
   - Ensures that in the final `.lig` executable, **all constants and spaces (`DATA`) come after the program (`TEXT`)** as required by `especs.tex` line 84 (*"E sempre no arquivo objeto as constantes e espaços criados por CONST ou SPACE vem depois do programa"*), computing the exact relocation offset for each module's `TEXT` and `DATA` symbols.
3. **Global Symbol Table (TSG) & Relocation**:
   - Resolves all `D` (Definitions) into the Global Symbol Table and detects undefined external symbols or multiply-defined public symbols.
   - Applies relocation using `R` and resolves external references using `U` (adding the global address of the external symbol to any existing array offset encoded in the word).
   - Outputs the final single-line space-separated executable to `<first_prog>.lig`.

### 4. `Makefile`
- Compiles both `montador` and `ligador` using `gcc -Wall -Wextra -std=c99 -O2`.
- Provides `all` and `clean` targets.

---

## Verification Plan
1. Compile `montador` and `ligador` with `gcc` and verify zero warnings/errors.
2. Run `./montador asm_sem_ligacao` and compare the output against `obj_sem_lig_sem_exec` (`12 29 10 29 4 28 ... 14 2 0 0 0`).
3. Test the snippet from `especs.tex` (lines 40-50) where `SECAO DATA` comes **before** `SECAO TEXT`.
4. Run `./montador asm_ligacao` and compare the output (`D`, `U`, `R`, and code line) against `obj_com_lig`.
5. Create a companion module `mod2.asm` and run `./ligador asm_ligacao mod2.asm` to verify the linked `.lig` output.
6. Test `EQU`, `IF`, array offsets (`X+2` and `X + 2`), multiline labels, and all 6 error categories (`LEXICO`, `SINTATICO`, `SEMANTICO`).

