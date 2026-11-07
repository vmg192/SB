#include "common.h"

typedef struct {
    int line_no;
    ErrorType type;
    char message[256];
} AsmError;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int value;
    int line_no;
} EquEntry;

typedef enum {
    SEC_NONE = 0,
    SEC_TEXT = 1,
    SEC_DATA = 2
} SectionType;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int addr;           /* Relative address (adjusted after Pass 1) */
    SectionType section;
    bool is_extern;
    bool is_begin_label;
    int size;           /* Allocated words (for SPACE/CONST) */
    int line_no;
} Symbol;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int line_no;
} PublicEntry;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int addr;
} UseEntry;

typedef struct {
    int line_no;
    int label_line_no;
    char label[MAX_TOKEN_LEN];
    char op[MAX_TOKEN_LEN];
    char operands[MAX_TOKENS][MAX_TOKEN_LEN];
    int num_operands;
    SectionType section;
    bool skipped_by_if;
} Statement;

static AsmError g_errors[MAX_ERRORS];
static int g_num_errors = 0;

static EquEntry g_equ_table[MAX_SYMBOLS];
static int g_num_equs = 0;

static Symbol g_sym_table[MAX_SYMBOLS];
static int g_num_syms = 0;

static PublicEntry g_public_table[MAX_SYMBOLS];
static int g_num_publics = 0;

static UseEntry g_use_table[MAX_WORDS];
static int g_num_uses = 0;

static Statement g_stmts[MAX_STMTS];
static int g_num_stmts = 0;

static int g_code[MAX_WORDS];
static int g_reloc[MAX_WORDS];
static int g_num_words = 0;

static void add_error(int line_no, ErrorType type, const char *msg) {
    if (g_num_errors >= MAX_ERRORS) {
        return;
    }
    /* Avoid duplicate identical error type & message on the same line */
    for (int i = 0; i < g_num_errors; i++) {
        if (g_errors[i].line_no == line_no &&
            g_errors[i].type == type &&
            strcmp(g_errors[i].message, msg) == 0) {
            return;
        }
    }
    g_errors[g_num_errors].line_no = line_no;
    g_errors[g_num_errors].type = type;
    snprintf(g_errors[g_num_errors].message, sizeof(g_errors[g_num_errors].message), "%s", msg);
    g_num_errors++;
}

static const char *error_type_str(ErrorType type) {
    switch (type) {
        case ERR_LEXICO:    return "LEXICO";
        case ERR_SINTATICO: return "SINTATICO";
        case ERR_SEMANTICO: return "SEMANTICO";
    }
    return "DESCONHECIDO";
}

static int cmp_errors(const void *a, const void *b) {
    const AsmError *ea = (const AsmError *)a;
    const AsmError *eb = (const AsmError *)b;
    if (ea->line_no != eb->line_no) {
        return ea->line_no - eb->line_no;
    }
    return (int)ea->type - (int)eb->type;
}

static void print_errors(void) {
    qsort(g_errors, (size_t)g_num_errors, sizeof(AsmError), cmp_errors);
    for (int i = 0; i < g_num_errors; i++) {
        if (g_errors[i].line_no > 0) {
            printf("Erro %s na linha %d: %s\n",
                   error_type_str(g_errors[i].type),
                   g_errors[i].line_no,
                   g_errors[i].message);
        } else {
            printf("Erro %s: %s\n",
                   error_type_str(g_errors[i].type),
                   g_errors[i].message);
        }
    }
}

static const EquEntry *find_equ(const char *name) {
    for (int i = 0; i < g_num_equs; i++) {
        if (strcmp(g_equ_table[i].name, name) == 0) {
            return &g_equ_table[i];
        }
    }
    return NULL;
}

static Symbol *find_symbol(const char *name) {
    for (int i = 0; i < g_num_syms; i++) {
        if (strcmp(g_sym_table[i].name, name) == 0) {
            return &g_sym_table[i];
        }
    }
    return NULL;
}

