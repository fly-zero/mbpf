#include "frontend/parser_driver.hpp"

#include <string>

int yyparse(void);

typedef struct yy_buffer_state *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *str);

void yy_delete_buffer(YY_BUFFER_STATE buffer);

int yylex_destroy(void);

namespace mbpf::frontend {
namespace {
thread_local ParseResult *g_result = nullptr;
}

ParseResult parse_expression(const std::string &expression)
{
    ParseResult result;
    g_result = &result;

    YY_BUFFER_STATE buffer       = yy_scan_string(expression.c_str());
    const int       parse_status = yyparse();
    yy_delete_buffer(buffer);
    yylex_destroy();

    if (parse_status != 0 && result.error_.empty()) {
        result.error_ = "parse failed";
    }

    g_result = nullptr;
    return result;
}

void set_parse_error(const char *message)
{
    if (!g_result) {
        return;
    }

    if (g_result->error_.empty()) {
        g_result->error_ = message ? message : "parse error";
    }
}

void set_parse_root(Expr *root)
{
    if (!g_result) {
        delete root;
        return;
    }

    g_result->root_.reset(root);
}

}  // namespace mbpf::frontend
