#include "mpc.h"
#include <editline/readline.h>

#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
  }

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
          "Function '%s' passed incorrect type for argument %i. " \
          "Got %s, Expected %s.", \
          func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
  LASSERT(args, args->count == num, \
          "Function '%s' passed incorrect number of arguments. " \
          "Got %i, Expected %i.", \
          func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
  LASSERT(args, args->cell[index]->count != 0, \
          "Function '%s' passed {} for argument %i.", func, index);

mpc_parser_t* Number;
mpc_parser_t* Symbol;
mpc_parser_t* String;
mpc_parser_t* Comment;
mpc_parser_t* Sxpr;
mpc_parser_t* Qxpr;
mpc_parser_t* Expr;
mpc_parser_t* Lispy;

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

enum {
  LVAL_ERR,
  LVAL_NUM,
  LVAL_SYM,
  LVAL_STR,
  LVAL_FUN,
  LVAL_SXPR,
  LVAL_QXPR
};

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
  int type;
  long num;
  char* err;
  char* sym;
  char* str;
  lbuiltin builtin;
  lenv* env;
  lval* formals;
  lval* body;

  int count;
  lval** cell;
};

struct lenv {
  lenv* par;
  int count;
  char** syms;
  lval** vals;
};

void lenv_del(lenv* e);
lenv* lenv_copy(lenv* e);

lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = func;
  return v;
}

lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* fmt, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  va_list va;
  va_start(va, fmt);

  v->err = malloc(512);

  vsnprintf(v->err, 511, fmt, va);

  v->err = realloc(v->err, strlen(v->err)+1);

  va_end(va);

  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_str(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(s) + 1);
  strcpy(v->str, s);

  return v;
}

lval* lval_sxpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval* lval_qxpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_FUN:
      if (!v->builtin) {
        lenv_del(v->env);
        lval_del(v->formals);
        lval_del(v->body);
      }
      break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_STR: free(v->str); break;
    case LVAL_SXPR:
    case LVAL_QXPR:
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
      break;
  }

  free(v);
}

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type) {
    case LVAL_FUN:
      if (v->builtin) {
        x->builtin = v->builtin;
      } else {
        x->builtin = NULL;
        x->env = lenv_copy(v->env);
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
      }
      break;
    case LVAL_NUM: x->num = v->num; break;
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->err, v->err); break;
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym); break;
    case LVAL_STR:
      x->str = malloc(strlen(v->str) +1);
      strcpy(x->str, v->str); break;
    case LVAL_SXPR:
    case LVAL_QXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
      break;
  }

  return x;
}

lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];

  memmove(&v->cell[i], &v->cell[i+1],
    sizeof(lval*) * (v->count-i-1));

  v->count--;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);

  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);

    if (i != (v->count-1)) {
      putchar(' ');
    }
  }

  putchar(close);
}

void lval_print_str(lval* v) {
  char* escaped = malloc(strlen(v->str)+1);
  strcpy(escaped, v->str);
  escaped = mpcf_escape(escaped);
  printf("\"%s\"", escaped);
  free(escaped);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM:   printf("%li", v->num); break;
    case LVAL_ERR:   printf("Error: %s", v->err); break;
    case LVAL_SYM:   printf("%s", v->sym); break;
    case LVAL_STR:  lval_print_str(v); break;
    case LVAL_SXPR: lval_expr_print(v, '(', ')'); break;
    case LVAL_QXPR: lval_expr_print(v, '{', '}'); break;
    case LVAL_FUN:
      if (v->builtin) {
        puts("<builtin>");
      } else {
        printf("(\\ ");
        lval_print(v->formals);
        putchar(' ');
        lval_print(v->body);
        putchar(')');
      }
  }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }
lval* lval_eval(lenv* e, lval* v);

char* ltype_name(int t) {
  switch (t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_STR: return "String";
    case LVAL_SXPR: return "S-Expression";
    case LVAL_QXPR: return "Q-Expression";
    default: return "Unknown type";
  }
}

