#pragma once

#include <string>

#include "codegen/bytecode.hpp"
#include "frontend/ast.hpp"
#include "registry/qualifier_registry.hpp"

namespace mbpf {

int compile_ast_to_program(const frontend::Expr    *root,
                           const QualifierRegistry &registry,
                           Program                 *out_program,
                           std::string             *out_error);

}  // namespace mbpf
