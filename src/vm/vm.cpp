#include "vm/vm.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#include "common/error.hpp"
#include "registry/qualifier_registry.hpp"

namespace mbpf {
namespace {

struct ProgramView
{
    const Instruction *instructions_      = nullptr;
    size_t             instruction_count_ = 0;
    int                register_count_    = 0;
};

int64_t sign_extend(uint64_t value, uint8_t size)
{
    switch (size) {
    case 1:
        return static_cast<int64_t>(static_cast<int8_t>(value));
    case 2:
        return static_cast<int64_t>(static_cast<int16_t>(value));
    case 4:
        return static_cast<int64_t>(static_cast<int32_t>(value));
    case 8:
        return static_cast<int64_t>(value);
    default:
        return 0;
    }
}

int64_t load_field(const void *ctx, uint32_t offset, uint8_t size, mbpf_type_t type)
{
    uint64_t raw = 0;
    std::memcpy(&raw, static_cast<const uint8_t *>(ctx) + offset, size);
    if (is_signed_type(type)) {
        return sign_extend(raw, size);
    }

    return static_cast<int64_t>(raw);
}

bool verify_program_impl(const ProgramView &program)
{
    const int reg_cnt = program.register_count_;
    if (reg_cnt < 0 || reg_cnt > kMaxRegisterCount) {
        return false;
    }

    if (!program.instructions_ || program.instruction_count_ == 0) {
        return false;
    }

    std::vector<bool> instruction_starts(program.instruction_count_ + 1, false);
    instruction_starts[0] = true;

    std::vector<int32_t> jump_targets;
    size_t               i = 0;
    while (i < program.instruction_count_) {
        const Instruction &ins = program.instructions_[i];
        instruction_starts[i]  = true;
        // 检查寄存器索引范围
        auto reg_ok = [&](uint8_t r) { return static_cast<int>(r) < reg_cnt; };
        switch (ins.op()) {
        case OpCode::kLoadField:
            if (!reg_ok(ins.a()))
                return false;
            if (!(ins.b() == 1 || ins.b() == 2 || ins.b() == 4 || ins.b() == 8))
                return false;
            if (ins.c() > 1)  // only 0 or 1 allowed
                return false;
            ++i;
            break;
        case OpCode::kLoadImm:
            if (!reg_ok(ins.a()))
                return false;
            if (i + 1 >= program.instruction_count_)
                return false;
            i += 2;
            break;
        case OpCode::kMove:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b()))
                return false;
            ++i;
            break;
        case OpCode::kCmpEq:
        case OpCode::kCmpNe:
        case OpCode::kCmpGt:
        case OpCode::kCmpLt:
        case OpCode::kCmpGe:
        case OpCode::kCmpLe:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b()) || !reg_ok(ins.c()))
                return false;
            ++i;
            break;
        case OpCode::kNot:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b()))
                return false;
            ++i;
            break;
        case OpCode::kJumpIfFalse:
            if (!reg_ok(ins.a()))
                return false;
            if (ins.aux_i32() < 0 ||
                static_cast<size_t>(ins.aux_i32()) > program.instruction_count_)
                return false;
            jump_targets.push_back(ins.aux_i32());
            ++i;
            break;
        case OpCode::kJump:
            if (ins.aux_i32() < 0 ||
                static_cast<size_t>(ins.aux_i32()) > program.instruction_count_)
                return false;
            jump_targets.push_back(ins.aux_i32());
            ++i;
            break;
        case OpCode::kRet:
            if (!reg_ok(ins.a()))
                return false;
            ++i;
            break;
        default:
            return false;
        }
    }

    instruction_starts[program.instruction_count_] = true;
    for (int32_t target : jump_targets) {
        if (!instruction_starts[static_cast<size_t>(target)]) {
            return false;
        }
    }

    return true;
}

