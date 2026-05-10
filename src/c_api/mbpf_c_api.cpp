#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "codegen/compiler.hpp"
#include "common/error.hpp"
#include "frontend/parser_driver.hpp"
#include "mbpf.h"
#include "registry/qualifier_registry.hpp"
#include "vm/vm.hpp"

struct mbpf_registry
{
    mbpf::QualifierRegistry impl_;
};

struct alignas(mbpf::Instruction) mbpf_program
{
    int                       register_count_     = 0;
    uint32_t                  instruction_count_  = 0;
    const mbpf::Instruction  *instructions_       = nullptr;
    mbpf::Instruction        *owned_instructions_ = nullptr;
    bool                      owns_self_          = false;
    mutable std::atomic<bool> verified_           = false;
};

namespace {

constexpr uint32_t kProgramBlobMagic      = 0x4650424dU;  // "MBPF"
constexpr uint32_t kProgramBlobVersion    = 1;
constexpr size_t   kProgramBlobHeaderSize = 16;

void write_u32_le(std::vector<uint8_t> *out, uint32_t value)
{
    out->push_back(static_cast<uint8_t>(value & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xffu));
}

void write_u64_le(std::vector<uint8_t> *out, uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
}

bool read_u32_le(const uint8_t *buffer, size_t buffer_size, size_t *offset, uint32_t *out)
{
    if (!buffer || !offset || !out) {
        return false;
    }

    if (*offset + 4 > buffer_size) {
        return false;
    }

    const size_t i = *offset;
    *out = static_cast<uint32_t>(buffer[i]) | (static_cast<uint32_t>(buffer[i + 1]) << 8) |
           (static_cast<uint32_t>(buffer[i + 2]) << 16) |
           (static_cast<uint32_t>(buffer[i + 3]) << 24);
    *offset += 4;
    return true;
}

bool read_u64_le(const uint8_t *buffer, size_t buffer_size, size_t *offset, uint64_t *out)
{
    if (!buffer || !offset || !out) {
        return false;
    }

    if (*offset + 8 > buffer_size) {
        return false;
    }

    const size_t i = *offset;
    *out = static_cast<uint64_t>(buffer[i]) | (static_cast<uint64_t>(buffer[i + 1]) << 8) |
           (static_cast<uint64_t>(buffer[i + 2]) << 16) |
           (static_cast<uint64_t>(buffer[i + 3]) << 24) |
           (static_cast<uint64_t>(buffer[i + 4]) << 32) |
           (static_cast<uint64_t>(buffer[i + 5]) << 40) |
           (static_cast<uint64_t>(buffer[i + 6]) << 48) |
           (static_cast<uint64_t>(buffer[i + 7]) << 56);
    *offset += 8;
    return true;
}

bool parse_serialized_program_header(const uint8_t *buffer,
                                     size_t         buffer_size,
                                     uint32_t      *out_register_count,
                                     uint32_t      *out_instruction_count)
{
    if (!buffer || !out_register_count || !out_instruction_count) {
        return false;
    }

    if (buffer_size < kProgramBlobHeaderSize) {
        mbpf::set_last_error("serialized program too small");
        return false;
    }

    size_t   offset            = 0;
    uint32_t magic             = 0;
    uint32_t version           = 0;
    uint32_t register_count    = 0;
    uint32_t instruction_count = 0;

    if (!read_u32_le(buffer, buffer_size, &offset, &magic) ||
        !read_u32_le(buffer, buffer_size, &offset, &version) ||
        !read_u32_le(buffer, buffer_size, &offset, &register_count) ||
        !read_u32_le(buffer, buffer_size, &offset, &instruction_count)) {
        mbpf::set_last_error("failed to read serialized header");
        return false;
    }

    if (magic != kProgramBlobMagic) {
        mbpf::set_last_error("invalid serialized program magic");
        return false;
    }

    if (version != kProgramBlobVersion) {
        mbpf::set_last_error("unsupported serialized program version");
        return false;
    }

    if (register_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        mbpf::set_last_error("serialized register_count is out of range");
        return false;
    }

    const size_t instruction_count_sz = static_cast<size_t>(instruction_count);
    if (instruction_count_sz > (std::numeric_limits<size_t>::max() - kProgramBlobHeaderSize) / 8) {
        mbpf::set_last_error("serialized instruction_count is too large");
        return false;
    }

    const size_t expected_size = kProgramBlobHeaderSize + instruction_count_sz * 8;
    if (expected_size != buffer_size) {
        mbpf::set_last_error("serialized program size mismatch");
        return false;
    }

    *out_register_count    = register_count;
    *out_instruction_count = instruction_count;
    return true;
}

int load_serialized_instructions(const uint8_t     *buffer,
                                 size_t             buffer_size,
                                 uint32_t           instruction_count,
                                 mbpf::Instruction *out_instructions)
{
    if (!buffer || !out_instructions) {
        mbpf::set_last_error("invalid instruction load arguments");
        return MBPF_ERR_INVALID_ARG;
    }

    size_t offset = kProgramBlobHeaderSize;
    for (uint32_t i = 0; i < instruction_count; ++i) {
        uint64_t bits = 0;
        if (!read_u64_le(buffer, buffer_size, &offset, &bits)) {
            mbpf::set_last_error("failed to read serialized instruction");
            return MBPF_ERR_VERIFICATION;
        }

        out_instructions[i].bits_ = bits;
    }

    return MBPF_OK;
}

int validate_program_shape(const mbpf_program_t *program)
{
    if (!program) {
        mbpf::set_last_error("program is null");
        return MBPF_ERR_INVALID_ARG;
    }

    if (program->register_count_ < 0) {
        mbpf::set_last_error("program register_count is invalid");
        return MBPF_ERR_INVALID_ARG;
    }

    if (program->instruction_count_ > 0 && !program->instructions_) {
        mbpf::set_last_error("program instructions pointer is null");
        return MBPF_ERR_INVALID_ARG;
    }

    return MBPF_OK;
}

}  // namespace

