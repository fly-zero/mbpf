#include "codegen/compiler.hpp"

#include <arpa/inet.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mbpf {
namespace {

enum class ValueType : uint8_t
{
    kBool,
    kInt,
    kIpv4,
    kIpv6
};

struct EmitValue
{
    uint8_t   reg_;
    ValueType type_;
};

struct EmitIpv6Value
{
    uint8_t hi_reg_;
    uint8_t lo_reg_;
};

class Compiler
{
public:
    Compiler(const QualifierRegistry &registry, Program *program, std::string *error)
        : registry_(registry), program_(program), error_(error)
    {
    }

    bool compile(const frontend::Expr *root)
    {
        const EmitValue result = emit_expr(root);
        if (!ok_) {
            return false;
        }

        const EmitValue bool_result = ensure_bool(result);
        if (!ok_) {
            return false;
        }

        emit(OpCode::kRet, bool_result.reg_, 0, 0, 0);
        release_reg(bool_result.reg_);
        program_->register_count_ = next_reg_;
        return true;
    }

    int status() const
    {
        return status_;
    }

private:
    EmitValue emit_expr(const frontend::Expr *expr)
    {
        if (!expr) {
            fail("empty expression node");
            return {0, ValueType::kBool};
        }

        switch (expr->kind_) {
        case frontend::ExprKind::kIdentifier:
            return emit_identifier(expr->ident());
        case frontend::ExprKind::kInteger:
            return emit_immediate(expr->int_value(), ValueType::kInt);
        case frontend::ExprKind::kBoolean:
            return emit_immediate(expr->bool_value() ? 1 : 0, ValueType::kBool);
        case frontend::ExprKind::kIpv4:
            return emit_ipv4(expr->text_value());
        case frontend::ExprKind::kIpv6:
            fail(MBPF_ERR_TYPE_MISMATCH, "ipv6 values are only supported in == comparisons");
            return {0, ValueType::kIpv6};
        case frontend::ExprKind::kNot:
            return emit_not(expr->left_);
        case frontend::ExprKind::kAnd:
            return emit_logical_and(expr->left_, expr->right_);
        case frontend::ExprKind::kOr:
            return emit_logical_or(expr->left_, expr->right_);
        case frontend::ExprKind::kEq:
            return emit_compare(OpCode::kCmpEq, expr->left_, expr->right_);
        case frontend::ExprKind::kNe:
            return emit_compare(OpCode::kCmpNe, expr->left_, expr->right_);
        case frontend::ExprKind::kGt:
            return emit_compare(OpCode::kCmpGt, expr->left_, expr->right_);
        case frontend::ExprKind::kLt:
            return emit_compare(OpCode::kCmpLt, expr->left_, expr->right_);
        case frontend::ExprKind::kGe:
            return emit_compare(OpCode::kCmpGe, expr->left_, expr->right_);
        case frontend::ExprKind::kLe:
            return emit_compare(OpCode::kCmpLe, expr->left_, expr->right_);
        case frontend::ExprKind::kBitAnd:
            return emit_bitwise(OpCode::kAnd, expr->left_, expr->right_);
        case frontend::ExprKind::kBitOr:
            return emit_bitwise(OpCode::kOr, expr->left_, expr->right_);
        default:
            fail("unsupported expression kind");
            return {0, ValueType::kBool};
        }
    }

    ValueType qualifier_value_type(mbpf_type_t type) const
    {
        switch (type) {
        case MBPF_TYPE_BOOL:
            return ValueType::kBool;
        case MBPF_TYPE_IPV4:
            return ValueType::kIpv4;
        case MBPF_TYPE_IPV6:
            return ValueType::kIpv6;
        default:
            return ValueType::kInt;
        }
    }

    bool is_ip_type(ValueType type) const
    {
        return type == ValueType::kIpv4 || type == ValueType::kIpv6;
    }

