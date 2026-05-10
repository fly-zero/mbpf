%option noyywrap noinput nounput

%{
#include <cstdlib>
#include <cstring>

#include "expression_parser.hpp"
%}

%%

[ \t\r\n]+                    ;
"true"                        { yylval.bval = 1; return BOOLEAN; }
"false"                       { yylval.bval = 0; return BOOLEAN; }
"=="                          { return EQ; }
"!="                          { return NE; }
">="                          { return GE; }
"<="                          { return LE; }
"&&"                          { return AND; }
"||"                          { return OR; }
"&"                           { return BIT_AND; }
"|"                           { return BIT_OR; }
[0-9]+                        { yylval.ival = atoll(yytext); return INTEGER; }
"0x"[0-9a-fA-F]+              { yylval.ival = strtoll(yytext, nullptr, 16); return INTEGER; }
[a-zA-Z_][a-zA-Z0-9_]*        { yylval.sval = strdup(yytext); return IDENT; }
[()!><]                       { return yytext[0]; }
.                             { return yytext[0]; }

%%
