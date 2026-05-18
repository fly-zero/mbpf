#pragma once

#include <new>
#include <string>
#include <type_traits>

namespace mbpf::frontend {

enum class ExprKind
{
    kIdentifier,
    kInteger,
    kBoolean,
    kIpv4,
    kIpv6,
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

    explicit Expr(ExprKind expr_kind) : kind_(expr_kind), left_(nullptr), right_(nullptr)
    {
    }

    Expr(ExprKind expr_kind, const std::string &value) : Expr(expr_kind)
    {
        new (&payload_) std::string(value);
    }

    Expr(ExprKind expr_kind, long long value) : Expr(expr_kind)
    {
        new (&payload_) long long(value);
    }

    Expr(ExprKind expr_kind, bool value) : Expr(expr_kind)
    {
        new (&payload_) bool(value);
    }

    ~Expr()
    {
        if (kind_ == ExprKind::kIdentifier || kind_ == ExprKind::kIpv4 ||
            kind_ == ExprKind::kIpv6) {
            string_payload().~basic_string();
        }

        delete left_;
        delete right_;
    }

    const std::string &ident() const
    {
        return string_payload();
    }

    const std::string &text_value() const
    {
        return string_payload();
    }

    long long int_value() const
    {
        return scalar_payload<long long>();
    }

    bool bool_value() const
    {
        return scalar_payload<bool>();
    }

    ExprKind kind_;
    Expr    *left_;
    Expr    *right_;

private:
    using PayloadStorage = std::aligned_union_t<0, std::string, long long, bool>;

    const std::string &string_payload() const
    {
        return *reinterpret_cast<const std::string *>(&payload_);
    }

    std::string &string_payload()
    {
        return *reinterpret_cast<std::string *>(&payload_);
    }

    template <typename T>
    const T &scalar_payload() const
    {
        return *reinterpret_cast<const T *>(&payload_);
    }

    PayloadStorage payload_;
};

Expr *make_binary(ExprKind kind, Expr *lhs, Expr *rhs);

Expr *make_unary(ExprKind kind, Expr *operand);

Expr *make_ident(const std::string &name);

Expr *make_integer(long long value);

Expr *make_boolean(bool value);

Expr *make_ipv4(const std::string &value);

Expr *make_ipv6(const std::string &value);

}  // namespace mbpf::frontend
