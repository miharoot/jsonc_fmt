/*
 * jsonc_fmt.c — переносимый однофайловый JSON-форматтер/валидатор на чистом C (C99).
 *
 * Возможности:
 *   - Форматирует (pretty-print) JSON с отступами.
 *   - Понимает комментарии "//" и блочные "/ *" ... "* /" (нестандартное расширение JSON, часто
 *     называемое JSONC) и не выбрасывает их при форматировании.
 *       * Комментарий, который в исходном файле был ЕДИНСТВЕННЫМ содержимым строки
 *         (перед ним и после него на строке нет ничего, кроме пробелов),
 *         считается "одиноким" и при выводе прижимается к началу строки
 *         (колонка 0), на отдельной строке.
 *       * Комментарий, стоящий на строке рядом с кодом (например, после
 *         значения перед запятой), считается "не одиноким" и остаётся
 *         прикреплённым к этой же строке кода, как и было.
 *   - Валидирует JSON и при синтаксической ошибке печатает в stderr номер
 *     строки и столбца, где она обнаружена, и завершает работу с кодом 1
 *     БЕЗ вывода в stdout.
 *   - По умолчанию результат печатается в stdout.
 *
 * Сборка:
 *   cc -std=c99 -O2 -Wall -Wextra -o jsonc_fmt jsonc_fmt.c
 *   cc -std=c99 -O2 -Wall -Wextra -static -o jsonc_fmt jsonc_fmt.c
 *
 * Использование:
 *   jsonc_fmt [опции] [файл]
 *   Если файл не указан или указан "-", читается stdin.
 *
 * Опции:
 *   -i, --indent N        Ширина отступа пробелами (по умолчанию 2).
 *   -t, --tabs             Использовать табуляцию для отступов вместо пробелов.
 *   -a, --align-comments    Не прижимать "одинокие" комментарии к колонке 0,
 *                           а выравнивать их по текущему уровню вложенности.
 *   -f, --fine              Красивый JSON: вставлять пустую строку между
 *                           соседними элементами-объектами/массивами вида
 *                           "},\n{" или "],\n[" (и их сочетаниями) — то есть
 *                           когда после запятой сразу начинается новый
 *                           "{" или "[", а перед запятой был "}" или "]".
 *   -c, --compress          Не разворачивать в несколько строк массивы,
 *                           состоящие только из скалярных значений (строки/
 *                           числа/true/false/null), сколько бы элементов в
 *                           них ни было — например ["geoip:ru","geoip:cn"]
 *                           останется на одной строке.
 *
 * Массив, состоящий из ОДНОГО скалярного значения (например ["geoip:ru"]),
 * никогда не разворачивается в несколько строк — это поведение включено
 * всегда, независимо от --compress.
 *   -o, --output FILE      Писать результат в файл вместо stdout.
 *   -s, --stdout            Явно писать результат в stdout (действие по
 *                           умолчанию; имеет приоритет над -o и -w, если
 *                           указаны одновременно).
 *   -w, --in-place           Форматировать файл "на месте" — результат
 *                           записывается обратно в исходный файл. Требует
 *                           указания входного файла (не работает с stdin).
 *       --no-comments      Не поддерживать "//" и блочные комментарии (строгий JSON);
 *                           при их наличии — это ошибка синтаксиса.
 *   -h, --help              Показать справку.
 *
 * Если программа запущена вовсе без аргументов и без перенаправленного
 * (пайп/файл) stdin, вместо ожидания ввода с терминала она печатает
 * справку (usage) и завершает работу.
 *
 * Коды возврата:
 *   0 — успех
 *   1 — ошибка синтаксиса JSON (сообщение с номером строки/столбца в stderr)
 *   2 — ошибка использования (неверные аргументы командной строки, файл не открыть и т.п.)
 */

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Динамический буфер (растущая строка)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (!b->data) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
    b->data[0] = '\0';
}

static void buf_ensure(Buf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->data = (char *)realloc(b->data, b->cap);
        if (!b->data) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
    }
}