static bool is_directive_name(const char *s) {
    static const char *directives[] = {
        "SECTION", "SECAO", "SPACE", "CONST", "EQU", "IF",
        "BEGIN", "END", "PUBLIC", "EXTERN"
    };
    for (size_t i = 0; i < sizeof(directives) / sizeof(directives[0]); i++) {
        if (strcmp(s, directives[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_reserved_word(const char *s) {
    return find_instruction(s) != NULL || is_directive_name(s);
}

static void validate_label_token(const char *label, int line_no) {
    if (!label || label[0] == '\0') {
        add_error(line_no, ERR_SINTATICO, "Rotulo vazio antes de ':'");
        return;
    }
    size_t len = strlen(label);
    if (len > MAX_LABEL_LEN) {
        add_error(line_no, ERR_LEXICO, "Rotulo excede o limite de 30 caracteres");
        return;
    }
    if (isdigit((unsigned char)label[0])) {
        add_error(line_no, ERR_LEXICO, "Rotulo nao pode comecar com numero");
        return;
    }
    if (!is_valid_label_Syntax(label)) {
        add_error(line_no, ERR_LEXICO, "Rotulo contem caracteres invalidos");
        return;
    }
    if (is_reserved_word(label)) {
        add_error(line_no, ERR_SINTATICO, "Palavra reservada nao pode ser usada como rotulo");
    }
}

/*
 * Splits operand string into tokens while merging array offset expressions
 * like "X + 2", "X+ 2", "X +2", or "X+2" into a single operand token "X+2".
 */
static int tokenize_operands(char *rest, int line_no, char out_ops[MAX_TOKENS][MAX_TOKEN_LEN]) {
    if (!rest || *rest == '\0') {
        return 0;
    }

    /* Check for invalid comma separator (e.g., COPY A, B) */
    if (strchr(rest, ',') != NULL) {
        add_error(line_no, ERR_SINTATICO, "Separador ',' invalido (use espaco entre operandos)");
        /* Replace commas with spaces so we can continue analyzing the line */
        for (char *p = rest; *p; ++p) {
            if (*p == ',') {
                *p = ' ';
            }
        }
    }

    char raw[MAX_TOKENS][MAX_TOKEN_LEN];
    int num_raw = 0;

    char *p = rest;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (num_raw >= MAX_TOKENS) {
            break;
        }
        int k = 0;
        while (*p && !isspace((unsigned char)*p)) {
            if (k < MAX_TOKEN_LEN - 1) {
                raw[num_raw][k++] = *p;
            }
            p++;
        }
        raw[num_raw][k] = '\0';
        num_raw++;
    }

    int count = 0;
    int i = 0;
    while (i < num_raw) {
        if (strcmp(raw[i], "+") == 0) {
            /* Standalone "+" joins previous operand with next token */
            if (count > 0 && i + 1 < num_raw && strchr(out_ops[count - 1], '+') == NULL) {
                strncat(out_ops[count - 1], "+", MAX_TOKEN_LEN - strlen(out_ops[count - 1]) - 1);
                strncat(out_ops[count - 1], raw[i + 1], MAX_TOKEN_LEN - strlen(out_ops[count - 1]) - 1);
                i += 2;
                continue;
            } else if (count > 0 && i + 1 >= num_raw) {
                strncat(out_ops[count - 1], "+", MAX_TOKEN_LEN - strlen(out_ops[count - 1]) - 1);
                i++;
                continue;
            }
        } else if (raw[i][0] == '+' && count > 0 && strchr(out_ops[count - 1], '+') == NULL) {
            /* e.g. "ADD X +2" or "COPY X +1 Y +2" */
            strncat(out_ops[count - 1], raw[i], MAX_TOKEN_LEN - strlen(out_ops[count - 1]) - 1);
            i++;
            continue;
        }

        size_t len = strlen(raw[i]);
        if (len > 0 && raw[i][len - 1] == '+' && i + 1 < num_raw) {
            /* e.g. "ADD X+ 2" */
            copy_str(out_ops[count], MAX_TOKEN_LEN, raw[i]);
            strncat(out_ops[count], raw[i + 1], MAX_TOKEN_LEN - strlen(out_ops[count]) - 1);
            count++;
            i += 2;
            continue;
        }

        copy_str(out_ops[count], MAX_TOKEN_LEN, raw[i]);
        count++;
        i++;
    }

    return count;
}

static bool read_and_parse_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir arquivo de entrada: %s\n", filename);
        return false;
    }

    char line_buf[MAX_LINE_LEN];
    int line_no = 0;

    char pending_label[MAX_TOKEN_LEN] = "";
    int pending_label_line = 0;

    while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
        line_no++;

        /* Strip comment starting with ';' */
        char *semi = strchr(line_buf, ';');
        if (semi) {
            *semi = '\0';
        }

        str_to_upper(line_buf);
        char *trimmed = trim_whitespace(line_buf);
        if (*trimmed == '\0') {
            continue;
        }

        char current_label[MAX_TOKEN_LEN] = "";
        int current_label_line = line_no;
        char *rest = trimmed;

        char *colon = strchr(rest, ':');
        if (colon != NULL) {
            /* Check if there are multiple colons on the same line */
            char *second_colon = strchr(colon + 1, ':');
            if (second_colon != NULL) {
                add_error(line_no, ERR_SINTATICO, "Mais de um rotulo na mesma linha");
                /* Recover by taking the first label and advancing past the last colon */
                *colon = '\0';
                char *lbl = trim_whitespace(rest);
                snprintf(current_label, sizeof(current_label), "%s", lbl);
                while (second_colon != NULL) {
                    colon = second_colon;
                    second_colon = strchr(colon + 1, ':');
                }
                rest = trim_whitespace(colon + 1);
            } else {
                *colon = '\0';
                char *lbl = trim_whitespace(rest);
                snprintf(current_label, sizeof(current_label), "%s", lbl);
                rest = trim_whitespace(colon + 1);
            }

            /* Special case as in asm_ligacao: "BEGIN: MOD1", "EXTERN: SYM", "PUBLIC: SYM" */
            if ((strcmp(current_label, "BEGIN") == 0 ||
                 strcmp(current_label, "EXTERN") == 0 ||
                 strcmp(current_label, "PUBLIC") == 0) && *rest != '\0') {
                Statement *st = &g_stmts[g_num_stmts++];
                memset(st, 0, sizeof(*st));
                st->line_no = line_no;
                st->label_line_no = line_no;
                copy_str(st->op, sizeof(st->op), current_label);
                st->num_operands = tokenize_operands(rest, line_no, st->operands);
                if (strcmp(current_label, "BEGIN") == 0) {
                    if (st->num_operands == 1) {
                        copy_str(st->label, sizeof(st->label), st->operands[0]);
                        st->num_operands = 0;
                        validate_label_token(st->label, line_no);
                    } else {
                        add_error(line_no, ERR_SINTATICO, "Quantidade de operandos invalida para BEGIN");
                    }
                }
                continue;
            }

            validate_label_token(current_label, line_no);

            if (pending_label[0] != '\0') {
                add_error(line_no, ERR_SINTATICO, "Dois rotulos consecutivos para a mesma instrucao");
            }
            snprintf(pending_label, sizeof(pending_label), "%s", current_label);
            pending_label_line = current_label_line;

            if (*rest == '\0') {
                /* Standalone label on its own line: wait for next non-empty line */
                continue;
            }
        }

        if (g_num_stmts >= MAX_STMTS) {
            break;
        }

        Statement *st = &g_stmts[g_num_stmts++];
        memset(st, 0, sizeof(*st));
        st->line_no = line_no;

        if (pending_label[0] != '\0') {
            snprintf(st->label, sizeof(st->label), "%s", pending_label);
            st->label_line_no = pending_label_line;
            pending_label[0] = '\0';
            pending_label_line = 0;
        }

        /* Extract operation token */
        char *p = rest;
        int k = 0;
        while (*p && !isspace((unsigned char)*p)) {
            if (k < MAX_TOKEN_LEN - 1) {
                st->op[k++] = *p;
            }
            p++;
        }
        st->op[k] = '\0';
        char *ops_str = trim_whitespace(p);

        st->num_operands = tokenize_operands(ops_str, line_no, st->operands);
    }

    if (pending_label[0] != '\0') {
        add_error(pending_label_line, ERR_SINTATICO, "Rotulo no final do arquivo sem instrucao ou diretiva associada");
    }

    fclose(fp);
    return true;
}

static void substitute_equ_in_operand(char *op_str) {
    char *plus = strchr(op_str, '+');
    if (plus == NULL) {
        const EquEntry *eq = find_equ(op_str);
        if (eq != NULL) {
            snprintf(op_str, MAX_TOKEN_LEN, "%d", eq->value);
        }
    } else {
        char left[MAX_TOKEN_LEN];
        char right[MAX_TOKEN_LEN];
        size_t left_len = (size_t)(plus - op_str);
        if (left_len >= sizeof(left)) {
            left_len = sizeof(left) - 1;
        }
        memcpy(left, op_str, left_len);
        left[left_len] = '\0';
        snprintf(right, sizeof(right), "%s", plus + 1);

        const EquEntry *eq_left = find_equ(left);
        if (eq_left != NULL) {
            snprintf(left, sizeof(left), "%d", eq_left->value);
        }
        const EquEntry *eq_right = find_equ(right);
        if (eq_right != NULL) {
            snprintf(right, sizeof(right), "%d", eq_right->value);
        }
        copy_str(op_str, MAX_TOKEN_LEN, left);
        append_str(op_str, MAX_TOKEN_LEN, "+");
        append_str(op_str, MAX_TOKEN_LEN, right);
    }
}

static void run_preprocessor(void) {
    SectionType current_sec = SEC_NONE;
    bool skip_next = false;

    for (int i = 0; i < g_num_stmts; i++) {
        Statement *st = &g_stmts[i];

        if (skip_next) {
            st->skipped_by_if = true;
            skip_next = false;
            continue;
        }

        if (strcmp(st->op, "SECTION") == 0 || strcmp(st->op, "SECAO") == 0) {
            if (st->label[0] != '\0') {
                add_error(st->label_line_no, ERR_SINTATICO, "Diretiva SECTION nao pode ter rotulo");
            }
            if (st->num_operands != 1) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva SECTION requer exatamente 1 operando (TEXT ou DATA)");
            } else if (strcmp(st->operands[0], "TEXT") == 0) {
                current_sec = SEC_TEXT;
            } else if (strcmp(st->operands[0], "DATA") == 0) {
                current_sec = SEC_DATA;
            } else {
                add_error(st->line_no, ERR_SINTATICO, "Nome de secao invalido (esperado TEXT ou DATA)");
            }
            st->section = current_sec;
            continue;
        }

        st->section = current_sec;

        if (strcmp(st->op, "EQU") == 0) {
            if (current_sec != SEC_NONE) {
                add_error(st->line_no, ERR_SEMANTICO, "Diretiva EQU deve ser declarada antes de qualquer secao");
            }
            if (st->label[0] == '\0') {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva EQU requer um rotulo");
            }
            if (st->num_operands != 1) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva EQU requer exatamente 1 operando");
            } else {
                substitute_equ_in_operand(st->operands[0]);
                int val = 0;
                if (!parse_decimal_int(st->operands[0], &val)) {
                    add_error(st->line_no, ERR_LEXICO, "Argumento invalido para EQU (esperado inteiro decimal)");
                } else if (st->label[0] != '\0') {
                    if (find_equ(st->label) != NULL) {
                        add_error(st->label_line_no, ERR_SEMANTICO, "Declaracao ou rotulo repetido em EQU");
                    } else if (g_num_equs < MAX_SYMBOLS) {
                        snprintf(g_equ_table[g_num_equs].name, sizeof(g_equ_table[g_num_equs].name), "%s", st->label);
                        g_equ_table[g_num_equs].value = val;
                        g_equ_table[g_num_equs].line_no = st->label_line_no;
                        g_num_equs++;
                    }
                }
            }
            continue;
        }

        /* Substitute existing EQU definitions in operands (except symbol-declaring directives) */
        if (strcmp(st->op, "BEGIN") != 0 &&
            strcmp(st->op, "PUBLIC") != 0 &&
            strcmp(st->op, "EXTERN") != 0) {
            for (int j = 0; j < st->num_operands; j++) {
                substitute_equ_in_operand(st->operands[j]);
            }
        }

        if (strcmp(st->op, "IF") == 0) {
            if (current_sec != SEC_TEXT) {
                add_error(st->line_no, ERR_SEMANTICO, "Diretiva IF somente e permitida dentro da secao de texto");
            }
            if (st->num_operands != 1) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva IF requer exatamente 1 operando");
            } else {
                int cond_val = 0;
                if (!parse_decimal_int(st->operands[0], &cond_val)) {
                    if (is_valid_label_Syntax(st->operands[0])) {
                        add_error(st->line_no, ERR_SEMANTICO, "Rotulo ou declaracao ausente no operando de IF");
                    } else {
                        add_error(st->line_no, ERR_LEXICO, "Operando invalido na diretiva IF");
                    }
                } else {
                    if (cond_val == 0) {
                        skip_next = true;
                    }
                }
            }
            continue;
        }
    }
}