    ValueType resolve_expr_type(const frontend::Expr *expr)
    {
        if (!expr) {
            fail("empty expression node");
            return ValueType::kBool;
        }

        switch (expr->kind_) {
        case frontend::ExprKind::kIdentifier: {
            const QualifierInfo *info = registry_.find(expr->ident());
            if (!info) {
                fail(MBPF_ERR_QUALIFIER_NOT_FOUND, "qualifier not found: " + expr->ident());
                return ValueType::kInt;
            }

            return qualifier_value_type(info->type_);
        }
        case frontend::ExprKind::kInteger:
            return ValueType::kInt;
        case frontend::ExprKind::kBoolean:
            return ValueType::kBool;
        case frontend::ExprKind::kIpv4:
            return ValueType::kIpv4;
        case frontend::ExprKind::kIpv6:
            return ValueType::kIpv6;
        case frontend::ExprKind::kNot:
        case frontend::ExprKind::kAnd:
        case frontend::ExprKind::kOr:
        case frontend::ExprKind::kEq:
        case frontend::ExprKind::kNe:
        case frontend::ExprKind::kGt:
        case frontend::ExprKind::kLt:
        case frontend::ExprKind::kGe:
        case frontend::ExprKind::kLe:
            return ValueType::kBool;
        case frontend::ExprKind::kBitAnd:
        case frontend::ExprKind::kBitOr:
            return ValueType::kInt;
        default:
            fail("unsupported expression kind");
            return ValueType::kBool;
        }
    }

    uint8_t emit_load_field_reg(uint32_t offset, uint8_t size, uint8_t sign_flag)
    {
        auto const dst = alloc_reg();
        if (!ok_) {
            return 0;
        }

        emit(OpCode::kLoadField, dst, size, sign_flag, static_cast<int64_t>(offset));
        return dst;
    }

    EmitValue emit_identifier(const std::string &name)
    {
        const QualifierInfo *info = registry_.find(name);
        if (!info) {
            fail(MBPF_ERR_QUALIFIER_NOT_FOUND, "qualifier not found: " + name);
            return {0, ValueType::kInt};
        }

        ValueType value_type = qualifier_value_type(info->type_);
        if (value_type == ValueType::kIpv6) {
            fail(MBPF_ERR_TYPE_MISMATCH, "ipv6 values are only supported in == comparisons");
            return {0, ValueType::kIpv6};
        }

        uint8_t    sign_flag = is_signed_type(info->type_) ? 1 : 0;
        auto const dst       = emit_load_field_reg(info->offset_, info->size_, sign_flag);
        return {dst, value_type};
    }

    EmitValue emit_immediate(int64_t value, ValueType type)
    {
        auto const dst = alloc_reg();
        if (!ok_) {
            return {0, type};
        }

        emit(OpCode::kLoadImm, dst, 0, 0, value);
        return {dst, type};
    }

    EmitValue emit_ipv4(const std::string &value)
    {
        uint32_t raw = 0;
        if (inet_pton(AF_INET, value.c_str(), &raw) != 1) {
            fail(MBPF_ERR_PARSE, "invalid ipv4 literal: " + value);
            return {0, ValueType::kIpv4};
        }

        return emit_immediate(static_cast<int64_t>(raw), ValueType::kIpv4);
    }

    EmitIpv6Value emit_ipv6_literal(const std::string &value)
    {
        std::array<uint8_t, 16> bytes = {};
        if (inet_pton(AF_INET6, value.c_str(), bytes.data()) != 1) {
            fail(MBPF_ERR_PARSE, "invalid ipv6 literal: " + value);
            return {0, 0};
        }

        uint64_t hi = 0;
        uint64_t lo = 0;
        std::memcpy(&hi, bytes.data(), sizeof(hi));
        std::memcpy(&lo, bytes.data() + sizeof(hi), sizeof(lo));

        EmitValue hi_value = emit_immediate(static_cast<int64_t>(hi), ValueType::kInt);
        EmitValue lo_value = emit_immediate(static_cast<int64_t>(lo), ValueType::kInt);
        return {hi_value.reg_, lo_value.reg_};
    }

