#pragma once

#include <string>

namespace mbpf::frontend {

enum class ExprKind
{
    kIdentifier,
    kInteger,
    kBoolean,
    kNot,
    kAnd,
    kOr,
    kEq,
    kNe,
    kGt,
    kLt,
    kGe,
    kLe,
    kBitAnd,
    kBitOr
};

struct Expr
{
    Expr(const Expr &)           = delete;
    Expr(Expr &&)                = delete;
    void operator=(const Expr &) = delete;
    void operator=(Expr &&)      = delete;

    explicit Expr(ExprKind expr_kind)
        : kind_(expr_kind), left_(nullptr), right_(nullptr), int_value_(0), bool_value_(false)
    {
    }

    ~Expr()
    {
        delete left_;
        delete right_;
    }

    ExprKind    kind_;
    Expr       *left_;
    Expr       *right_;
    std::string ident_;
    long long   int_value_;
    bool        bool_value_;
};

Expr *make_binary(ExprKind kind, Expr *lhs, Expr *rhs);

Expr *make_unary(ExprKind kind, Expr *operand);

Expr *make_ident(const std::string &name);

Expr *make_integer(long long value);

Expr *make_boolean(bool value);

}  // namespace mbpf::frontend