lval* builtin_head(lenv* e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QXPR,
          "Function 'head' passed incorrect type! %s",
          ltype_name(a->cell[0]->type));
  LASSERT(a, a->cell[0]->count != 0,
          "Function 'head' passed {}!");

  lval* v = lval_take(a, 0);
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lenv* e, lval* a) {
  LASSERT(a, a->count == 1,
          "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QXPR,
          "Function 'tail' passed incorrect type! %s",
          ltype_name(a->cell[0]->type));
  LASSERT(a, a->cell[0]->count != 0,
          "Function 'tail' passed {}!");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_list(lenv* e, lval* a) {
  a->type= LVAL_QXPR;
  return a;
}

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT(a, a->count == 1,
          "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QXPR,
          "Function 'eval' passed incorrect type! %s",
          ltype_name(a->cell[0]->type));

  lval* x = lval_take(a, 0);
  x->type = LVAL_SXPR;

  return lval_eval(e, x);
}

lval* lval_join(lval* x, lval* y) {
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  lval_del(y);

  return x;
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QXPR,
            "Function 'join' passed incorrect type! %s",
            ltype_name(a->cell[0]->type));
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);

  return x;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }
  lval* x = lval_pop(a, 0);

  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  while (a->count > 0) {
    lval* y = lval_pop(a, 0);
    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "%") == 0) { x->num %= y->num; }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division By Zero.");
        break;
      }
      x->num /= y->num;
    }
    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->par = NULL;
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

void lenv_del(lenv* e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }

  free(e->syms);
  free(e->vals);
  free(e);
}

lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n-> par = e->par;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);

  for (int i = 0; i < e->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }

  return n;
}

lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }

  if (e->par) {
    return lenv_get(e->par, k);
  }

  return lval_err("unbound symbol '%s'", k->sym);
}

void lenv_put(lenv* e, lval* k, lval* v) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
}

void lenv_def(lenv* e, lval* k, lval* v) {
  while (e->par) { e = e->par; }

  lenv_put(e, k, v);
}

lval* lval_lambda(lval* formals, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;

  v->builtin = NULL;

  v->env = lenv_new();

  v->formals = formals;
  v->body = body;

  return v;
}

lval* lval_call(lenv* e, lval* f, lval* a) {
  if (f->builtin) { return f->builtin(e, a); }

  int given = a->count;
  int total = f->formals->count;

  while (a->count) {
    if (f->formals->count == 0) {
      lval_del(a);
      return lval_err(
        "function passed too many arguments. expected %i but got %i",
        given, total);
    }

    lval* sym = lval_pop(f->formals, 0);

    if (strcmp(sym->sym, "&") == 0) {
      if (f->formals->count != 1) {
        lval_del(a);

        return lval_err("Function format invalid; symbol '&' not followed by a single symbol");
      }

      lval* nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym);
      lval_del(nsym);

      break;
    }

    lval* val = lval_pop(a, 0);

    lenv_put(f->env, sym, val);

    lval_del(sym);
    lval_del(val);
  }

  lval_del(a);

  if (f->formals->count > 0 &&
      strcmp(f->formals->cell[0]->sym, "&") == 0) {
    if (f->formals->count != 2) {
      return lval_err("Function format invalid; symbol '&' not followed by a single symbol");
    }

    lval_del(lval_pop(f->formals, 0));

    lval* sym = lval_pop(f->formals, 0);
    lval* val = lval_qxpr();

    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  if (f->formals->count == 0) {
    f->env->par = e;

    return builtin_eval(f->env, lval_add(lval_sxpr(), lval_copy(f->body)));
  }

  return lval_copy(f);
}

lval* builtin_ord(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE(op, a, 0, LVAL_NUM);
  LASSERT_TYPE(op, a, 1, LVAL_NUM);

  int r;

  if (strcmp(op, ">") == 0) {
    r = (a->cell[0]->num > a->cell[1]->num);
  }

  if (strcmp(op, "<") == 0) {
    r = (a->cell[0]->num < a->cell[1]->num);
  }

  if (strcmp(op, ">=") == 0) {
    r = (a->cell[0]->num <= a->cell[1]->num);
  }

  if (strcmp(op, "<=") == 0) {
    r = (a->cell[0]->num <= a->cell[1]->num);
  }

  lval_del(a);

  return lval_num(r);
}

lval* builtin_gt(lenv* e, lval* a) {
  return builtin_ord(e, a, ">");
}