int execute_program_impl(const Instruction *instructions,
                         size_t             instruction_count,
                         int                register_count,
                         const void        *ctx,
                         bool              *out_result,
                         bool               run_verify)
{
    if (!ctx || !out_result) {
        set_last_error("invalid execute arguments");
        return MBPF_ERR_INVALID_ARG;
    }

    if (run_verify) {
        const int verify_status = verify_program(instructions, instruction_count, register_count);
        if (verify_status != MBPF_OK) {
            return verify_status;
        }
    }

    ProgramView view;
    view.instructions_      = instructions;
    view.instruction_count_ = instruction_count;
    view.register_count_    = register_count;

    const int reg_cnt                 = view.register_count_;
    int64_t   regs[kMaxRegisterCount] = {0};
    size_t    pc                      = 0;

    auto reg_ok = [reg_cnt](uint8_t r) { return static_cast<int>(r) < reg_cnt; };

    while (pc < view.instruction_count_) {
        const Instruction &ins = view.instructions_[pc];
        switch (ins.op()) {
        case OpCode::kLoadField:
            if (!reg_ok(ins.a()) ||
                !(ins.b() == 1 || ins.b() == 2 || ins.b() == 4 || ins.b() == 8) || ins.c() > 1) {
                set_last_error("invalid LoadField at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }
            {
                // c_==1: signed, c_==0: unsigned/bool
                bool     is_signed = (ins.c() == 1);
                uint64_t raw       = 0;
                std::memcpy(&raw, static_cast<const uint8_t *>(ctx) + ins.aux_u32(), ins.b());
                int64_t val = is_signed ? sign_extend(raw, ins.b()) : static_cast<int64_t>(raw);
                regs[static_cast<size_t>(ins.a())] = val;
            }
            ++pc;
            break;
        case OpCode::kLoadImm:
            if (!reg_ok(ins.a()) || pc + 1 >= view.instruction_count_) {
                set_last_error("invalid LoadImm at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            regs[static_cast<size_t>(ins.a())] = view.instructions_[pc + 1].raw_wide_data();
            pc += 2;
            break;
        case OpCode::kMove:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b())) {
                set_last_error("invalid Move at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            regs[static_cast<size_t>(ins.a())] = regs[static_cast<size_t>(ins.b())];
            ++pc;
            break;
        case OpCode::kCmpEq:
        case OpCode::kCmpNe:
        case OpCode::kCmpGt:
        case OpCode::kCmpLt:
        case OpCode::kCmpGe:
        case OpCode::kCmpLe:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b()) || !reg_ok(ins.c())) {
                set_last_error("invalid Cmp at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            switch (ins.op()) {
            case OpCode::kCmpEq:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] == regs[static_cast<size_t>(ins.c())];
                break;
            case OpCode::kCmpNe:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] != regs[static_cast<size_t>(ins.c())];
                break;
            case OpCode::kCmpGt:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] > regs[static_cast<size_t>(ins.c())];
                break;
            case OpCode::kCmpLt:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] < regs[static_cast<size_t>(ins.c())];
                break;
            case OpCode::kCmpGe:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] >= regs[static_cast<size_t>(ins.c())];
                break;
            case OpCode::kCmpLe:
                regs[static_cast<size_t>(ins.a())] =
                    regs[static_cast<size_t>(ins.b())] <= regs[static_cast<size_t>(ins.c())];
                break;
            default:
                break;
            }

            ++pc;
            break;
        case OpCode::kNot:
            if (!reg_ok(ins.a()) || !reg_ok(ins.b())) {
                set_last_error("invalid Not at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            regs[static_cast<size_t>(ins.a())] = !regs[static_cast<size_t>(ins.b())];
            ++pc;
            break;
        case OpCode::kJumpIfFalse:
            if (!reg_ok(ins.a()) || ins.aux_i32() < 0 ||
                static_cast<size_t>(ins.aux_i32()) > view.instruction_count_) {
                set_last_error("invalid JumpIfFalse at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            if (!regs[static_cast<size_t>(ins.a())]) {
                pc = static_cast<size_t>(ins.aux_i32());
            } else {
                ++pc;
            }

            break;
        case OpCode::kJump:
            if (ins.aux_i32() < 0 || static_cast<size_t>(ins.aux_i32()) > view.instruction_count_) {
                set_last_error("invalid Jump at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            pc = static_cast<size_t>(ins.aux_i32());
            break;
        case OpCode::kRet:
            if (!reg_ok(ins.a())) {
                set_last_error("invalid Ret at runtime");
                return MBPF_ERR_VM_RUNTIME;
            }

            *out_result = regs[static_cast<size_t>(ins.a())] != 0;
            return MBPF_OK;
        default:
            set_last_error("invalid opcode at runtime");
            return MBPF_ERR_VM_RUNTIME;
        }
    }

    set_last_error("program terminated without RET");
    return MBPF_ERR_VM_RUNTIME;
}

}  // namespace

int verify_program(const Instruction *instructions, size_t instruction_count, int register_count)
{
    ProgramView view;
    view.instructions_      = instructions;
    view.instruction_count_ = instruction_count;
    view.register_count_    = register_count;

    if (!verify_program_impl(view)) {
        set_last_error("program verification failed");
        return MBPF_ERR_VERIFICATION;
    }

    return MBPF_OK;
}

int execute_program(const Program &program, const void *ctx, bool *out_result)
{
    return execute_program(program.instructions_.data(),
                           program.instructions_.size(),
                           program.register_count_,
                           ctx,
                           out_result);
}

int execute_program(const Instruction *instructions,
                    size_t             instruction_count,
                    int                register_count,
                    const void        *ctx,
                    bool              *out_result)
{
    return execute_program_impl(
        instructions, instruction_count, register_count, ctx, out_result, true);
}

int execute_program_verified(const Instruction *instructions,
                             size_t             instruction_count,
                             int                register_count,
                             const void        *ctx,
                             bool              *out_result)
{
    return execute_program_impl(
        instructions, instruction_count, register_count, ctx, out_result, false);
}

}  // namespace mbpf
