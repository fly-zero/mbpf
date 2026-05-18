#include <gtest/gtest.h>

#include <cstdint>

#include "codegen/bytecode.hpp"
#include "mbpf.h"
#include "vm/vm.hpp"

using mbpf::execute_program;
using mbpf::Instruction;
using mbpf::OpCode;
using mbpf::Program;

namespace {

void push_load_imm(Program &prog, uint8_t dst, int64_t imm)
{
    auto wide = mbpf::make_load_imm(dst, imm);
    prog.instructions_.push_back(wide.first);
    prog.instructions_.push_back(wide.second);
}

}  // namespace

TEST(VmValidationTest, RejectsInvalidRegister)
{
    bool    out   = false;
    int     dummy = 0;
    Program prog;
    push_load_imm(prog, 99, 1);
    prog.register_count_ = 2;
    int ret              = execute_program(prog, &dummy, &out);
    EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
}

TEST(VmValidationTest, RejectsInvalidFieldSize)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 255, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 3, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsProgramWithoutRet)
{
    bool        out   = false;
    int         dummy = 0;
    Program     prog;
    Instruction ins = {OpCode::kLoadField, 0, 8, 0, 0};
    prog.instructions_.push_back(ins);
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    EXPECT_EQ(ret, MBPF_ERR_VM_RUNTIME);
}

TEST(VmValidationTest, RejectsInvalidJump)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kJump, 0, 0, 0, 100};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kJump, 0, 0, 0, -1};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsInvalidJumpIfFalse)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kJumpIfFalse, 0, 0, 0, 100};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kJumpIfFalse, 0, 0, 0, -1};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsInvalidMove)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 255, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 0, 255, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 1, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsInvalidCompare)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kCmpEq, 0, 0, 255, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kCmpEq, 0, 0, 1, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsInvalidRet)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program     prog;
        Instruction ins = {OpCode::kRet, 255, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program     prog;
        Instruction ins = {OpCode::kRet, 1, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsInvalidRegisterCount)
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    {
        Program prog;
        push_load_imm(prog, 0, 1);
        prog.register_count_ = 0;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
    {
        Program prog;
        push_load_imm(prog, 0, 1);
        prog.register_count_ = 257;
        ret                  = execute_program(prog, &dummy, &out);
        EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
    }
}

TEST(VmValidationTest, RejectsEmptyProgram)
{
    bool    out   = false;
    int     dummy = 0;
    Program prog;
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
}

TEST(VmValidationTest, RejectsRetOnlyInvalidProgram)
{
    bool        out   = false;
    int         dummy = 0;
    Program     prog;
    Instruction ins = {OpCode::kRet, 1, 0, 0, 0};
    prog.instructions_.push_back(ins);
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    EXPECT_EQ(ret, MBPF_ERR_VERIFICATION);
}

TEST(VmValidationTest, AcceptsLegalBoundaryProgram)
{
    bool    out   = false;
    int     dummy = 0;
    Program prog;
    static_assert(sizeof(Instruction) == sizeof(uint64_t));

    push_load_imm(prog, 0, 1);
    Instruction retins = {OpCode::kRet, 0, 0, 0, static_cast<int32_t>(0)};
    prog.instructions_.push_back(retins);
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    EXPECT_EQ(ret, MBPF_OK);
}