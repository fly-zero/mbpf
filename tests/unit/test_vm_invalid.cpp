#ifndef NDEBUG
#define NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstdio>

#include "codegen/bytecode.hpp"
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

// 具体分组实现
void test_invalid_register()
{
    bool    out   = false;
    int     dummy = 0;
    Program prog;
    push_load_imm(prog, 99, 1);
    prog.register_count_ = 2;
    int ret              = execute_program(prog, &dummy, &out);
    assert(ret == MBPF_ERR_VERIFICATION);
}

void test_invalid_field_size()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // size=0
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // size=255
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 255, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // size=3
    {
        Program     prog;
        Instruction ins = {OpCode::kLoadField, 0, 3, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_type_size_mismatch()
{
    bool        out   = false;
    int         dummy = 0;
    Program     prog;
    Instruction ins = {OpCode::kLoadField, 0, 8, 0, 0};
    prog.instructions_.push_back(ins);
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    assert(ret == MBPF_ERR_VERIFICATION);
}

void test_invalid_jump()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // imm=100
    {
        Program     prog;
        Instruction ins = {OpCode::kJump, 0, 0, 0, 100};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // imm=-1
    {
        Program     prog;
        Instruction ins = {OpCode::kJump, 0, 0, 0, -1};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_invalid_jumpif()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // imm=100
    {
        Program     prog;
        Instruction ins = {OpCode::kJumpIfFalse, 0, 0, 0, 100};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // imm=-1
    {
        Program     prog;
        Instruction ins = {OpCode::kJumpIfFalse, 0, 0, 0, -1};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_invalid_move()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // a=255 (out of range)
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 255, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // b=255 (out of range)
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 0, 255, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // a=1 (==reg_cnt)
    {
        Program     prog;
        Instruction ins = {OpCode::kMove, 1, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_invalid_cmp()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // c=255 (out of range)
    {
        Program     prog;
        Instruction ins = {OpCode::kCmpEq, 0, 0, 255, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // c=1 (==reg_cnt)
    {
        Program     prog;
        Instruction ins = {OpCode::kCmpEq, 0, 0, 1, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_invalid_ret()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // a=255 (out of range)
    {
        Program     prog;
        Instruction ins = {OpCode::kRet, 255, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // a=1 (==reg_cnt)
    {
        Program     prog;
        Instruction ins = {OpCode::kRet, 1, 0, 0, 0};
        prog.instructions_.push_back(ins);
        prog.register_count_ = 1;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_invalid_regcnt()
{
    bool out   = false;
    int  dummy = 0;
    int  ret   = 0;
    // reg_cnt=0
    {
        Program prog;
        push_load_imm(prog, 0, 1);
        prog.register_count_ = 0;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
    // reg_cnt=257
    {
        Program prog;
        push_load_imm(prog, 0, 1);
        prog.register_count_ = 257;
        ret                  = execute_program(prog, &dummy, &out);
        assert(ret == MBPF_ERR_VERIFICATION);
    }
}

void test_empty_program()
{
    bool    out   = false;
    int     dummy = 0;
    Program prog;
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    assert(ret == MBPF_ERR_VERIFICATION);
}

void test_ret_only_invalid()
{
    bool        out   = false;
    int         dummy = 0;
    Program     prog;
    Instruction ins = {OpCode::kRet, 1, 0, 0, 0};
    prog.instructions_.push_back(ins);
    prog.register_count_ = 1;
    int ret              = execute_program(prog, &dummy, &out);
    assert(ret == MBPF_ERR_VERIFICATION);
}

void test_legal_boundary()
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
    assert(ret == MBPF_OK);
}

int main()
{
    test_invalid_register();
    test_invalid_field_size();
    test_type_size_mismatch();
    test_invalid_jump();
    test_invalid_jumpif();
    test_invalid_move();
    test_invalid_cmp();
    test_invalid_ret();
    test_invalid_regcnt();
    test_empty_program();
    test_ret_only_invalid();
    test_legal_boundary();
}