lval* builtin_lt(lenv* e, lval* a) {
  return builtin_ord(e, a, "<");
}

lval* builtin_ge(lenv* e, lval* a) {
  return builtin_ord(e, a, ">=");
}

lval* builtin_le(lenv* e, lval* a) {
  return builtin_ord(e, a, "<=");
}

int lval_eq(lval* x, lval* y) {
  if (x->type != y->type) {
    return 0;
  }

  switch (x->type) {
    case LVAL_NUM:
      return x->num == y->num;
    case LVAL_ERR:
      return strcmp(x->err, y->err) == 0;
    case LVAL_SYM:
      return strcmp(x->sym, y->sym) == 0;
    case LVAL_STR:
      return strcmp(x->str, y->str) == 0;
    case LVAL_FUN:
      if (x->builtin || y->builtin) {
        return x->builtin == y->builtin;
      }

      return lval_eq(x->formals, y->formals) &&
        lval_eq(x->body, y->body);

    case LVAL_QXPR:
    case LVAL_SXPR:
      if (x->count != y->count) {
        return 0;
      }

      for (int i = 0; i < x->count; i++) {
        if (!lval_eq(x->cell[i], y->cell[i])) {
          return 0;
        }
      }

      return 1;
    default:
      break;
  }

  return 0;
}

lval* builtin_cmp(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);

  int r;

  if (strcmp(op, "==") == 0) {
    r = lval_eq(a->cell[0], a->cell[1]);
  }

  if (strcmp(op, "!=") == 0) {
    r = !lval_eq(a->cell[0], a->cell[1]);
  }

  lval_del(a);

  return lval_num(r);
}

lval* builtin_eq(lenv* e, lval* a) {
  return builtin_cmp(e, a, "==");
}

lval* builtin_ne(lenv* e, lval* a) {
  return builtin_cmp(e, a, "!=");
}

lval* builtin_if(lenv* e, lval* a) {
  LASSERT_NUM("if", a, 3);
  LASSERT_TYPE("if", a, 0, LVAL_NUM);
  LASSERT_TYPE("if", a, 1, LVAL_QXPR);
  LASSERT_TYPE("if", a, 2, LVAL_QXPR);

  lval* x;
  a->cell[1]->type = LVAL_SXPR;
  a->cell[2]->type = LVAL_SXPR;

  if (a->cell[0]->num) {
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    x = lval_eval(e, lval_pop(a, 2));
  }

  lval_del(a);

  return x;
}

lval* builtin_lambda(lenv* e, lval* a) {
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QXPR);

  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define non-symbol %s",
            ltype_name(a->cell[0]->cell[i]->type));
  }

  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  lval_del(a);

  return lval_lambda(formals, body);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_TYPE(func, a, 0, LVAL_QXPR);

  lval* syms = a->cell[0];

  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
            "function %s cannot define nonsymbol %s",
            ltype_name(syms->cell[i]->type),
            ltype_name(LVAL_SYM));
  }

  LASSERT(a, (syms->count == a->count-1),
          "function %s passed too many arguments for symbols %i",
          func, syms->count);

  for (int i = 0; i < syms->count; i++) {
    if (strcmp(func, "def") == 0) {
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }

    if (strcmp(func, "=") == 0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }

  lval_del(a);

  return lval_sxpr();
}

lval* builtin_def(lenv* e, lval* a) {
  return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
  return builtin_var(e, a, "=");
}

lval* builtin_load(lenv* e, lval* a);
lval* builtin_print(lenv* e, lval* a);
lval* builtin_error(lenv* e, lval* a);

void lenv_add_builtins(lenv* e) {
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "=", builtin_put);
  lenv_add_builtin(e, "\\", builtin_lambda);
  lenv_add_builtin(e, "if", builtin_if);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_ne);
  lenv_add_builtin(e, ">", builtin_gt);
  lenv_add_builtin(e, "<", builtin_lt);
  lenv_add_builtin(e, ">=", builtin_ge);
  lenv_add_builtin(e, "<=", builtin_le);
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "load", builtin_load);
  lenv_add_builtin(e, "print", builtin_print);
  lenv_add_builtin(e, "error", builtin_error);
}