static void add_symbol_ex(const char *name, int addr, SectionType sec, bool is_extern,
                          bool is_begin_label, int size, int line_no) {
    if (!name || name[0] == '\0') {
        return;
    }
    if (!is_valid_label_Syntax(name) || is_reserved_word(name)) {
        return;
    }
    Symbol *existing = find_symbol(name);
    if (existing != NULL) {
        if (existing->is_begin_label && !is_begin_label && !is_extern && sec == SEC_TEXT) {
            existing->addr = addr;
            existing->section = sec;
            existing->size = size;
            existing->line_no = line_no;
            existing->is_begin_label = false;
            return;
        }
        add_error(line_no, ERR_SEMANTICO, "Declaracao ou rotulo repetido");
        return;
    }
    if (find_equ(name) != NULL) {
        add_error(line_no, ERR_SEMANTICO, "Declaracao ou rotulo repetido");
        return;
    }
    if (g_num_syms < MAX_SYMBOLS) {
        Symbol *sym = &g_sym_table[g_num_syms++];
        snprintf(sym->name, sizeof(sym->name), "%s", name);
        sym->addr = addr;
        sym->section = sec;
        sym->is_extern = is_extern;
        sym->is_begin_label = is_begin_label;
        sym->size = size;
        sym->line_no = line_no;
    }
}

