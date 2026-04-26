#include "expr.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *input;
    int pos;
    WorkflowContext *ctx;
} ExprParser;

static void skip_ws(ExprParser *p) {
    while (p->input[p->pos] && isspace((unsigned char)p->input[p->pos])) p->pos++;
}

static bool match_word(ExprParser *p, const char *word) {
    skip_ws(p);
    int len = strlen(word);
    if (strncasecmp(p->input + p->pos, word, len) == 0 &&
        !isalnum((unsigned char)p->input[p->pos + len]) &&
        p->input[p->pos + len] != '_') {
        p->pos += len;
        return true;
    }
    return false;
}

static char *parse_string_lit(ExprParser *p) {
    skip_ws(p);
    if (p->input[p->pos] != '\'') return NULL;
    p->pos++;
    int start = p->pos;
    while (p->input[p->pos] && p->input[p->pos] != '\'') p->pos++;
    char *result = strndup(p->input + start, p->pos - start);
    if (p->input[p->pos] == '\'') p->pos++;
    return result;
}

static char *parse_ref(ExprParser *p) {
    skip_ws(p);
    int start = p->pos;
    while (p->input[p->pos] &&
           (isalnum((unsigned char)p->input[p->pos]) ||
            p->input[p->pos] == '_' ||
            p->input[p->pos] == '.' ||
            p->input[p->pos] == '-')) {
        p->pos++;
    }
    if (p->pos == start) return NULL;
    return strndup(p->input + start, p->pos - start);
}

static bool parse_atom(ExprParser *p);

static bool parse_term(ExprParser *p) {
    skip_ws(p);
    bool negate = match_word(p, "NOT");
    bool result = parse_atom(p);
    return negate ? !result : result;
}

static bool parse_atom(ExprParser *p) {
    skip_ws(p);

    if (p->input[p->pos] == '(') {
        p->pos++;
        bool result = parse_term(p);
        skip_ws(p);

        while (match_word(p, "AND") || match_word(p, "OR")) {
            bool is_and = (p->input[p->pos - 3] == 'A' || p->input[p->pos - 3] == 'a');
            bool right = parse_term(p);
            result = is_and ? (result && right) : (result || right);
        }

        skip_ws(p);
        if (p->input[p->pos] == ')') p->pos++;
        return result;
    }

    char *ref = parse_ref(p);
    if (!ref) return false;

    skip_ws(p);

    if (match_word(p, "contains")) {
        char *needle = parse_string_lit(p);
        if (!needle) { free(ref); return false; }
        char *val = workflow_resolve_var(p->ctx, ref);
        bool result = val && strstr(val, needle) != NULL;
        free(ref);
        free(needle);
        free(val);
        return result;
    }

    char op[3] = {0};
    if (p->input[p->pos] == '=' && p->input[p->pos + 1] == '=') {
        op[0] = '='; op[1] = '='; p->pos += 2;
    } else if (p->input[p->pos] == '!' && p->input[p->pos + 1] == '=') {
        op[0] = '!'; op[1] = '='; p->pos += 2;
    } else if (p->input[p->pos] == '>' && p->input[p->pos + 1] == '=') {
        op[0] = '>'; op[1] = '='; p->pos += 2;
    } else if (p->input[p->pos] == '<' && p->input[p->pos + 1] == '=') {
        op[0] = '<'; op[1] = '='; p->pos += 2;
    } else if (p->input[p->pos] == '>') {
        op[0] = '>'; p->pos++;
    } else if (p->input[p->pos] == '<') {
        op[0] = '<'; p->pos++;
    } else {
        char *val = workflow_resolve_var(p->ctx, ref);
        bool result = val && strlen(val) > 0 && strcmp(val, "false") != 0 && strcmp(val, "0") != 0;
        free(ref);
        free(val);
        return result;
    }

    skip_ws(p);

    char *rhs = NULL;
    if (p->input[p->pos] == '\'') {
        rhs = parse_string_lit(p);
    } else {
        rhs = parse_ref(p);
    }

    char *lhs_val = workflow_resolve_var(p->ctx, ref);
    free(ref);

    if (!lhs_val || !rhs) {
        free(lhs_val);
        free(rhs);
        return false;
    }

    bool result = false;
    if (op[0] == '=' && op[1] == '=') {
        result = strcmp(lhs_val, rhs) == 0;
    } else if (op[0] == '!' && op[1] == '=') {
        result = strcmp(lhs_val, rhs) != 0;
    } else {
        double lv = atof(lhs_val);
        double rv = atof(rhs);
        if (op[0] == '>' && op[1] == '=') result = lv >= rv;
        else if (op[0] == '<' && op[1] == '=') result = lv <= rv;
        else if (op[0] == '>') result = lv > rv;
        else if (op[0] == '<') result = lv < rv;
    }

    free(lhs_val);
    free(rhs);
    return result;
}

bool expr_eval(WorkflowContext *ctx, const char *expr) {
    if (!ctx || !expr) return false;

    ExprParser p = { .input = expr, .pos = 0, .ctx = ctx };

    bool result = parse_term(&p);

    while (true) {
        skip_ws(&p);
        if (match_word(&p, "AND")) {
            bool right = parse_term(&p);
            result = result && right;
        } else if (match_word(&p, "OR")) {
            bool right = parse_term(&p);
            result = result || right;
        } else {
            break;
        }
    }

    return result;
}
