%{
#include <cstdlib>
#include <cstring>

#include "frontend/parser_driver.hpp"

using mbpf::frontend::Expr;
using mbpf::frontend::ExprKind;
using mbpf::frontend::make_binary;
using mbpf::frontend::make_boolean;
using mbpf::frontend::make_ident;
using mbpf::frontend::make_integer;
using mbpf::frontend::make_unary;
using mbpf::frontend::set_parse_error;
using mbpf::frontend::set_parse_root;

extern int yylex(void);
void yyerror(const char *s);
%}

%code requires {
#include "frontend/ast.hpp"
}

%define parse.error verbose

%union {
    long long ival;
    char *sval;
    int bval;
    mbpf::frontend::Expr *expr;
}

%token <sval> IDENT
%token <ival> INTEGER
%token <bval> BOOLEAN
%token EQ NE GE LE AND OR

%type <expr> expr or_expr and_expr unary_expr primary comparison operand

%destructor { free($$); } <sval>

%%

input:
    expr { set_parse_root($1); }
;

expr:
    or_expr { $$ = $1; }
;

or_expr:
    and_expr { $$ = $1; }
    | or_expr OR and_expr { $$ = make_binary(ExprKind::kOr, $1, $3); }
;

and_expr:
    unary_expr { $$ = $1; }
    | and_expr AND unary_expr { $$ = make_binary(ExprKind::kAnd, $1, $3); }
;

unary_expr:
    primary { $$ = $1; }
    | '!' unary_expr { $$ = make_unary(ExprKind::kNot, $2); }
;

primary:
    '(' expr ')' { $$ = $2; }
    | comparison { $$ = $1; }
    | operand { $$ = $1; }
;

comparison:
    operand EQ operand { $$ = make_binary(ExprKind::kEq, $1, $3); }
    | operand NE operand { $$ = make_binary(ExprKind::kNe, $1, $3); }
    | operand '>' operand { $$ = make_binary(ExprKind::kGt, $1, $3); }
    | operand '<' operand { $$ = make_binary(ExprKind::kLt, $1, $3); }
    | operand GE operand { $$ = make_binary(ExprKind::kGe, $1, $3); }
    | operand LE operand { $$ = make_binary(ExprKind::kLe, $1, $3); }
;

operand:
    IDENT {
        $$ = make_ident($1);
        free($1);
    }
    | INTEGER {
        $$ = make_integer($1);
    }
    | BOOLEAN {
        $$ = make_boolean($1 != 0);
    }
;

%%

void yyerror(const char *s) {
    set_parse_error(s);
}