extern "C" {

mbpf_registry_t *mbpf_registry_create(void)
{
    try {
        return new mbpf_registry();
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return nullptr;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_registry_create");
        return nullptr;
    }
}

int mbpf_register_qualifier(mbpf_registry_t *registry, const mbpf_qualifier_desc_t *desc)
{
    try {
        if (!registry || !desc) {
            mbpf::set_last_error("invalid register_qualifier arguments");
            return MBPF_ERR_INVALID_ARG;
        }

        return registry->impl_.register_qualifier(*desc);
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_INVALID_ARG;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_register_qualifier");
        return MBPF_ERR_INVALID_ARG;
    }
}

int mbpf_compile_expression(const mbpf_registry_t        *registry,
                            const char                   *expression,
                            const mbpf_compile_options_t *options,
                            mbpf_program_t              **out_program)
{
    try {
        (void)options;
        if (!registry || !expression || !out_program) {
            mbpf::set_last_error("invalid compile arguments");
            return MBPF_ERR_INVALID_ARG;
        }

        auto parse_result = mbpf::frontend::parse_expression(expression);
        if (!parse_result.error_.empty()) {
            mbpf::set_last_error(std::move(parse_result.error_));
            return MBPF_ERR_PARSE;
        }

        std::unique_ptr<mbpf_program> program(new mbpf_program());
        std::string                   codegen_error;
        mbpf::Program                 compiled_program;
        const int                     status = mbpf::compile_ast_to_program(
            parse_result.root_.get(), registry->impl_, &compiled_program, &codegen_error);
        if (status != MBPF_OK) {
            mbpf::set_last_error(std::move(codegen_error));
            return status;
        }

        const size_t instruction_count = compiled_program.instructions_.size();
        if (instruction_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            mbpf::set_last_error("compiled instruction_count is out of range");
            return MBPF_ERR_CODEGEN;
        }

        if (instruction_count > 0) {
            std::unique_ptr<mbpf::Instruction[]> owned(new mbpf::Instruction[instruction_count]);
            std::memcpy(owned.get(),
                        compiled_program.instructions_.data(),
                        instruction_count * sizeof(mbpf::Instruction));

            program->owned_instructions_ = owned.release();
            program->instructions_       = program->owned_instructions_;
        }
        program->instruction_count_ = static_cast<uint32_t>(instruction_count);
        program->register_count_    = compiled_program.register_count_;
        program->owns_self_         = true;

        *out_program = program.release();
        return MBPF_OK;
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_CODEGEN;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_compile_expression");
        return MBPF_ERR_CODEGEN;
    }
}

int mbpf_execute(const mbpf_program_t *program, const void *ctx, bool *out_result)
{
    try {
        if (!program) {
            mbpf::set_last_error("program is null");
            return MBPF_ERR_INVALID_ARG;
        }

        const int shape_status = validate_program_shape(program);
        if (shape_status != MBPF_OK) {
            return shape_status;
        }

        if (!program->verified_.load(std::memory_order_acquire)) {
            const int verify_status =
                mbpf::verify_program(program->instructions_,
                                     static_cast<size_t>(program->instruction_count_),
                                     program->register_count_);
            if (verify_status != MBPF_OK) {
                return verify_status;
            }
            program->verified_.store(true, std::memory_order_release);
        }

        return mbpf::execute_program_verified(program->instructions_,
                                              static_cast<size_t>(program->instruction_count_),
                                              program->register_count_,
                                              ctx,
                                              out_result);
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_VM_RUNTIME;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_execute");
        return MBPF_ERR_VM_RUNTIME;
    }
}

int mbpf_program_serialize(const mbpf_program_t *program,
                           void                 *out_buffer,
                           size_t               *inout_buffer_size)
{
    try {
        if (!inout_buffer_size) {
            mbpf::set_last_error("invalid serialize arguments");
            return MBPF_ERR_INVALID_ARG;
        }

        const int shape_status = validate_program_shape(program);
        if (shape_status != MBPF_OK) {
            return shape_status;
        }

        const size_t instruction_count = static_cast<size_t>(program->instruction_count_);
        if (instruction_count > (std::numeric_limits<size_t>::max() - kProgramBlobHeaderSize) / 8) {
            mbpf::set_last_error("program too large to serialize");
            return MBPF_ERR_INVALID_ARG;
        }

        const size_t required_size = kProgramBlobHeaderSize + instruction_count * 8;
        const size_t provided_size = *inout_buffer_size;
        *inout_buffer_size         = required_size;

        if (!out_buffer) {
            return MBPF_OK;
        }

        if (provided_size < required_size) {
            mbpf::set_last_error("serialize buffer too small");
            return MBPF_ERR_INVALID_ARG;
        }

        std::vector<uint8_t> blob;
        blob.reserve(required_size);
        write_u32_le(&blob, kProgramBlobMagic);
        write_u32_le(&blob, kProgramBlobVersion);
        write_u32_le(&blob, static_cast<uint32_t>(program->register_count_));
        write_u32_le(&blob, static_cast<uint32_t>(instruction_count));

        for (size_t i = 0; i < instruction_count; ++i) {
            write_u64_le(&blob, program->instructions_[i].bits_);
        }

        std::memcpy(out_buffer, blob.data(), blob.size());
        return MBPF_OK;
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_INVALID_ARG;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_program_serialize");
        return MBPF_ERR_INVALID_ARG;
    }
}

int mbpf_program_deserialize(const void *buffer, size_t buffer_size, mbpf_program_t **out_program)
{
    try {
        if (!buffer || !out_program) {
            mbpf::set_last_error("invalid deserialize arguments");
            return MBPF_ERR_INVALID_ARG;
        }

        *out_program = nullptr;

        const auto *bytes = static_cast<const uint8_t *>(buffer);

        uint32_t register_count_u32 = 0;
        uint32_t instruction_count  = 0;
        if (!parse_serialized_program_header(
                bytes, buffer_size, &register_count_u32, &instruction_count)) {
            return MBPF_ERR_VERIFICATION;
        }

        std::unique_ptr<mbpf_program> program(new mbpf_program());
        const size_t                  instruction_count_sz = static_cast<size_t>(instruction_count);
        if (instruction_count_sz > 0) {
            std::unique_ptr<mbpf::Instruction[]> owned(new mbpf::Instruction[instruction_count_sz]);
            const int                            load_status =
                load_serialized_instructions(bytes, buffer_size, instruction_count, owned.get());
            if (load_status != MBPF_OK) {
                return load_status;
            }

            program->owned_instructions_ = owned.release();
            program->instructions_       = program->owned_instructions_;
        }

        program->register_count_    = static_cast<int>(register_count_u32);
        program->instruction_count_ = instruction_count;
        program->owns_self_         = true;

        int verify_status = mbpf::verify_program(program->instructions_,
                                                 static_cast<size_t>(program->instruction_count_),
                                                 program->register_count_);
        if (verify_status != MBPF_OK) {
            return verify_status;
        }
        program->verified_.store(true, std::memory_order_release);

        *out_program = program.release();
        return MBPF_OK;
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_VERIFICATION;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_program_deserialize");
        return MBPF_ERR_VERIFICATION;
    }
}

int mbpf_program_deserialize_to_memory(const void      *buffer,
                                       size_t           buffer_size,
                                       void            *program_memory,
                                       size_t          *inout_memory_size,
                                       mbpf_program_t **out_program)
{
    try {
        if (!buffer || !inout_memory_size || !out_program) {
            mbpf::set_last_error("invalid deserialize_to_memory arguments");
            return MBPF_ERR_INVALID_ARG;
        }

        *out_program = nullptr;

        const auto *bytes              = static_cast<const uint8_t *>(buffer);
        uint32_t    register_count_u32 = 0;
        uint32_t    instruction_count  = 0;
        if (!parse_serialized_program_header(
                bytes, buffer_size, &register_count_u32, &instruction_count)) {
            return MBPF_ERR_VERIFICATION;
        }

        const size_t instruction_count_sz = static_cast<size_t>(instruction_count);
        if (instruction_count_sz > (std::numeric_limits<size_t>::max() - sizeof(mbpf_program_t)) /
                                       sizeof(mbpf::Instruction)) {
            mbpf::set_last_error("program memory size overflow");
            return MBPF_ERR_INVALID_ARG;
        }

        const size_t required_size =
            sizeof(mbpf_program_t) + instruction_count_sz * sizeof(mbpf::Instruction);
        const size_t provided_size = *inout_memory_size;
        *inout_memory_size         = required_size;

        if (!program_memory) {
            return MBPF_OK;
        }

        if (provided_size < required_size) {
            mbpf::set_last_error("deserialize_to_memory buffer too small");
            return MBPF_ERR_INVALID_ARG;
        }

        auto *program = static_cast<mbpf_program_t *>(program_memory);
        new (program) mbpf_program();

        if (instruction_count_sz > 0) {
            auto *ins = reinterpret_cast<mbpf::Instruction *>(
                static_cast<uint8_t *>(program_memory) + sizeof(mbpf_program_t));
            const int load_status =
                load_serialized_instructions(bytes, buffer_size, instruction_count, ins);
            if (load_status != MBPF_OK) {
                return load_status;
            }
            program->instructions_ = ins;
        }

        program->register_count_     = static_cast<int>(register_count_u32);
        program->instruction_count_  = instruction_count;
        program->owns_self_          = false;
        program->owned_instructions_ = nullptr;

        int verify_status = mbpf::verify_program(program->instructions_,
                                                 static_cast<size_t>(program->instruction_count_),
                                                 program->register_count_);
        if (verify_status != MBPF_OK) {
            return verify_status;
        }
        program->verified_.store(true, std::memory_order_release);

        *out_program = program;
        return MBPF_OK;
    } catch (const std::exception &e) {
        mbpf::set_last_error(std::string{e.what()});
        return MBPF_ERR_VERIFICATION;
    } catch (...) {
        mbpf::set_last_error("unknown exception in mbpf_program_deserialize_to_memory");
        return MBPF_ERR_VERIFICATION;
    }
}

void mbpf_program_free(mbpf_program_t *program)
{
    try {
        if (!program) {
            return;
        }

        if (program->owned_instructions_) {
            delete[] program->owned_instructions_;
            program->owned_instructions_ = nullptr;
        }

        if (program->owns_self_) {
            delete program;
            return;
        }

        program->instructions_      = nullptr;
        program->instruction_count_ = 0;
        program->register_count_    = 0;
        program->verified_.store(false, std::memory_order_relaxed);
    } catch (...) {
    }
}

void mbpf_registry_free(mbpf_registry_t *registry)
{
    try {
        delete registry;
    } catch (...) {
    }
}

const char *mbpf_last_error(void)
{
    return mbpf::last_error_cstr();
}

}  // extern "C"
