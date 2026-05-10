#include "frontend/ast.hpp"

namespace mbpf::frontend {

Expr *make_binary(ExprKind kind, Expr *lhs, Expr *rhs)
{
    auto *expr   = new Expr(kind);
    expr->left_  = lhs;
    expr->right_ = rhs;
    return expr;
}

Expr *make_unary(ExprKind kind, Expr *operand)
{
    auto *expr  = new Expr(kind);
    expr->left_ = operand;
    return expr;
}

Expr *make_ident(const std::string &name)
{
    auto *expr   = new Expr(ExprKind::kIdentifier);
    expr->ident_ = name;
    return expr;
}

Expr *make_integer(long long value)
{
    auto *expr       = new Expr(ExprKind::kInteger);
    expr->int_value_ = value;
    return expr;
}

Expr *make_boolean(bool value)
{
    auto *expr        = new Expr(ExprKind::kBoolean);
    expr->bool_value_ = value;
    return expr;
}

}  // namespace mbpf::frontend