static void add_symbol(const char *name, int addr, SectionType sec, bool is_extern, int size, int line_no) {
    add_symbol_ex(name, addr, sec, is_extern, false, size, line_no);
}

static void run_pass1(bool *out_is_module) {
    bool has_text_section = false;
    bool has_begin = false;
    bool has_end = false;
    bool has_extern = false;
    int text_lc = 0;
    int data_lc = 0;

    for (int i = 0; i < g_num_stmts; i++) {
        Statement *st = &g_stmts[i];
        if (st->skipped_by_if) {
            continue;
        }

        if (strcmp(st->op, "SECTION") == 0 || strcmp(st->op, "SECAO") == 0) {
            if (st->section == SEC_TEXT) {
                has_text_section = true;
            }
            continue;
        }

        if (strcmp(st->op, "EQU") == 0) {
            continue;
        }

        if (strcmp(st->op, "IF") == 0) {
            if (st->label[0] != '\0') {
                add_symbol(st->label, text_lc, SEC_TEXT, false, 0, st->label_line_no);
            }
            continue;
        }

        if (strcmp(st->op, "BEGIN") == 0) {
            has_begin = true;
            if (st->section != SEC_TEXT) {
                add_error(st->line_no, ERR_SEMANTICO, "Diretiva BEGIN deve estar na secao TEXT");
            }
            if (st->num_operands == 1 && st->label[0] == '\0') {
                copy_str(st->label, sizeof(st->label), st->operands[0]);
                st->label_line_no = st->line_no;
                st->num_operands = 0;
                validate_label_token(st->label, st->line_no);
            } else if (st->num_operands > 0) {
                add_error(st->line_no, ERR_SINTATICO, "Quantidade de operandos invalida para BEGIN");
            }
            if (st->label[0] != '\0') {
                add_symbol_ex(st->label, text_lc, SEC_TEXT, false, true, 0, st->label_line_no);
            }
            continue;
        }

        if (strcmp(st->op, "END") == 0) {
            has_end = true;
            if (st->label[0] != '\0') {
                add_error(st->label_line_no, ERR_SINTATICO, "Diretiva END nao pode possuir rotulo");
            }
            if (st->num_operands != 0) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva END nao aceita operandos");
            }
            continue;
        }

        if (strcmp(st->op, "EXTERN") == 0) {
            has_extern = true;
            char ext_name[MAX_TOKEN_LEN] = "";
            int ext_line = st->line_no;
            if (st->label[0] != '\0' && st->num_operands == 0) {
                snprintf(ext_name, sizeof(ext_name), "%s", st->label);
                ext_line = st->label_line_no;
            } else if (st->label[0] == '\0' && st->num_operands == 1) {
                snprintf(ext_name, sizeof(ext_name), "%s", st->operands[0]);
                validate_label_token(ext_name, st->line_no);
            } else {
                add_error(st->line_no, ERR_SINTATICO, "Formato invalido para diretiva EXTERN");
            }
            if (ext_name[0] != '\0') {
                add_symbol(ext_name, 0, SEC_NONE, true, 0, ext_line);
            }
            continue;
        }

        if (strcmp(st->op, "PUBLIC") == 0) {
            char pub_name[MAX_TOKEN_LEN] = "";
            if (st->label[0] == '\0' && st->num_operands == 1) {
                snprintf(pub_name, sizeof(pub_name), "%s", st->operands[0]);
                validate_label_token(pub_name, st->line_no);
            } else if (st->label[0] != '\0' && st->num_operands == 0) {
                snprintf(pub_name, sizeof(pub_name), "%s", st->label);
            } else {
                add_error(st->line_no, ERR_SINTATICO, "Quantidade de operandos invalida para PUBLIC");
            }
            if (pub_name[0] != '\0' && g_num_publics < MAX_SYMBOLS) {
                snprintf(g_public_table[g_num_publics].name, sizeof(g_public_table[g_num_publics].name), "%s", pub_name);
                g_public_table[g_num_publics].line_no = st->line_no;
                g_num_publics++;
            }
            continue;
        }

        if (st->section == SEC_NONE) {
            add_error(st->line_no, ERR_SEMANTICO, "Instrucao ou diretiva fora de qualquer secao");
            continue;
        }

        /* Handle regular instructions and DATA directives */
        if (strcmp(st->op, "SPACE") == 0) {
            if (st->section != SEC_DATA) {
                add_error(st->line_no, ERR_SEMANTICO, "Diretiva SPACE deve estar na secao DATA");
            }
            int count = 1;
            if (st->num_operands == 1) {
                if (!parse_decimal_int(st->operands[0], &count)) {
                    add_error(st->line_no, ERR_LEXICO, "Argumento invalido para SPACE (esperado inteiro positivo)");
                    count = 1;
                } else if (count <= 0) {
                    add_error(st->line_no, ERR_SINTATICO, "Argumento de SPACE deve ser maior que zero");
                    count = 1;
                }
            } else if (st->num_operands > 1) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva SPACE aceita no maximo 1 operando");
            }
            if (st->label[0] != '\0') {
                add_symbol(st->label, data_lc, SEC_DATA, false, count, st->label_line_no);
            }
            data_lc += count;
            continue;
        }

        if (strcmp(st->op, "CONST") == 0) {
            if (st->section != SEC_DATA) {
                add_error(st->line_no, ERR_SEMANTICO, "Diretiva CONST deve estar na secao DATA");
            }
            if (st->num_operands != 1) {
                add_error(st->line_no, ERR_SINTATICO, "Diretiva CONST requer exatamente 1 operando");
            } else {
                int dummy = 0;
                if (!parse_decimal_int(st->operands[0], &dummy)) {
                    add_error(st->line_no, ERR_LEXICO, "Valor invalido para CONST (esperado inteiro decimal)");
                }
            }
            if (st->label[0] != '\0') {
                add_symbol(st->label, data_lc, SEC_DATA, false, 1, st->label_line_no);
            }
            data_lc += 1;
            continue;
        }

        const InstructionInfo *info = find_instruction(st->op);
        if (info != NULL) {
            if (st->section != SEC_TEXT) {
                add_error(st->line_no, ERR_SEMANTICO, "Instrucao nao permitida fora da secao TEXT");
            }
            if (st->label[0] != '\0') {
                add_symbol(st->label, text_lc, SEC_TEXT, false, info->size, st->label_line_no);
            }
            text_lc += info->size;
        } else {
            /* Invalid instruction or directive */
            if (st->label[0] != '\0') {
                add_symbol(st->label, (st->section == SEC_DATA) ? data_lc : text_lc,
                           st->section, false, 1, st->label_line_no);
            }
            add_error(st->line_no, ERR_SINTATICO, "Instrucao ou diretiva invalida");
        }
    }

    if (!has_text_section) {
        add_error(1, ERR_SEMANTICO, "Secao TEXT ausente no programa");
    }

    if (has_begin != has_end) {
        add_error(1, ERR_SEMANTICO, "Diretivas BEGIN e END desbalanceadas no modulo");
    }

    /* Adjust DATA symbols so DATA always comes immediately after TEXT */
    for (int i = 0; i < g_num_syms; i++) {
        if (!g_sym_table[i].is_extern && g_sym_table[i].section == SEC_DATA) {
            g_sym_table[i].addr += text_lc;
        }
    }

    *out_is_module = (has_begin || has_end || g_num_publics > 0 || has_extern);
}