    EmitIpv6Value emit_ipv6_identifier(const std::string &name)
    {
        const QualifierInfo *info = registry_.find(name);
        if (!info) {
            fail(MBPF_ERR_QUALIFIER_NOT_FOUND, "qualifier not found: " + name);
            return {0, 0};
        }

        if (info->type_ != MBPF_TYPE_IPV6) {
            fail(MBPF_ERR_TYPE_MISMATCH, "ipv6 comparisons require ipv6 qualifiers");
            return {0, 0};
        }

        return {emit_load_field_reg(info->offset_, 8, 0),
                emit_load_field_reg(info->offset_ + 8, 8, 0)};
    }

    EmitIpv6Value emit_ipv6_value(const frontend::Expr *expr)
    {
        if (!expr) {
            fail("empty expression node");
            return {0, 0};
        }

        switch (expr->kind_) {
        case frontend::ExprKind::kIdentifier:
            return emit_ipv6_identifier(expr->ident());
        case frontend::ExprKind::kIpv6:
            return emit_ipv6_literal(expr->text_value());
        default:
            fail(MBPF_ERR_TYPE_MISMATCH, "ipv6 equality requires ipv6 operands");
            return {0, 0};
        }
    }

    EmitValue emit_compare_regs(OpCode op, EmitValue lhs, EmitValue rhs)
    {
        auto const dst = alloc_reg();
        emit(op, dst, lhs.reg_, rhs.reg_, 0);
        release_reg(lhs.reg_);
        release_reg(rhs.reg_);
        return {dst, ValueType::kBool};
    }

    EmitValue emit_ipv6_compare(OpCode                op,
                                const frontend::Expr *lhs_expr,
                                const frontend::Expr *rhs_expr)
    {
        EmitIpv6Value lhs = emit_ipv6_value(lhs_expr);
        EmitIpv6Value rhs = emit_ipv6_value(rhs_expr);
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        EmitValue hi_equal = emit_compare_regs(
            OpCode::kCmpEq, {lhs.hi_reg_, ValueType::kInt}, {rhs.hi_reg_, ValueType::kInt});
        EmitValue lo_equal = emit_compare_regs(
            OpCode::kCmpEq, {lhs.lo_reg_, ValueType::kInt}, {rhs.lo_reg_, ValueType::kInt});
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        auto const dst = alloc_reg();
        emit(OpCode::kAnd, dst, hi_equal.reg_, lo_equal.reg_, 0);
        release_reg(hi_equal.reg_);
        release_reg(lo_equal.reg_);

        if (op == OpCode::kCmpEq) {
            return {dst, ValueType::kBool};
        }

        auto const not_dst = alloc_reg();
        emit(OpCode::kNot, not_dst, dst, 0, 0);
        release_reg(dst);
        return {not_dst, ValueType::kBool};
    }

    EmitValue emit_compare(OpCode                op,
                           const frontend::Expr *lhs_expr,
                           const frontend::Expr *rhs_expr)
    {
        ValueType lhs_type = resolve_expr_type(lhs_expr);
        ValueType rhs_type = resolve_expr_type(rhs_expr);
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        if (is_ip_type(lhs_type) || is_ip_type(rhs_type)) {
            if (lhs_type != rhs_type) {
                fail(MBPF_ERR_TYPE_MISMATCH, "ip comparison operands must have the same type");
                return {0, ValueType::kBool};
            }

            if (op != OpCode::kCmpEq && op != OpCode::kCmpNe) {
                fail(MBPF_ERR_TYPE_MISMATCH, "ip values only support == and != comparisons");
                return {0, ValueType::kBool};
            }

            if (lhs_type == ValueType::kIpv6) {
                return emit_ipv6_compare(op, lhs_expr, rhs_expr);
            }
        }

        EmitValue lhs = emit_expr(lhs_expr);
        EmitValue rhs = emit_expr(rhs_expr);
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        if (lhs.type_ == ValueType::kBool && rhs.type_ == ValueType::kInt) {
            rhs = ensure_bool(rhs);
        } else if (lhs.type_ == ValueType::kInt && rhs.type_ == ValueType::kBool) {
            lhs = ensure_bool(lhs);
        } else if (lhs.type_ != rhs.type_) {
            fail(MBPF_ERR_TYPE_MISMATCH, "comparison operands must have compatible types");
            return {0, ValueType::kBool};
        }

        return emit_compare_regs(op, lhs, rhs);
    }

