#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace mbpf {

constexpr int kMaxRegisterCount = 256;

enum class OpCode : uint8_t
{
    kLoadField,
    kLoadImm,
    kMove,
    kCmpEq,
    kCmpNe,
    kCmpGt,
    kCmpLt,
    kCmpGe,
    kCmpLe,
    kNot,
    kJumpIfFalse,
    kJump,
    kRet
};

// Base instruction layout is one 64-bit slot:
//   [ op:8 | a:8 | b:8 | c:8 | aux:32 ]
//
// For LoadField:
//   a = dest reg
//   b = field size (1/2/4/8)
//   c = 1 if signed, 0 if unsigned/bool (other bits must be 0)
//   aux_u32 = field offset
// For Jump / JumpIfFalse:
//   aux_i32 = absolute jump target slot
// For LoadImm:
//   first slot stores op/a/b/c, followed by one raw 64-bit data slot
//   carrying the immediate. Together they occupy 128 bits.
struct Instruction
{
    uint64_t bits_ = 0;

    constexpr Instruction() = default;

    constexpr Instruction(OpCode op, uint8_t a, uint8_t b, uint8_t c, int64_t aux)
        : bits_(encode(op, a, b, c, static_cast<uint32_t>(aux)))
    {
    }

    constexpr OpCode op() const
    {
        return static_cast<OpCode>(bits_ & 0xffu);
    }

    constexpr uint8_t a() const
    {
        return static_cast<uint8_t>((bits_ >> 8) & 0xffu);
    }

    constexpr uint8_t b() const
    {
        return static_cast<uint8_t>((bits_ >> 16) & 0xffu);
    }

    constexpr uint8_t c() const
    {
        return static_cast<uint8_t>((bits_ >> 24) & 0xffu);
    }

    constexpr uint32_t aux_u32() const
    {
        return static_cast<uint32_t>(bits_ >> 32);
    }

    constexpr int32_t aux_i32() const
    {
        return static_cast<int32_t>(aux_u32());
    }

    static constexpr Instruction make_load_imm_head(uint8_t dst)
    {
        return Instruction(OpCode::kLoadImm, dst, 0, 0, 0);
    }

    static constexpr Instruction make_load_imm_data(int64_t imm)
    {
        Instruction ins;
        ins.bits_ = static_cast<uint64_t>(imm);
        return ins;
    }

    constexpr int64_t raw_wide_data() const
    {
        return static_cast<int64_t>(bits_);
    }

private:
    static constexpr uint64_t encode(OpCode op, uint8_t a, uint8_t b, uint8_t c, uint32_t aux_u32)
    {
        return static_cast<uint64_t>(static_cast<uint8_t>(op)) | (static_cast<uint64_t>(a) << 8) |
               (static_cast<uint64_t>(b) << 16) | (static_cast<uint64_t>(c) << 24) |
               (static_cast<uint64_t>(aux_u32) << 32);
    }
};

static_assert(sizeof(Instruction) == sizeof(uint64_t), "Instruction must stay 64-bit");

inline std::pair<Instruction, Instruction> make_load_imm(uint8_t dst, int64_t imm)
{
    return {Instruction::make_load_imm_head(dst), Instruction::make_load_imm_data(imm)};
}

inline bool fits_i32(int64_t value)
{
    return value >= std::numeric_limits<int32_t>::min() &&
           value <= std::numeric_limits<int32_t>::max();
}

struct Program
{
    std::vector<Instruction> instructions_;
    int                      register_count_ = 0;
};

}  // namespace mbpf
