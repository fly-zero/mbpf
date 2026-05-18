#ifndef MBPF_H
#define MBPF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MBPF_OK                      = 0,
    MBPF_ERR_INVALID_ARG         = 1,
    MBPF_ERR_DUP_QUALIFIER       = 2,
    MBPF_ERR_QUALIFIER_NOT_FOUND = 3,
    MBPF_ERR_PARSE               = 4,
    MBPF_ERR_TYPE_MISMATCH       = 5,
    MBPF_ERR_CODEGEN             = 6,
    MBPF_ERR_VM_RUNTIME          = 7,
    MBPF_ERR_VERIFICATION        = 8
} mbpf_status_t;

typedef enum
{
    MBPF_TYPE_BOOL = 0,
    MBPF_TYPE_I8,
    MBPF_TYPE_U8,
    MBPF_TYPE_I16,
    MBPF_TYPE_U16,
    MBPF_TYPE_I32,
    MBPF_TYPE_U32,
    MBPF_TYPE_I64,
    MBPF_TYPE_U64,
    MBPF_TYPE_IPV4,
    MBPF_TYPE_IPV6
} mbpf_type_t;

typedef struct
{
    const char *name;
    uint32_t    offset;
    uint8_t     size;
    mbpf_type_t type;
} mbpf_qualifier_desc_t;

typedef struct
{
    bool enable_const_folding;
    bool enable_short_circuit;
    bool enable_dead_code_elim;
} mbpf_compile_options_t;

typedef struct mbpf_registry mbpf_registry_t;
typedef struct mbpf_program  mbpf_program_t;

mbpf_registry_t *mbpf_registry_create(void);

int mbpf_register_qualifier(mbpf_registry_t *registry, const mbpf_qualifier_desc_t *desc);

int mbpf_compile_expression(const mbpf_registry_t        *registry,
                            const char                   *expression,
                            const mbpf_compile_options_t *options,
                            mbpf_program_t              **out_program);

int mbpf_execute(const mbpf_program_t *program, const void *ctx, bool *out_result);

int mbpf_program_serialize(const mbpf_program_t *program,
                           void                 *out_buffer,
                           size_t               *inout_buffer_size);

int mbpf_program_deserialize(const void *buffer, size_t buffer_size, mbpf_program_t **out_program);

int mbpf_program_deserialize_to_memory(const void      *buffer,
                                       size_t           buffer_size,
                                       void            *program_memory,
                                       size_t          *inout_memory_size,
                                       mbpf_program_t **out_program);

void mbpf_program_free(mbpf_program_t *program);

void mbpf_registry_free(mbpf_registry_t *registry);

const char *mbpf_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