lval* lval_eval_sxpr(lenv* e, lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  if (v->count == 0) { return v; }
  if (v->count == 1) { return lval_eval(e, lval_take(v, 0)); }

  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval* err = lval_err(
      "S-Expression starts with incorrect type. "
      "Got %s, Expected %s.",
      ltype_name(f->type), ltype_name(LVAL_FUN));
    lval_del(f); lval_del(v);
    return err;
  }

  lval* result = lval_call(e, f, v);
  lval_del(f);
  return result;
}

lval* lval_eval(lenv* e, lval* v) {
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }

  if (v->type == LVAL_SXPR) { return lval_eval_sxpr(e, v); }
  return v;
}

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ?
    lval_num(x) : lval_err("invalid number");
}

lval* lval_read_str(mpc_ast_t* t) {
  t->contents[strlen(t->contents)-1] = '\0';

  char* unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  unescaped = mpcf_unescape(unescaped);
  lval* str = lval_str(unescaped);

  free(unescaped);

  return str;
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
  if (strstr(t->tag, "string")) { return lval_read_str(t); }
  if (strstr(t->tag, "comment")) { return NULL; }

  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sxpr(); }
  if (strstr(t->tag, "sxpr"))  { x = lval_sxpr(); }
  if (strstr(t->tag, "qxpr")) { x = lval_qxpr(); }

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

lval* builtin_load(lenv* e, lval* a) {
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->str, Lispy, &r)) {
    lval* expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    while (expr->count) {
      lval* x = lval_eval(e, lval_pop(expr, 0));
      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }

    lval_del(expr);
    lval_del(a);

    return lval_sxpr();
  } else {
    char* err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);

    lval* err = lval_err("Could not load Library %s", err_msg);
    free(err_msg);
    lval_del(a);

    return err;
  }
}

lval* builtin_print(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    lval_print(a->cell[i]); putchar(' ');
  }

  putchar('\n');
  lval_del(a);

  return lval_sxpr();
}

lval* builtin_error(lenv* e, lval* a) {
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);

  lval* err = lval_err(a->cell[0]->str);

  lval_del(a);
  return err;
}

int main(int argc, char** argv) {
  Number  = mpc_new("number");
  Symbol  = mpc_new("symbol");
  String  = mpc_new("string");
  Comment = mpc_new("comment");
  Sxpr   = mpc_new("sxpr");
  Qxpr   = mpc_new("qxpr");
  Expr    = mpc_new("expr");
  Lispy   = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                          \
      number : /-?[0-9]+/ ;                    \
      symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ; \
      string : /\"(\\\\.|[^\"])*\"/ ;          \
      comment : /;[^\\r\\n]*/ ;                \
      sxpr  : '(' <expr>* ')' ;                \
      qxpr  : '{' <expr>* '}' ;                \
      expr   : <number> | <symbol> | <string> | <sxpr> | <qxpr> ;  \
      lispy  : /^/ <expr>* /$/ ;               \
    ", Number, Symbol, String, Comment, Sxpr, Qxpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+C to Exit\n");

  lenv* e = lenv_new();
  lenv_add_builtins(e);

  lval* args = lval_add(lval_sxpr(), lval_str("stdlib.lispy"));
  lval* lib = builtin_load(e, args);
  if (lib->type == LVAL_ERR) { lval_println(lib); }
  lval_del(lib);

  if (argc == 1) {
    while (1) {
      char* input = readline("λ> ");
      add_history(input);

      mpc_result_t r;
      if (mpc_parse("<stdin>", input, Lispy, &r)) {
        lval* x = lval_eval(e, lval_read(r.output));
        lval_println(x);
        lval_del(x);

        mpc_ast_delete(r.output);
      } else {
        mpc_err_print(r.error);
        mpc_err_delete(r.error);
      }

      free(input);
    }
  }

  if (argc >= 2) {
    for (int i = 1; i < argc; i++) {
      lval* args = lval_add(lval_sxpr(), lval_str(argv[i]));
      lval* x = builtin_load(e, args);
      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }
  }

  lenv_del(e);

  mpc_cleanup(6, Number, Symbol, String, Comment, Sxpr, Qxpr, Expr, Lispy);

  return 0;
}