    EmitValue emit_bitwise(OpCode                op,
                           const frontend::Expr *lhs_expr,
                           const frontend::Expr *rhs_expr)
    {
        EmitValue lhs = emit_expr(lhs_expr);
        EmitValue rhs = emit_expr(rhs_expr);
        if (!ok_) {
            return {0, ValueType::kInt};
        }

        if (lhs.type_ != ValueType::kInt || rhs.type_ != ValueType::kInt) {
            fail(MBPF_ERR_TYPE_MISMATCH, "bitwise operators require integer operands");
            return {0, ValueType::kInt};
        }

        auto const dst = alloc_reg();
        emit(op, dst, lhs.reg_, rhs.reg_, 0);
        release_reg(lhs.reg_);
        release_reg(rhs.reg_);
        return {dst, ValueType::kInt};
    }

    EmitValue emit_not(const frontend::Expr *operand_expr)
    {
        EmitValue operand = ensure_bool(emit_expr(operand_expr));
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        auto const dst = alloc_reg();
        emit(OpCode::kNot, dst, operand.reg_, 0, 0);
        release_reg(operand.reg_);
        return {dst, ValueType::kBool};
    }

    EmitValue emit_logical_and(const frontend::Expr *lhs_expr, const frontend::Expr *rhs_expr)
    {
        EmitValue lhs = ensure_bool(emit_expr(lhs_expr));
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        auto const dst          = alloc_reg();
        const auto jmp_false_ip = emit(OpCode::kJumpIfFalse, lhs.reg_, 0, 0, -1);
        release_reg(lhs.reg_);

        EmitValue rhs = ensure_bool(emit_expr(rhs_expr));
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        emit(OpCode::kMove, dst, rhs.reg_, 0, 0);
        release_reg(rhs.reg_);
        const auto jmp_end_ip = emit(OpCode::kJump, 0, 0, 0, -1);

        patch_jump(jmp_false_ip, static_cast<int64_t>(program_->instructions_.size()));
        emit(OpCode::kLoadImm, dst, 0, 0, 0);
        patch_jump(jmp_end_ip, static_cast<int64_t>(program_->instructions_.size()));

        return {dst, ValueType::kBool};
    }

    EmitValue emit_logical_or(const frontend::Expr *lhs_expr, const frontend::Expr *rhs_expr)
    {
        EmitValue lhs = ensure_bool(emit_expr(lhs_expr));
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        auto const dst        = alloc_reg();
        const auto jmp_rhs_ip = emit(OpCode::kJumpIfFalse, lhs.reg_, 0, 0, -1);
        release_reg(lhs.reg_);

        emit(OpCode::kLoadImm, dst, 0, 0, 1);
        const auto jmp_end_ip = emit(OpCode::kJump, 0, 0, 0, -1);

        patch_jump(jmp_rhs_ip, static_cast<int64_t>(program_->instructions_.size()));
        EmitValue rhs = ensure_bool(emit_expr(rhs_expr));
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        emit(OpCode::kMove, dst, rhs.reg_, 0, 0);
        release_reg(rhs.reg_);
        patch_jump(jmp_end_ip, static_cast<int64_t>(program_->instructions_.size()));

        return {dst, ValueType::kBool};
    }

