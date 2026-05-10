#pragma once

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/set_hook.hpp>
#include <cstdint>
#include <string>

#include "mbpf.h"

namespace mbpf {

struct QualifierInfo
{
    struct Compare;

    using SetHook = boost::intrusive::set_member_hook<boost::intrusive::store_hash<true>>;

    QualifierInfo(const QualifierInfo &)  = delete;
    QualifierInfo(QualifierInfo &&)       = delete;
    void operator=(const QualifierInfo &) = delete;
    void operator=(QualifierInfo &&)      = delete;

    QualifierInfo(const char *name, uint32_t offset, uint8_t size, mbpf_type_t type)
        : name_(name), offset_(offset), size_(size), type_(type)
    {
    }

    SetHook     hash_hook_;
    std::string name_;
    uint32_t    offset_;
    uint8_t     size_;
    mbpf_type_t type_;
};

struct QualifierInfo::Compare
{
    bool operator()(const QualifierInfo &lhs, const QualifierInfo &rhs) const
    {
        return lhs.name_ < rhs.name_;
    }

    bool operator()(const QualifierInfo &info, const std::string &name) const
    {
        return info.name_ < name;
    }

    bool operator()(const std::string &name, const QualifierInfo &info) const
    {
        return name < info.name_;
    }

    bool operator()(const std::string &lhs, const std::string &rhs) const
    {
        return lhs < rhs;
    }
};

class QualifierRegistry
{
    using QualifierSet =
        boost::intrusive::set<QualifierInfo,
                              boost::intrusive::compare<QualifierInfo::Compare>,
                              boost::intrusive::member_hook<QualifierInfo,
                                                            QualifierInfo::SetHook,
                                                            &QualifierInfo::hash_hook_>>;

public:
    ~QualifierRegistry()
    {
        qualifiers_.clear_and_dispose([](QualifierInfo *info) { delete info; });
    }

    int register_qualifier(const mbpf_qualifier_desc_t &desc);

    const QualifierInfo *find(const std::string &name) const;

private:
    QualifierSet qualifiers_;
};

bool is_signed_type(mbpf_type_t type);

bool is_bool_type(mbpf_type_t type);

uint8_t type_size(mbpf_type_t type);

}  // namespace mbpf
