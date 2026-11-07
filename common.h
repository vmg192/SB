#ifndef COMMON_H
#define COMMON_H

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LABEL_LEN 30
#define MAX_TOKEN_LEN 128
#define MAX_LINE_LEN 1024
#define MAX_TOKENS 64
#define MAX_SYMBOLS 1024
#define MAX_WORDS 65536
#define MAX_STMTS 4096
#define MAX_ERRORS 512

typedef enum {
    ERR_LEXICO,
    ERR_SINTATICO,
    ERR_SEMANTICO
} ErrorType;

typedef struct {
    const char *mnemonic;
    int opcode;
    int num_operands;
    int size;
} InstructionInfo;

static const InstructionInfo INSTRUCTION_TABLE[] = {
    {"ADD",    1,  1, 2},
    {"SUB",    2,  1, 2},
    {"MULT",   3,  1, 2},
    {"DIV",    4,  1, 2},
    {"JMP",    5,  1, 2},
    {"JMPN",   6,  1, 2},
    {"JMPP",   7,  1, 2},
    {"JMPZ",   8,  1, 2},
    {"COPY",   9,  2, 3},
    {"LOAD",   10, 1, 2},
    {"STORE",  11, 1, 2},
    {"INPUT",  12, 1, 2},
    {"OUTPUT", 13, 1, 2},
    {"STOP",   14, 0, 1}
};

#define NUM_INSTRUCTIONS (sizeof(INSTRUCTION_TABLE) / sizeof(INSTRUCTION_TABLE[0]))

static inline const InstructionInfo *find_instruction(const char *name) {
    for (size_t i = 0; i < NUM_INSTRUCTIONS; i++) {
        if (strcmp(INSTRUCTION_TABLE[i].mnemonic, name) == 0) {
            return &INSTRUCTION_TABLE[i];
        }
    }
    return NULL;
}

static inline void str_to_upper(char *s) {
    for (; *s; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static inline void copy_str(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memmove(dst, src, len);
    dst[len] = '\0';
}

static inline void append_str(char *dst, size_t dst_size, const char *src) {
    size_t cur = strlen(dst);
    if (cur + 1 >= dst_size) {
        return;
    }
    copy_str(dst + cur, dst_size - cur, src);
}

static inline char *trim_whitespace(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static inline bool is_valid_label_Syntax(const char *s) {
    if (!s || s[0] == '\0') {
        return false;
    }
    size_t len = strlen(s);
    if (len > MAX_LABEL_LEN) {
        return false;
    }
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return false;
    }
    for (size_t i = 1; i < len; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) {
            return false;
        }
    }
    return true;
}

static inline bool parse_decimal_int(const char *s, int *out_val) {
    if (!s || s[0] == '\0') {
        return false;
    }
    int i = 0;
    if (s[0] == '+' || s[0] == '-') {
        i++;
        if (s[i] == '\0') {
            return false;
        }
    }
    for (; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    *out_val = (int)strtol(s, NULL, 10);
    return true;
}

static inline void replace_extension(const char *input_path, const char *new_ext,
                                     char *out_path, size_t out_size) {
    snprintf(out_path, out_size, "%s", input_path);
    char *last_slash = strrchr(out_path, '/');
    char *last_dot = strrchr(out_path, '.');
    if (last_dot != NULL && (last_slash == NULL || last_dot > last_slash)) {
        *last_dot = '\0';
    }
    strncat(out_path, new_ext, out_size - strlen(out_path) - 1);
}

#endif /* COMMON_H */
