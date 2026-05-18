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
    return new Expr(ExprKind::kIdentifier, name);
}

Expr *make_integer(long long value)
{
    return new Expr(ExprKind::kInteger, value);
}

Expr *make_boolean(bool value)
{
    return new Expr(ExprKind::kBoolean, value);
}

Expr *make_ipv4(const std::string &value)
{
    return new Expr(ExprKind::kIpv4, value);
}

Expr *make_ipv6(const std::string &value)
{
    return new Expr(ExprKind::kIpv6, value);
}

}  // namespace mbpf::frontend
