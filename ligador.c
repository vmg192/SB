#include "common.h"

#define MAX_MODULES 3

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int addr;
} DefEntry;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int addr;
} UseEntry;

typedef struct {
    DefEntry defs[MAX_SYMBOLS];
    int num_defs;

    UseEntry uses[MAX_WORDS];
    int num_uses;

    int reloc[MAX_WORDS];
    int num_reloc;

    int code[MAX_WORDS];
    int num_words;

    int correction_factor;
    char begin_label[MAX_LABEL_LEN + 1];
} Module;

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    int global_addr;
} GlobalSymbol;

static Module g_modules[MAX_MODULES];
static GlobalSymbol g_tsg[MAX_SYMBOLS * MAX_MODULES];
static int g_num_tsg = 0;

static int g_num_modules = 0;

static int find_global_symbol(const char *name) {
    for (int i = 0; i < g_num_tsg; i++) {
        if (strcmp(g_tsg[i].name, name) == 0) {
            return g_tsg[i].global_addr;
        }
    }
    /* Fallback: check if any module declared this name as its BEGIN label */
    for (int m = 0; m < g_num_modules; m++) {
        if (g_modules[m].begin_label[0] != '\0' &&
            strcmp(g_modules[m].begin_label, name) == 0) {
            return g_modules[m].correction_factor;
        }
    }
    return -1;
}

static void extract_begin_label_from_asm(const char *asm_path, char *out_label, size_t out_size) {
    out_label[0] = '\0';
    FILE *fp = fopen(asm_path, "r");
    if (!fp) {
        return;
    }
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *semi = strchr(line, ';');
        if (semi) {
            *semi = '\0';
        }
        str_to_upper(line);
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0') {
            continue;
        }
        char *colon = strchr(trimmed, ':');
        if (colon != NULL) {
            *colon = '\0';
            char *left = trim_whitespace(trimmed);
            char *right = trim_whitespace(colon + 1);
            if (strcmp(left, "BEGIN") == 0 && *right != '\0') {
                char tok[MAX_TOKEN_LEN];
                if (sscanf(right, "%127s", tok) == 1 && is_valid_label_Syntax(tok)) {
                    copy_str(out_label, out_size, tok);
                    break;
                }
            } else if (strcmp(right, "BEGIN") == 0 && is_valid_label_Syntax(left)) {
                copy_str(out_label, out_size, left);
                break;
            }
        } else {
            char op[MAX_TOKEN_LEN], arg[MAX_TOKEN_LEN];
            if (sscanf(trimmed, "%127s %127s", op, arg) == 2 &&
                strcmp(op, "BEGIN") == 0 && is_valid_label_Syntax(arg)) {
                copy_str(out_label, out_size, arg);
                break;
            }
        }
    }
    fclose(fp);
}

static bool has_obj_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    return (strcmp(dot, ".obj") == 0 || strcmp(dot, ".OBJ") == 0);
}

static bool invoke_assembler(const char *prog_argv0, const char *asm_path) {
    char montador_bin[MAX_LINE_LEN] = "./montador";
    const char *slash = strrchr(prog_argv0, '/');
    if (slash != NULL) {
        size_t dir_len = (size_t)(slash - prog_argv0 + 1);
        if (dir_len + strlen("montador") < sizeof(montador_bin)) {
            memcpy(montador_bin, prog_argv0, dir_len);
            montador_bin[dir_len] = '\0';
            strcat(montador_bin, "montador");
        }
    }

    char cmd[MAX_LINE_LEN * 2];
    snprintf(cmd, sizeof(cmd), "%s \"%s\"", montador_bin, asm_path);
    int ret = system(cmd);
    return (ret == 0);
}