static int resolve_operand(const char *op_str, int line_no, int word_addr) {
    char sym_name[MAX_TOKEN_LEN];
    int offset = 0;

    const char *plus = strchr(op_str, '+');
    if (plus == NULL) {
        snprintf(sym_name, sizeof(sym_name), "%s", op_str);
    } else {
        size_t len = (size_t)(plus - op_str);
        if (len >= sizeof(sym_name)) {
            len = sizeof(sym_name) - 1;
        }
        memcpy(sym_name, op_str, len);
        sym_name[len] = '\0';
        const char *off_str = plus + 1;
        if (*off_str == '\0') {
            add_error(line_no, ERR_SINTATICO, "Expressao de indice de vetor incompleta apos '+'");
        } else if (!parse_decimal_int(off_str, &offset) || offset < 0) {
            add_error(line_no, ERR_LEXICO, "Constante de deslocamento invalida no operando");
            offset = 0;
        }
    }

    if (sym_name[0] == '\0') {
        add_error(line_no, ERR_SINTATICO, "Operando invalido");
        return 0;
    }

    if (strlen(sym_name) > MAX_LABEL_LEN || isdigit((unsigned char)sym_name[0]) || !is_valid_label_Syntax(sym_name)) {
        add_error(line_no, ERR_LEXICO, "Operando contem identificador lexico invalido");
        return 0;
    }

    Symbol *sym = find_symbol(sym_name);
    if (sym == NULL) {
        add_error(line_no, ERR_SEMANTICO, "Declaracao ou rotulo ausente");
        return 0;
    }

    if (sym->is_extern) {
        if (g_num_uses < MAX_WORDS) {
            snprintf(g_use_table[g_num_uses].name, sizeof(g_use_table[g_num_uses].name), "%s", sym->name);
            g_use_table[g_num_uses].addr = word_addr;
            g_num_uses++;
        }
        return offset;
    }

    return sym->addr + offset;
}