    EmitValue ensure_bool(EmitValue value)
    {
        if (value.type_ == ValueType::kBool) {
            return value;
        }

        if (value.type_ != ValueType::kInt) {
            fail(MBPF_ERR_TYPE_MISMATCH, "logical operators require boolean or integer operands");
            return {0, ValueType::kBool};
        }

        EmitValue  zero = emit_immediate(0, ValueType::kInt);
        auto const dst  = alloc_reg();
        emit(OpCode::kCmpNe, dst, value.reg_, zero.reg_, 0);
        release_reg(value.reg_);
        release_reg(zero.reg_);
        return {dst, ValueType::kBool};
    }

    uint8_t alloc_reg()
    {
        if (!free_regs_.empty()) {
            const auto reg = free_regs_.back();
            free_regs_.pop_back();
            reg_used_bitset_.set(reg);
            return reg;
        }

        if (next_reg_ >= kMaxRegisterCount) {
            fail("register allocation failed: register limit exceeded");
            return 0;
        }

        const auto reg = next_reg_++;
        reg_used_bitset_.set(reg);
        return reg;
    }

    void release_reg(uint8_t reg)
    {
        if (reg >= next_reg_) {
            fail("internal codegen register release failed");
            return;
        }

        const size_t index = static_cast<size_t>(reg);
        if (!reg_used_bitset_.test(index)) {
            fail("internal codegen double register release");
            return;
        }

        reg_used_bitset_.reset(index);
        free_regs_.push_back(reg);
    }

    int emit(OpCode op, uint8_t a, uint8_t b, uint8_t c, int64_t imm)
    {
        if (!ok_) {
            return -1;
        }

        if (op == OpCode::kLoadImm) {
            const auto wide = make_load_imm(a, imm);
            program_->instructions_.push_back(wide.first);
            program_->instructions_.push_back(wide.second);
            return static_cast<int>(program_->instructions_.size()) - 2;
        }

        if (!fits_i32(imm)) {
            fail("instruction auxiliary payload out of 32-bit range");
            return -1;
        }

        program_->instructions_.push_back(Instruction{op, a, b, c, static_cast<int32_t>(imm)});
        return static_cast<int>(program_->instructions_.size()) - 1;
    }

    void patch_jump(int index, int64_t target)
    {
        if (index < 0 || static_cast<size_t>(index) >= program_->instructions_.size()) {
            fail("internal codegen jump patch failed");
            return;
        }

        if (!fits_i32(target)) {
            fail("jump target out of 32-bit range");
            return;
        }

        Instruction &instruction = program_->instructions_[static_cast<size_t>(index)];
        instruction              = Instruction(instruction.op(),
                                  instruction.a(),
                                  instruction.b(),
                                  instruction.c(),
                                  static_cast<int32_t>(target));
    }

    void fail(const std::string &message)
    {
        fail(MBPF_ERR_CODEGEN, message);
    }

    void fail(int status, const std::string &message)
    {
        if (!ok_) {
            return;
        }

        ok_     = false;
        status_ = status;
        *error_ = message;
    }

    const QualifierRegistry       &registry_;
    Program                       *program_;
    std::string                   *error_;
    int                            next_reg_ = 0;
    int                            status_   = MBPF_ERR_CODEGEN;
    bool                           ok_       = true;
    std::vector<uint8_t>           free_regs_;
    std::bitset<kMaxRegisterCount> reg_used_bitset_;
};

}  // namespace

int compile_ast_to_program(const frontend::Expr    *root,
                           const QualifierRegistry &registry,
                           Program                 *out_program,
                           std::string             *out_error)
{
    if (!root || !out_program || !out_error) {
        return MBPF_ERR_INVALID_ARG;
    }

    out_program->instructions_.clear();
    out_program->register_count_ = 0;
    out_error->clear();

    Compiler compiler(registry, out_program, out_error);
    if (!compiler.compile(root)) {
        return compiler.status();
    }

    return MBPF_OK;
}

}  // namespace mbpf
