#pragma once

#include <memory>
#include <string>

#include "frontend/ast.hpp"

namespace mbpf::frontend {

struct ParseResult
{
    std::unique_ptr<Expr> root_;
    std::string           error_;
};

ParseResult parse_expression(const std::string &expression);

void set_parse_error(const char *message);

void set_parse_root(Expr *root);

}  // namespace mbpf::frontend
