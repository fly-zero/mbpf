#pragma once

#include "codegen/bytecode.hpp"

namespace mbpf {

int execute_program(const Program &program, const void *ctx, bool *out_result);

int execute_program(const Instruction *instructions,
                    size_t             instruction_count,
                    int                register_count,
                    const void        *ctx,
                    bool              *out_result);

}  // namespace mbpf