static void run_pass2(void) {
    /* Verify PUBLIC symbols exist in Symbol Table */
    for (int i = 0; i < g_num_publics; i++) {
        Symbol *sym = find_symbol(g_public_table[i].name);
        if (sym == NULL || sym->is_extern) {
            add_error(g_public_table[i].line_no, ERR_SEMANTICO, "Rotulo declarado em PUBLIC ausente no modulo");
        }
    }

    /* Emit SECTION TEXT instructions first */
    for (int i = 0; i < g_num_stmts; i++) {
        Statement *st = &g_stmts[i];
        if (st->skipped_by_if || st->section != SEC_TEXT) {
            continue;
        }
        if (strcmp(st->op, "SECTION") == 0 || strcmp(st->op, "SECAO") == 0 ||
            strcmp(st->op, "EQU") == 0 || strcmp(st->op, "IF") == 0 ||
            strcmp(st->op, "BEGIN") == 0 || strcmp(st->op, "END") == 0 ||
            strcmp(st->op, "PUBLIC") == 0 || strcmp(st->op, "EXTERN") == 0 ||
            strcmp(st->op, "SPACE") == 0 || strcmp(st->op, "CONST") == 0) {
            continue;
        }

        const InstructionInfo *info = find_instruction(st->op);
        if (info == NULL) {
            continue;
        }

        if (st->num_operands != info->num_operands) {
            add_error(st->line_no, ERR_SINTATICO, "Instrucao com quantidade de operandos errada");
        }

        /* Emit opcode */
        if (g_num_words < MAX_WORDS) {
            g_code[g_num_words] = info->opcode;
            g_reloc[g_num_words] = 0;
            g_num_words++;
        }

        /* Emit operands */
        for (int op_idx = 0; op_idx < info->num_operands; op_idx++) {
            int val = 0;
            if (op_idx < st->num_operands) {
                val = resolve_operand(st->operands[op_idx], st->line_no, g_num_words);
            }
            if (g_num_words < MAX_WORDS) {
                g_code[g_num_words] = val;
                g_reloc[g_num_words] = 1;
                g_num_words++;
            }
        }
    }

    /* Emit SECTION DATA words after SECTION TEXT */
    for (int i = 0; i < g_num_stmts; i++) {
        Statement *st = &g_stmts[i];
        if (st->skipped_by_if || st->section != SEC_DATA) {
            continue;
        }
        if (strcmp(st->op, "SPACE") == 0) {
            int count = 1;
            if (st->num_operands == 1) {
                if (!parse_decimal_int(st->operands[0], &count) || count <= 0) {
                    count = 1;
                }
            }
            for (int k = 0; k < count; k++) {
                if (g_num_words < MAX_WORDS) {
                    g_code[g_num_words] = 0;
                    g_reloc[g_num_words] = 0;
                    g_num_words++;
                }
            }
        } else if (strcmp(st->op, "CONST") == 0) {
            int val = 0;
            if (st->num_operands == 1) {
                parse_decimal_int(st->operands[0], &val);
            }
            if (g_num_words < MAX_WORDS) {
                g_code[g_num_words] = val;
                g_reloc[g_num_words] = 0;
                g_num_words++;
            }
        }
    }
}

