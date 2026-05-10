#include "registry/qualifier_registry.hpp"

#include <limits>

#include "common/error.hpp"

namespace mbpf {

int QualifierRegistry::register_qualifier(const mbpf_qualifier_desc_t &desc)
{
    if (!desc.name || desc.name[0] == '\0') {
        set_last_error("qualifier name is empty");
        return MBPF_ERR_INVALID_ARG;
    }

    QualifierSet::insert_commit_data commit_data;
    auto [it, inserted] =
        qualifiers_.insert_check(desc.name, QualifierInfo::Compare{}, commit_data);
    if (!inserted) {
        set_last_error("duplicate qualifier: %s", desc.name);
        return MBPF_ERR_DUP_QUALIFIER;
    }

    if (desc.size != type_size(desc.type)) {
        set_last_error("qualifier size does not match type: %s", desc.name);
        return MBPF_ERR_INVALID_ARG;
    }

    if (desc.offset > std::numeric_limits<uint32_t>::max() - desc.size) {
        set_last_error("qualifier offset overflow: %s", desc.name);
        return MBPF_ERR_INVALID_ARG;
    }

    auto *info = new QualifierInfo{desc.name, desc.offset, desc.size, desc.type};
    qualifiers_.insert_commit(*info, commit_data);
    return MBPF_OK;
}

const QualifierInfo *QualifierRegistry::find(const std::string &name) const
{
    auto it = qualifiers_.find(name, QualifierInfo::Compare{});
    if (it == qualifiers_.end()) {
        return nullptr;
    }

    return &*it;
}

bool is_signed_type(mbpf_type_t type)
{
    return type == MBPF_TYPE_I8 || type == MBPF_TYPE_I16 || type == MBPF_TYPE_I32 ||
           type == MBPF_TYPE_I64;
}

bool is_bool_type(mbpf_type_t type)
{
    return type == MBPF_TYPE_BOOL;
}

uint8_t type_size(mbpf_type_t type)
{
    switch (type) {
    case MBPF_TYPE_BOOL:
    case MBPF_TYPE_I8:
    case MBPF_TYPE_U8:
        return 1;
    case MBPF_TYPE_I16:
    case MBPF_TYPE_U16:
        return 2;
    case MBPF_TYPE_I32:
    case MBPF_TYPE_U32:
        return 4;
    case MBPF_TYPE_I64:
    case MBPF_TYPE_U64:
        return 8;
    default:
        return 0;
    }
}

}  // namespace mbpf