static void buf_append_n(Buf *b, const char *s, size_t n) {
    buf_ensure(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_append(Buf *b, const char *s) {
    buf_append_n(b, s, strlen(s));
}

static void buf_append_char(Buf *b, char c) {
    buf_ensure(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void buf_append_repeat(Buf *b, char c, size_t n) {
    buf_ensure(b, n);
    memset(b->data + b->len, c, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/* ------------------------------------------------------------------ */
/* Чтение всего входного файла / stdin                                 */
/* ------------------------------------------------------------------ */

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = 65536, len = 0;
    char *data = (char *)malloc(cap);
    if (!data) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
    size_t n;
    while ((n = fread(data + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            data = (char *)realloc(data, cap);
            if (!data) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
        }
    }
    data[len] = '\0';
    *out_len = len;
    return data;
}

/* ------------------------------------------------------------------ */
/* Лексер                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET, T_COLON, T_COMMA,
    T_STRING, T_NUMBER, T_TRUE, T_FALSE, T_NULL,
    T_COMMENT_LINE, T_COMMENT_BLOCK,
    T_EOF
} TokType;

typedef struct {
    TokType type;
    const char *start;   /* указатель на начало лексемы в исходном тексте */
    size_t len;           /* длина лексемы в байтах                        */
    int line, col;        /* позиция начала лексемы (1-based)              */
    bool standalone;      /* только для комментариев: одинок ли на строке  */
} Token;

typedef struct {
    Token *items;
    size_t count, cap;
} TokList;

static void toks_init(TokList *tl) {
    tl->cap = 1024;
    tl->count = 0;
    tl->items = (Token *)malloc(tl->cap * sizeof(Token));
    if (!tl->items) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
}

static void toks_push(TokList *tl, Token t) {
    if (tl->count == tl->cap) {
        tl->cap *= 2;
        tl->items = (Token *)realloc(tl->items, tl->cap * sizeof(Token));
        if (!tl->items) { fprintf(stderr, "jsonc_fmt: out of memory\n"); exit(2); }
    }
    tl->items[tl->count++] = t;
}

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    int line, col;
    bool line_has_code;   /* было ли на текущей строке что-то, кроме пробелов/комментариев */
    bool allow_comments;
    /* сведения об ошибке */
    bool error;
    char errmsg[256];
    int err_line, err_col;
} Lexer;

static void lex_error(Lexer *lx, int line, int col, const char *fmt, ...) {
    if (lx->error) return; /* сохраняем первую ошибку */
    lx->error = true;
    lx->err_line = line;
    lx->err_col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lx->errmsg, sizeof(lx->errmsg), fmt, ap);
    va_end(ap);
}

static int lx_peek(Lexer *lx, size_t ahead) {
    size_t p = lx->pos + ahead;
    if (p >= lx->len) return -1;
    return (unsigned char)lx->src[p];
}

static int lx_cur(Lexer *lx) { return lx_peek(lx, 0); }

static void lx_advance(Lexer *lx) {
    if (lx->pos >= lx->len) return;
    char c = lx->src[lx->pos];
    lx->pos++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
        lx->line_has_code = false;
    } else {
        lx->col++;
    }
}

/* Пропускает пробелы (не комментарии). Возвращает, встретился ли перевод строки. */
static void lx_skip_ws(Lexer *lx) {
    for (;;) {
        int c = lx_cur(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lx_advance(lx);
        } else {
            break;
        }
    }
}

static bool is_ident_start(int c) { return c == 't' || c == 'f' || c == 'n'; }

static void lex_string(Lexer *lx, Token *tok) {
    int sl = lx->line, sc = lx->col;
    const char *start = lx->src + lx->pos;
    lx_advance(lx); /* opening quote */
    for (;;) {
        int c = lx_cur(lx);
        if (c == -1) {
            lex_error(lx, sl, sc, "незакрытая строка");
            return;
        }
        if (c == '\n') {
            lex_error(lx, lx->line, lx->col, "незакрытая строка (перевод строки внутри строки)");
            return;
        }
        if ((unsigned char)c < 0x20) {
            lex_error(lx, lx->line, lx->col, "недопустимый управляющий символ 0x%02X в строке", c);
            return;
        }
        if (c == '"') {
            lx_advance(lx);
            break;
        }
        if (c == '\\') {
            lx_advance(lx);
            int e = lx_cur(lx);
            switch (e) {
                case '"': case '\\': case '/': case 'b':
                case 'f': case 'n': case 'r': case 't':
                    lx_advance(lx);
                    break;
                case 'u': {
                    lx_advance(lx);
                    for (int i = 0; i < 4; i++) {
                        int h = lx_cur(lx);
                        if (!isxdigit(h)) {
                            lex_error(lx, lx->line, lx->col, "некорректная \\u-последовательность");
                            return;
                        }
                        lx_advance(lx);
                    }
                    break;
                }
                default:
                    lex_error(lx, lx->line, lx->col, "недопустимая escape-последовательность '\\%c'",
                              e == -1 ? '?' : e);
                    return;
            }
        } else {
            lx_advance(lx);
        }
    }
    tok->type = T_STRING;
    tok->start = start;
    tok->len = (size_t)((lx->src + lx->pos) - start);
    tok->line = sl; tok->col = sc;
    lx->line_has_code = true;
}

static void lex_number(Lexer *lx, Token *tok) {
    int sl = lx->line, sc = lx->col;
    const char *start = lx->src + lx->pos;

    if (lx_cur(lx) == '-') lx_advance(lx);

    if (lx_cur(lx) == '0') {
        lx_advance(lx);
    } else if (isdigit(lx_cur(lx))) {
        while (isdigit(lx_cur(lx))) lx_advance(lx);
    } else {
        lex_error(lx, sl, sc, "некорректное число");
        return;
    }

    if (lx_cur(lx) == '.') {
        lx_advance(lx);
        if (!isdigit(lx_cur(lx))) {
            lex_error(lx, lx->line, lx->col, "ожидалась цифра после '.' в числе");
            return;
        }
        while (isdigit(lx_cur(lx))) lx_advance(lx);
    }

    if (lx_cur(lx) == 'e' || lx_cur(lx) == 'E') {
        lx_advance(lx);
        if (lx_cur(lx) == '+' || lx_cur(lx) == '-') lx_advance(lx);
        if (!isdigit(lx_cur(lx))) {
            lex_error(lx, lx->line, lx->col, "ожидалась цифра в экспоненте числа");
            return;
        }
        while (isdigit(lx_cur(lx))) lx_advance(lx);
    }

    tok->type = T_NUMBER;
    tok->start = start;
    tok->len = (size_t)((lx->src + lx->pos) - start);
    tok->line = sl; tok->col = sc;
    lx->line_has_code = true;
}

static void lex_keyword(Lexer *lx, Token *tok) {
    int sl = lx->line, sc = lx->col;
    const char *start = lx->src + lx->pos;
    static const char *kws[] = { "true", "false", "null" };
    static const TokType kts[] = { T_TRUE, T_FALSE, T_NULL };
    for (int k = 0; k < 3; k++) {
        size_t klen = strlen(kws[k]);
        if (lx->pos + klen <= lx->len && strncmp(lx->src + lx->pos, kws[k], klen) == 0) {
            for (size_t i = 0; i < klen; i++) lx_advance(lx);
            tok->type = kts[k];
            tok->start = start;
            tok->len = klen;
            tok->line = sl; tok->col = sc;
            lx->line_has_code = true;
            return;
        }
    }
    lex_error(lx, sl, sc, "нераспознанный токен возле '%c'", (char)lx_cur(lx));
}

/* Заглянуть вперёд: есть ли до конца строки (не считая пробелов) что-то ещё? */
static bool nothing_but_ws_until_eol(Lexer *lx) {
    size_t p = lx->pos;
    while (p < lx->len) {
        char c = lx->src[p];
        if (c == '\n') return true;
        if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
        return false;
    }
    return true; /* EOF */
}

static void lex_comment(Lexer *lx, Token *tok) {
    int sl = lx->line, sc = lx->col;
    const char *start = lx->src + lx->pos;
    bool before_ok = !lx->line_has_code; /* ничего "кодового" раньше на этой строке */

    if (lx_cur(lx) == '/' && lx_peek(lx, 1) == '/') {
        lx_advance(lx); lx_advance(lx);
        while (lx_cur(lx) != -1 && lx_cur(lx) != '\n') lx_advance(lx);
        tok->type = T_COMMENT_LINE;
        tok->standalone = before_ok; /* после // всегда конец строки */
    } else if (lx_cur(lx) == '/' && lx_peek(lx, 1) == '*') {
        lx_advance(lx); lx_advance(lx);
        bool closed = false;
        while (lx_cur(lx) != -1) {
            if (lx_cur(lx) == '*' && lx_peek(lx, 1) == '/') {
                lx_advance(lx); lx_advance(lx);
                closed = true;
                break;
            }
            lx_advance(lx);
        }
        if (!closed) {
            lex_error(lx, sl, sc, "незакрытый комментарий /* ... */");
            return;
        }
        tok->type = T_COMMENT_BLOCK;
        tok->standalone = before_ok && nothing_but_ws_until_eol(lx);
    } else {
        lex_error(lx, sl, sc, "нераспознанный символ '/'");
        return;
    }
    tok->start = start;
    tok->len = (size_t)((lx->src + lx->pos) - start);
    tok->line = sl; tok->col = sc;
    /* комментарии не считаются "кодом" для строки */
}

static void lex_all(Lexer *lx, TokList *out) {
    for (;;) {
        lx_skip_ws(lx);
        if (lx->error) return;
        int c = lx_cur(lx);
        if (c == -1) {
            Token t = {0};
            t.type = T_EOF; t.line = lx->line; t.col = lx->col;
            toks_push(out, t);
            return;
        }
        Token t; memset(&t, 0, sizeof(t));
        switch (c) {
            case '{': t.type = T_LBRACE;   t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case '}': t.type = T_RBRACE;   t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case '[': t.type = T_LBRACKET; t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case ']': t.type = T_RBRACKET; t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case ':': t.type = T_COLON;    t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case ',': t.type = T_COMMA;    t.start = lx->src + lx->pos; t.len = 1; t.line = lx->line; t.col = lx->col; lx_advance(lx); lx->line_has_code = true; toks_push(out, t); break;
            case '"':
                lex_string(lx, &t);
                if (lx->error) return;
                toks_push(out, t);
                break;
            case '/':
                if (!lx->allow_comments) {
                    lex_error(lx, lx->line, lx->col, "комментарии запрещены (используется --no-comments)");
                    return;
                }
                lex_comment(lx, &t);
                if (lx->error) return;
                toks_push(out, t);
                break;
            default:
                if (c == '-' || isdigit(c)) {
                    lex_number(lx, &t);
                    if (lx->error) return;
                    toks_push(out, t);
                } else if (is_ident_start(c)) {
                    lex_keyword(lx, &t);
                    if (lx->error) return;
                    toks_push(out, t);
                } else {
                    lex_error(lx, lx->line, lx->col, "неожиданный символ '%c'", c);
                    return;
                }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Парсер-валидатор (работает по токенам, игнорируя комментарии)       */
/* ------------------------------------------------------------------ */

typedef struct {
    TokList *tl;
    size_t pos;
    bool error;
    char errmsg[256];
    int err_line, err_col;
} Parser;

static void parse_error(Parser *p, Token *t, const char *fmt, ...) {
    if (p->error) return;
    p->error = true;
    p->err_line = t->line;
    p->err_col = t->col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->errmsg, sizeof(p->errmsg), fmt, ap);
    va_end(ap);
}

static Token *p_real_at(Parser *p, size_t idx) {
    while (idx < p->tl->count &&
           (p->tl->items[idx].type == T_COMMENT_LINE || p->tl->items[idx].type == T_COMMENT_BLOCK)) {
        idx++;
    }
    return &p->tl->items[idx];
}

/* следующий "настоящий" (не-комментарий) токен, не двигая позицию */
static Token *p_peek_real(Parser *p) { return p_real_at(p, p->pos); }

static void p_skip_to_real(Parser *p) {
    while (p->pos < p->tl->count &&
           (p->tl->items[p->pos].type == T_COMMENT_LINE || p->tl->items[p->pos].type == T_COMMENT_BLOCK)) {
        p->pos++;
    }
}

static Token *p_advance_real(Parser *p) {
    p_skip_to_real(p);
    Token *t = &p->tl->items[p->pos];
    if (t->type != T_EOF) p->pos++;
    return t;
}

static void parse_value(Parser *p);

static void parse_object(Parser *p) {
    p_advance_real(p); /* '{' */
    Token *t = p_peek_real(p);
    if (t->type == T_RBRACE) { p_advance_real(p); return; }
    for (;;) {
        Token *key = p_peek_real(p);
        if (key->type != T_STRING) {
            parse_error(p, key, "ожидался ключ-строка");
            return;
        }
        p_advance_real(p);
        Token *colon = p_peek_real(p);
        if (colon->type != T_COLON) {
            parse_error(p, colon, "ожидалось ':'");
            return;
        }
        p_advance_real(p);
        parse_value(p);
        if (p->error) return;
        Token *sep = p_peek_real(p);
        if (sep->type == T_COMMA) {
            p_advance_real(p);
            continue;
        } else if (sep->type == T_RBRACE) {
            p_advance_real(p);
            return;
        } else {
            parse_error(p, sep, "ожидалось ',' или '}'");
            return;
        }
    }
}

static void parse_array(Parser *p) {
    p_advance_real(p); /* '[' */
    Token *t = p_peek_real(p);
    if (t->type == T_RBRACKET) { p_advance_real(p); return; }
    for (;;) {
        parse_value(p);
        if (p->error) return;
        Token *sep = p_peek_real(p);
        if (sep->type == T_COMMA) {
            p_advance_real(p);
            continue;
        } else if (sep->type == T_RBRACKET) {
            p_advance_real(p);
            return;
        } else {
            parse_error(p, sep, "ожидалось ',' или ']'");
            return;
        }
    }
}

static void parse_value(Parser *p) {
    if (p->error) return;
    Token *t = p_peek_real(p);
    switch (t->type) {
        case T_LBRACE:   parse_object(p); break;
        case T_LBRACKET: parse_array(p); break;
        case T_STRING:   p_advance_real(p); break;
        case T_NUMBER:   p_advance_real(p); break;
        case T_TRUE:     p_advance_real(p); break;
        case T_FALSE:    p_advance_real(p); break;
        case T_NULL:     p_advance_real(p); break;
        case T_EOF:
            parse_error(p, t, "неожиданный конец файла, ожидалось значение");
            break;
        default:
            parse_error(p, t, "неожиданный токен, ожидалось значение");
            break;
    }
}

/* Возвращает true при успехе. */
static bool validate(TokList *tl, Parser *out) {
    memset(out, 0, sizeof(*out));
    out->tl = tl;
    out->pos = 0;
    parse_value(out);
    if (out->error) return false;
    Token *t = p_peek_real(out);
    if (t->type != T_EOF) {
        parse_error(out, t, "лишние данные после корневого JSON-значения");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Форматирование (второй проход, уже с комментариями)                 */
/* ------------------------------------------------------------------ */

typedef struct {
    TokList *tl;
    size_t pos;
    Buf out;
    int indent_level;
    int indent_width;   /* сколько пробелов на уровень (если не табы) */
    bool use_tabs;
    bool align_comments; /* если true — не прижимать одинокие комментарии к колонке 0 */
    bool at_line_start;   /* true, если после последнего вывода мы находимся сразу после '\n' */
    bool fine;             /* пустая строка между соседними "}"/"]" и "{"/"[" элементами */
    bool compress;         /* не разворачивать "плоские" массивы скаляров вообще */
    bool last_was_container_close; /* только что выведенное значение было объектом/массивом */
} Formatter;

static void f_write_indent(Formatter *f) {
    if (f->use_tabs) {
        buf_append_repeat(&f->out, '\t', (size_t)f->indent_level);
    } else {
        buf_append_repeat(&f->out, ' ', (size_t)f->indent_level * (size_t)f->indent_width);
    }
}

static void f_newline(Formatter *f) {
    buf_append_char(&f->out, '\n');
    f->at_line_start = true;
}

/* Завершает текущую строку (если нужно) и добавляет одну пустую строку. */
static void f_blank_line(Formatter *f) {
    if (!f->at_line_start) buf_append_char(&f->out, '\n');
    buf_append_char(&f->out, '\n');
    f->at_line_start = true;
}

/* Обрезать завершающие пробелы/CR у "сырого" текста комментария */
static void f_append_trimmed(Formatter *f, const char *s, size_t len) {
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) len--;
    buf_append_n(&f->out, s, len);
}

static void f_print_comment(Formatter *f, Token *t) {
    if (t->standalone) {
        if (!f->at_line_start) f_newline(f);
        if (f->align_comments) f_write_indent(f);
        f_append_trimmed(f, t->start, t->len);
        f_newline(f);
    } else {
        /* "не одинокий" — остаётся на той же строке, что и код перед ним */
        buf_append_char(&f->out, ' ');
        f_append_trimmed(f, t->start, t->len);
        f->at_line_start = false;
    }
}

/* Выводит все идущие подряд комментарии перед следующим "настоящим" токеном. */
static void f_flush_comments(Formatter *f) {
    while (f->pos < f->tl->count &&
           (f->tl->items[f->pos].type == T_COMMENT_LINE ||
            f->tl->items[f->pos].type == T_COMMENT_BLOCK)) {
        f_print_comment(f, &f->tl->items[f->pos]);
        f->pos++;
    }
}

static Token *f_peek_real(Formatter *f) {
    size_t idx = f->pos;
    while (idx < f->tl->count &&
           (f->tl->items[idx].type == T_COMMENT_LINE || f->tl->items[idx].type == T_COMMENT_BLOCK)) {
        idx++;
    }
    return &f->tl->items[idx];
}

static void f_ensure_line_start(Formatter *f) {
    if (!f->at_line_start) f_newline(f);
    f_write_indent(f);
    f->at_line_start = false;
}

static void format_value(Formatter *f);

/*
 * Проверяет, что массив, начинающийся сразу с позиции start_pos (первый
 * токен после '['), состоит только из скалярных значений (строки, числа,
 * true/false/null), разделённых запятыми, без вложенных объектов/массивов
 * и без комментариев внутри. Если это так — возвращает true и записывает
 * количество элементов в *out_count (0 для пустого массива). Иначе false
 * (в этом случае массив всегда форматируется обычным многострочным
 * образом, чтобы не потерять комментарии и не портить читаемость вложенных
 * структур).
 */
static bool array_is_flat_scalars(Formatter *f, size_t start_pos, int *out_count) {
    int count = 0;
    size_t idx = start_pos;
    for (;;) {
        if (idx >= f->tl->count) return false;
        TokType tt = f->tl->items[idx].type;
        if (tt == T_RBRACKET) { *out_count = count; return true; }
        if (tt == T_STRING || tt == T_NUMBER || tt == T_TRUE || tt == T_FALSE || tt == T_NULL) {
            count++;
            idx++;
            continue;
        }
        if (tt == T_COMMA) { idx++; continue; }
        /* '{', '[' или комментарий — массив не "плоский" */
        return false;
    }
}

/* Печатает один скалярный токен (строка/число/true/false/null) как есть. */
static void f_print_scalar(Formatter *f, Token *t) {
    switch (t->type) {
        case T_STRING:
        case T_NUMBER:
            buf_append_n(&f->out, t->start, t->len);
            break;
        case T_TRUE:  buf_append(&f->out, "true");  break;
        case T_FALSE: buf_append(&f->out, "false"); break;
        case T_NULL:  buf_append(&f->out, "null");  break;
        default: break;
    }
}

static void format_container(Formatter *f, TokType open, TokType close, const char *open_s, const char *close_s, bool is_object) {
    buf_append(&f->out, open_s);
    f->at_line_start = false;
    f->pos++; /* пропускаем сам '{' или '[' — он уже был выведен */

    if (!is_object) {
        int count = 0;
        bool flat = array_is_flat_scalars(f, f->pos, &count);
        /* Массив с одним скалярным значением никогда не разворачивается.
         * С опцией --compress не разворачивается ни один "плоский" массив
         * скаляров, сколько бы в нём ни было элементов. */
        if (flat && (count <= 1 || f->compress)) {
            bool first = true;
            while (f->tl->items[f->pos].type != close) {
                if (f->tl->items[f->pos].type == T_COMMA) { f->pos++; continue; }
                if (!first) buf_append(&f->out, ", ");
                f_print_scalar(f, &f->tl->items[f->pos]);
                f->pos++;
                first = false;
            }
            f->pos++; /* пропускаем ']' */
            buf_append(&f->out, close_s);
            f->at_line_start = false;
            (void)open;
            return;
        }
    }

    f->indent_level++;

    f_flush_comments(f);
    Token *next = f_peek_real(f);
    bool empty = (next->type == close);

    if (empty) {
        f->indent_level--;
        if (f->out.len > 0 && f->out.data[f->out.len - 1] != open_s[0]) {
            /* были только комментарии внутри — контейнер не пуст визуально */
            f_ensure_line_start(f);
        }
        buf_append(&f->out, close_s);
        f->at_line_start = false;
        f->pos++; /* пропускаем '}' / ']' */
        return;
    }

    bool first = true;
    for (;;) {
        f_ensure_line_start(f);
        if (is_object) {
            Token *key = f_peek_real(f);
            buf_append_n(&f->out, key->start, key->len);
            f->pos++;
            f_flush_comments(f);
            buf_append(&f->out, ": ");
            f->at_line_start = false;
            f->pos++; /* ':' */
            f_flush_comments(f);
            format_value(f);
        } else {
            format_value(f);
        }
        (void)first; first = false;

        f_flush_comments(f);
        Token *sep = f_peek_real(f);
        if (sep->type == T_COMMA) {
            buf_append_char(&f->out, ',');
            f->at_line_start = false;
            f->pos++; /* ',' */
            f_flush_comments(f);
            Token *after = f_peek_real(f);
            if (f->fine && f->last_was_container_close &&
                (after->type == T_LBRACE || after->type == T_LBRACKET)) {
                f_blank_line(f);
            }
            continue;
        } else if (sep->type == close) {
            break;
        } else {
            /* не должно происходить после успешной валидации */
            break;
        }
    }

    f->indent_level--;
    f_ensure_line_start(f);
    buf_append(&f->out, close_s);
    f->at_line_start = false;
    f->pos++; /* '}' / ']' */
    (void)open;
}

static void format_value(Formatter *f) {
    f_flush_comments(f);
    Token *t = f_peek_real(f);
    switch (t->type) {
        case T_LBRACE:
            format_container(f, T_LBRACE, T_RBRACE, "{", "}", true);
            f->last_was_container_close = true;
            break;
        case T_LBRACKET:
            format_container(f, T_LBRACKET, T_RBRACKET, "[", "]", false);
            f->last_was_container_close = true;
            break;
        case T_STRING:
            buf_append_n(&f->out, t->start, t->len);
            f->at_line_start = false;
            f->pos++;
            f->last_was_container_close = false;
            break;
        case T_NUMBER:
            buf_append_n(&f->out, t->start, t->len);
            f->at_line_start = false;
            f->pos++;
            f->last_was_container_close = false;
            break;
        case T_TRUE:
            buf_append(&f->out, "true");
            f->at_line_start = false;
            f->pos++;
            f->last_was_container_close = false;
            break;
        case T_FALSE:
            buf_append(&f->out, "false");
            f->at_line_start = false;
            f->pos++;
            f->last_was_container_close = false;
            break;
        case T_NULL:
            buf_append(&f->out, "null");
            f->at_line_start = false;
            f->pos++;
            f->last_was_container_close = false;
            break;
        default:
            /* не должно происходить после успешной валидации */
            break;
    }
}

static void format_root(Formatter *f) {
    f->pos = 0;
    f->indent_level = 0;
    f->at_line_start = true;
    buf_init(&f->out);

    f_flush_comments(f); /* комментарии в самом начале файла */
    format_value(f);
    f_flush_comments(f); /* комментарии в конце файла, после корневого значения */
    if (f->out.len == 0 || f->out.data[f->out.len - 1] != '\n') {
        buf_append_char(&f->out, '\n');
    }
}

/* ------------------------------------------------------------------ */
/* main / CLI                                                          */
/* ------------------------------------------------------------------ */

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
        "Использование: %s [опции] [файл]\n"
        "Форматирует и валидирует JSON (с поддержкой // и /* */ комментариев).\n"
        "Если файл не указан или указан \"-\", читается stdin. Результат — в stdout.\n"
        "\n"
        "Опции:\n"
        "  -i, --indent N        ширина отступа в пробелах (по умолчанию 2)\n"
        "  -t, --tabs             использовать табуляцию для отступов\n"
        "  -a, --align-comments   выравнивать одинокие комментарии по уровню\n"
        "                          вложенности, а не прижимать к колонке 0\n"
        "  -f, --fine              красивый JSON: пустая строка между соседними\n"
        "                          элементами-объектами/массивами (между \"},\"\n"
        "                          и следующим \"{\", и т.п. сочетаниями)\n"
        "  -c, --compress          не разворачивать массивы из скаляров даже\n"
        "                          при многих элементах (массив с одним\n"
        "                          скаляром никогда не разворачивается и без\n"
        "                          этой опции)\n"
        "  -o, --output FILE      писать результат в файл вместо stdout\n"
        "  -s, --stdout            писать результат в stdout явно (приоритет\n"
        "                          над -o и -w, если указаны вместе)\n"
        "  -w, --in-place          форматировать входной файл \"на месте\"\n"
        "                          (перезаписать его же); требует файл, не stdin\n"
        "      --no-comments      запретить // и /* */ (строгий JSON)\n"
        "  -h, --help              показать эту справку\n"
        "\n"
        "Без аргументов и без перенаправленного stdin печатается эта справка.\n"
        "\n"
        "Коды возврата: 0 — успех, 1 — ошибка JSON, 2 — ошибка использования.\n",
        prog);
}

int main(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    int indent_width = 2;
    bool use_tabs = false;
    bool align_comments = false;
    bool fine = false;
    bool compress = false;
    bool allow_comments = true;
    bool force_stdout = false;
    bool in_place = false;

    if (argc == 1 && isatty(fileno(stdin))) {
        /* Запуск без параметров и без перенаправленного ввода — показать usage. */
        print_usage(stdout, argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(a, "-i") == 0 || strcmp(a, "--indent") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "jsonc_fmt: опции %s требуется значение\n", a); return 2; }
            indent_width = atoi(argv[++i]);
            if (indent_width < 0 || indent_width > 16) {
                fprintf(stderr, "jsonc_fmt: недопустимая ширина отступа\n");
                return 2;
            }
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--tabs") == 0) {
            use_tabs = true;
        } else if (strcmp(a, "-a") == 0 || strcmp(a, "--align-comments") == 0) {
            align_comments = true;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--fine") == 0) {
            fine = true;
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "--compress") == 0) {
            compress = true;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "jsonc_fmt: опции %s требуется значение\n", a); return 2; }
            out_path = argv[++i];
        } else if (strcmp(a, "-s") == 0 || strcmp(a, "--stdout") == 0) {
            force_stdout = true;
        } else if (strcmp(a, "-w") == 0 || strcmp(a, "--in-place") == 0) {
            in_place = true;
        } else if (strcmp(a, "--no-comments") == 0) {
            allow_comments = false;
        } else if (strcmp(a, "-") == 0) {
            in_path = NULL;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "jsonc_fmt: неизвестная опция '%s'\n", a);
            print_usage(stderr, argv[0]);
            return 2;
        } else {
            if (in_path != NULL) {
                fprintf(stderr, "jsonc_fmt: указано больше одного входного файла\n");
                return 2;
            }
            in_path = a;
        }
    }

    if (in_place && in_path == NULL) {
        fprintf(stderr, "jsonc_fmt: -w/--in-place требует указания входного файла (не работает с stdin)\n");
        return 2;
    }

    FILE *in = stdin;
    if (in_path != NULL) {
        in = fopen(in_path, "rb");
        if (!in) {
            fprintf(stderr, "jsonc_fmt: не удалось открыть файл '%s': %s\n", in_path, strerror(errno));
            return 2;
        }
    }

    size_t src_len;
    char *src = read_all(in, &src_len);
    if (in != stdin) fclose(in);

    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.src = src;
    lx.len = src_len;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    lx.line_has_code = false;
    lx.allow_comments = allow_comments;

    TokList tl;
    toks_init(&tl);
    lex_all(&lx, &tl);

    if (lx.error) {
        fprintf(stderr, "Ошибка: %s (строка %d, столбец %d)\n", lx.errmsg, lx.err_line, lx.err_col);
        return 1;
    }

    Parser ps;
    if (!validate(&tl, &ps)) {
        fprintf(stderr, "Ошибка: %s (строка %d, столбец %d)\n", ps.errmsg, ps.err_line, ps.err_col);
        return 1;
    }

    Formatter f;
    memset(&f, 0, sizeof(f));
    f.tl = &tl;
    f.indent_width = indent_width;
    f.use_tabs = use_tabs;
    f.align_comments = align_comments;
    f.fine = fine;
    f.compress = compress;
    format_root(&f);

    const char *dest_path = NULL; /* NULL значит stdout */
    if (!force_stdout) {
        if (out_path != NULL) {
            dest_path = out_path;
        } else if (in_place) {
            dest_path = in_path;
        }
    }

    FILE *out = stdout;
    if (dest_path != NULL) {
        out = fopen(dest_path, "wb");
        if (!out) {
            fprintf(stderr, "jsonc_fmt: не удалось открыть файл '%s' для записи: %s\n", dest_path, strerror(errno));
            return 2;
        }
    }
    fwrite(f.out.data, 1, f.out.len, out);
    if (out != stdout) fclose(out);

    return 0;
}