static bool write_object_file(const char *obj_filename, bool is_module) {
    FILE *fp = fopen(obj_filename, "w");
    if (!fp) {
        fprintf(stderr, "Erro ao criar arquivo objeto: %s\n", obj_filename);
        return false;
    }

    if (is_module) {
        /* Definition Table (D) */
        for (int i = 0; i < g_num_publics; i++) {
            Symbol *sym = find_symbol(g_public_table[i].name);
            int addr = (sym != NULL) ? sym->addr : 0;
            fprintf(fp, "D, %s %d\n", g_public_table[i].name, addr);
        }

        /* Use Table (U) */
        for (int i = 0; i < g_num_uses; i++) {
            fprintf(fp, "U, %s %d\n", g_use_table[i].name, g_use_table[i].addr);
        }

        /* Relocation Bitmap (R) */
        fprintf(fp, "R,");
        for (int i = 0; i < g_num_words; i++) {
            fprintf(fp, " %d", g_reloc[i]);
        }
        fprintf(fp, "\n");
    }

    /* Single line of object words separated by spaces */
    for (int i = 0; i < g_num_words; i++) {
        if (i > 0) {
            fprintf(fp, " ");
        }
        fprintf(fp, "%d", g_code[i]);
    }
    fprintf(fp, " ");

    fclose(fp);
    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <arquivo.asm>\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    if (!read_and_parse_file(input_path)) {
        return 1;
    }

    run_preprocessor();

    bool is_module = false;
    run_pass1(&is_module);
    run_pass2();

    if (g_num_errors > 0) {
        print_errors();
        return 1;
    }

    char obj_path[MAX_LINE_LEN];
    replace_extension(input_path, ".obj", obj_path, sizeof(obj_path));

    if (!write_object_file(obj_path, is_module)) {
        return 1;
    }

    return 0;
}