static bool load_module(const char *obj_path, Module *mod) {
    FILE *fp = fopen(obj_path, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir arquivo objeto: %s\n", obj_path);
        return false;
    }

    memset(mod, 0, sizeof(*mod));
    char line[MAX_WORDS * 4];

    while (fgets(line, sizeof(line), fp) != NULL) {
        str_to_upper(line);
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0') {
            continue;
        }

        if (strncmp(trimmed, "D,", 2) == 0 || strncmp(trimmed, "D ", 2) == 0) {
            char sym[MAX_TOKEN_LEN];
            int addr = 0;
            if (sscanf(trimmed + 2, "%127s %d", sym, &addr) == 2) {
                if (mod->num_defs < MAX_SYMBOLS) {
                    copy_str(mod->defs[mod->num_defs].name, sizeof(mod->defs[mod->num_defs].name), sym);
                    mod->defs[mod->num_defs].addr = addr;
                    mod->num_defs++;
                }
            }
        } else if (strncmp(trimmed, "U,", 2) == 0 || strncmp(trimmed, "U ", 2) == 0) {
            char sym[MAX_TOKEN_LEN];
            int addr = 0;
            if (sscanf(trimmed + 2, "%127s %d", sym, &addr) == 2) {
                if (mod->num_uses < MAX_WORDS) {
                    copy_str(mod->uses[mod->num_uses].name, sizeof(mod->uses[mod->num_uses].name), sym);
                    mod->uses[mod->num_uses].addr = addr;
                    mod->num_uses++;
                }
            }
        } else if (strncmp(trimmed, "R,", 2) == 0 || strncmp(trimmed, "R ", 2) == 0) {
            char *p = trimmed + 2;
            while (*p) {
                while (*p && isspace((unsigned char)*p)) {
                    p++;
                }
                if (!*p) {
                    break;
                }
                int bit = (int)strtol(p, &p, 10);
                if (mod->num_reloc < MAX_WORDS) {
                    mod->reloc[mod->num_reloc++] = (bit != 0) ? 1 : 0;
                }
            }
        } else {
            /* Object code words line */
            char *p = trimmed;
            while (*p) {
                while (*p && isspace((unsigned char)*p)) {
                    p++;
                }
                if (!*p) {
                    break;
                }
                char *endptr = NULL;
                int word = (int)strtol(p, &endptr, 10);
                if (endptr == p) {
                    break;
                }
                p = endptr;
                if (mod->num_words < MAX_WORDS) {
                    mod->code[mod->num_words++] = word;
                }
            }
        }
    }

    fclose(fp);
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 1 + MAX_MODULES) {
        fprintf(stderr, "Uso: %s <prog1.asm> [prog2.asm] [prog3.asm]\n", argv[0]);
        return 1;
    }

    int num_modules = argc - 1;
    g_num_modules = num_modules;

    for (int i = 0; i < num_modules; i++) {
        const char *input_arg = argv[i + 1];
        char obj_path[MAX_LINE_LEN];
        char begin_lbl[MAX_LABEL_LEN + 1] = "";

        if (has_obj_extension(input_arg)) {
            snprintf(obj_path, sizeof(obj_path), "%s", input_arg);
        } else {
            extract_begin_label_from_asm(input_arg, begin_lbl, sizeof(begin_lbl));
            if (!invoke_assembler(argv[0], input_arg)) {
                fprintf(stderr, "Erro na montagem de: %s\n", input_arg);
                return 1;
            }
            replace_extension(input_arg, ".obj", obj_path, sizeof(obj_path));
        }

        if (!load_module(obj_path, &g_modules[i])) {
            return 1;
        }
        copy_str(g_modules[i].begin_label, sizeof(g_modules[i].begin_label), begin_lbl);
    }

    /* Pass 1: Compute correction factors and build Global Symbol Table (TSG) */
    int current_offset = 0;
    for (int m = 0; m < num_modules; m++) {
        Module *mod = &g_modules[m];
        mod->correction_factor = current_offset;

        for (int d = 0; d < mod->num_defs; d++) {
            const char *sym_name = mod->defs[d].name;
            if (find_global_symbol(sym_name) != -1) {
                fprintf(stderr, "Erro SEMANTICO no ligador: simbolo PUBLIC repetido (%s)\n", sym_name);
                return 1;
            }
            copy_str(g_tsg[g_num_tsg].name, sizeof(g_tsg[g_num_tsg].name), sym_name);
            g_tsg[g_num_tsg].global_addr = mod->defs[d].addr + mod->correction_factor;
            g_num_tsg++;
        }

        current_offset += mod->num_words;
    }

    /* Pass 2: Resolve external references (U) and relocate internal addresses (R) */
    for (int m = 0; m < num_modules; m++) {
        Module *mod = &g_modules[m];
        bool is_ext_word[MAX_WORDS];
        memset(is_ext_word, 0, sizeof(is_ext_word));

        for (int u = 0; u < mod->num_uses; u++) {
            const char *sym_name = mod->uses[u].name;
            int addr = mod->uses[u].addr;
            int global_addr = find_global_symbol(sym_name);
            if (global_addr == -1) {
                fprintf(stderr, "Erro SEMANTICO no ligador: simbolo EXTERN nao definido (%s)\n", sym_name);
                return 1;
            }
            if (addr >= 0 && addr < mod->num_words) {
                mod->code[addr] += global_addr;
                is_ext_word[addr] = true;
            }
        }

        for (int w = 0; w < mod->num_words; w++) {
            if (!is_ext_word[w] && w < mod->num_reloc && mod->reloc[w] == 1) {
                mod->code[w] += mod->correction_factor;
            }
        }
    }

    /* Write output .lig file named after the first input program */
    char lig_path[MAX_LINE_LEN];
    replace_extension(argv[1], ".lig", lig_path, sizeof(lig_path));

    FILE *out = fopen(lig_path, "w");
    if (!out) {
        fprintf(stderr, "Erro ao criar arquivo executavel: %s\n", lig_path);
        return 1;
    }

    bool first_word = true;
    for (int m = 0; m < num_modules; m++) {
        Module *mod = &g_modules[m];
        for (int w = 0; w < mod->num_words; w++) {
            if (!first_word) {
                fprintf(out, " ");
            }
            fprintf(out, "%d", mod->code[w]);
            first_word = false;
        }
    }
    fprintf(out, " ");

    fclose(out);
    return 0;
}
