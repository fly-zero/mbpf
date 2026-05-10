#include "codegen/compiler.hpp"

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace mbpf {
namespace {

enum class ValueType : uint8_t
{
    kBool,
    kInt
};

struct EmitValue
{
    uint8_t   reg_;
    ValueType type_;
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

private:
    EmitValue emit_expr(const frontend::Expr *expr)
    {
        if (!expr) {
            fail("empty expression node");
            return {0, ValueType::kBool};
        }

        switch (expr->kind_) {
        case frontend::ExprKind::kIdentifier:
            return emit_identifier(expr->ident_);
        case frontend::ExprKind::kInteger:
            return emit_immediate(expr->int_value_, ValueType::kInt);
        case frontend::ExprKind::kBoolean:
            return emit_immediate(expr->bool_value_ ? 1 : 0, ValueType::kBool);
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

    EmitValue emit_identifier(const std::string &name)
    {
        const QualifierInfo *info = registry_.find(name);
        if (!info) {
            fail("qualifier not found: " + name);
            return {0, ValueType::kInt};
        }

        auto const dst = alloc_reg();
        if (!ok_) {
            return {0, ValueType::kInt};
        }

        // c_ = 1 if signed, 0 if unsigned/bool
        uint8_t sign_flag = is_signed_type(info->type_) ? 1 : 0;
        emit(OpCode::kLoadField,
             dst,
             static_cast<int>(info->size_),
             sign_flag,
             static_cast<int64_t>(info->offset_));
        return {dst, is_bool_type(info->type_) ? ValueType::kBool : ValueType::kInt};
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

    EmitValue emit_compare(OpCode                op,
                           const frontend::Expr *lhs_expr,
                           const frontend::Expr *rhs_expr)
    {
        EmitValue lhs = emit_expr(lhs_expr);
        EmitValue rhs = emit_expr(rhs_expr);
        if (!ok_) {
            return {0, ValueType::kBool};
        }

        if (lhs.type_ == ValueType::kBool && rhs.type_ == ValueType::kInt) {
            rhs = ensure_bool(rhs);
        } else if (lhs.type_ == ValueType::kInt && rhs.type_ == ValueType::kBool) {
            lhs = ensure_bool(lhs);
        }

        auto const dst = alloc_reg();
        emit(op, dst, lhs.reg_, rhs.reg_, 0);
        release_reg(lhs.reg_);
        release_reg(rhs.reg_);
        return {dst, ValueType::kBool};
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
        if (!ok_) {
            return;
        }

        ok_     = false;
        *error_ = message;
    }

    const QualifierRegistry       &registry_;
    Program                       *program_;
    std::string                   *error_;
    int                            next_reg_ = 0;
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
        if (out_error->find("qualifier not found") == 0) {
            return MBPF_ERR_QUALIFIER_NOT_FOUND;
        }

        return MBPF_ERR_CODEGEN;
    }

    return MBPF_OK;
}

}  // namespace mbpf
