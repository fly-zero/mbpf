#include <arpa/inet.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "frontend/parser_driver.hpp"
#include "mbpf.h"

namespace {

struct Input
{
    int32_t a_;
    int32_t b_;
    int32_t c_;
    int32_t d_;
    uint8_t flag_;
};

struct IpInput
{
    uint8_t src_[4];
    uint8_t peer_[4];
    uint8_t dst_[16];
};

mbpf_registry_t *create_default_registry()
{
    mbpf_registry_t *reg = mbpf_registry_create();
    assert(reg);

    mbpf_qualifier_desc_t qa = {"a", static_cast<uint32_t>(offsetof(Input, a_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qb = {"b", static_cast<uint32_t>(offsetof(Input, b_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qc = {"c", static_cast<uint32_t>(offsetof(Input, c_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qd = {"d", static_cast<uint32_t>(offsetof(Input, d_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qflag = {
        "flag", static_cast<uint32_t>(offsetof(Input, flag_)), 1, MBPF_TYPE_BOOL};

    assert(mbpf_register_qualifier(reg, &qa) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qb) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qc) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qd) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qflag) == MBPF_OK);

    return reg;
}

mbpf_registry_t *create_ip_registry()
{
    mbpf_registry_t *reg = mbpf_registry_create();
    assert(reg);

    mbpf_qualifier_desc_t qsrc = {
        "src", static_cast<uint32_t>(offsetof(IpInput, src_)), 4, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qpeer = {
        "peer", static_cast<uint32_t>(offsetof(IpInput, peer_)), 4, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qdst = {
        "dst", static_cast<uint32_t>(offsetof(IpInput, dst_)), 16, MBPF_TYPE_IPV6};

    assert(mbpf_register_qualifier(reg, &qsrc) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qpeer) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qdst) == MBPF_OK);

    return reg;
}

void fill_ipv4(uint8_t (&out)[4], const char *value)
{
    assert(inet_pton(AF_INET, value, out) == 1);
}

void fill_ipv6(uint8_t (&out)[16], const char *value)
{
    assert(inet_pton(AF_INET6, value, out) == 1);
}

template <typename T>
void assert_expr_result(mbpf_registry_t *reg, const char *expr, const T &in, bool expected)
{
    mbpf_program_t *program = nullptr;
    assert(mbpf_compile_expression(reg, expr, nullptr, &program) == MBPF_OK);
    assert(program);

    bool out = !expected;
    assert(mbpf_execute(program, &in, &out) == MBPF_OK);
    assert(out == expected);

    mbpf_program_free(program);
}

void assert_compile_status(mbpf_registry_t *reg, const char *expr, int expected)
{
    mbpf_program_t *program = nullptr;
    assert(mbpf_compile_expression(reg, expr, nullptr, &program) == expected);
    assert(program == nullptr);
}

void test_register_validation()
{
    mbpf_registry_t *reg = mbpf_registry_create();
    assert(reg);

    mbpf_qualifier_desc_t qa        = {"a", 0, 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qbad_size = {"bad_size", 4, 8, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qoverflow = {"overflow", UINT32_MAX - 1, 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qbad_ipv4 = {"bad_ipv4", 8, 8, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qbad_ipv6 = {"bad_ipv6", 16, 8, MBPF_TYPE_IPV6};

    assert(mbpf_register_qualifier(nullptr, &qa) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_register_qualifier(reg, nullptr) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_register_qualifier(reg, &qa) == MBPF_OK);
    assert(mbpf_register_qualifier(reg, &qa) == MBPF_ERR_DUP_QUALIFIER);
    assert(mbpf_register_qualifier(reg, &qbad_size) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_register_qualifier(reg, &qbad_ipv4) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_register_qualifier(reg, &qbad_ipv6) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_register_qualifier(reg, &qoverflow) == MBPF_ERR_INVALID_ARG);

    mbpf_registry_free(reg);
}

void test_all_comparison_operators()
{
    mbpf_registry_t *reg = create_default_registry();
    Input            in  = {5, 5, 3, 7, 1};

    assert_expr_result(reg, "a == b", in, true);
    assert_expr_result(reg, "a != c", in, true);
    assert_expr_result(reg, "a > c", in, true);
    assert_expr_result(reg, "c < a", in, true);
    assert_expr_result(reg, "a >= b", in, true);
    assert_expr_result(reg, "c <= b", in, true);

    assert_expr_result(reg, "a == c", in, false);
    assert_expr_result(reg, "a < c", in, false);
    assert_expr_result(reg, "c >= d", in, false);

    mbpf_registry_free(reg);
}

void test_all_logical_operators()
{
    mbpf_registry_t *reg = create_default_registry();
    Input            in  = {2, 1, 0, -1, 1};

    assert_expr_result(reg, "true", in, true);
    assert_expr_result(reg, "false", in, false);
    assert_expr_result(reg, "!false", in, true);
    assert_expr_result(reg, "!true", in, false);
    assert_expr_result(reg, "true && false", in, false);
    assert_expr_result(reg, "true || false", in, true);
    assert_expr_result(reg, "flag && (a > 1)", in, true);
    assert_expr_result(reg, "flag && (a < 1)", in, false);

    mbpf_registry_free(reg);
}

void test_precedence_levels()
{
    mbpf_registry_t *reg = create_default_registry();

    Input i1 = {2, 0, 0, 0, 0};
    Input i2 = {0, 3, 4, 0, 0};
    Input i3 = {0, 3, 0, 0, 0};

    assert_expr_result(reg, "a > 1 || b > 2 && c > 3", i1, true);
    assert_expr_result(reg, "a > 1 || b > 2 && c > 3", i2, true);
    assert_expr_result(reg, "a > 1 || b > 2 && c > 3", i3, false);

    assert_expr_result(reg, "!false || false && false", i1, true);
    assert_expr_result(reg, "!(a > 1) || b > 2 && c > 3", i2, true);

    mbpf_registry_free(reg);
}

void test_parentheses_override_precedence()
{
    mbpf_registry_t *reg = create_default_registry();

    Input in = {2, 0, 0, 0, 0};
    assert_expr_result(reg, "a > 1 || b > 2 && c > 3", in, true);
    assert_expr_result(reg, "(a > 1 || b > 2) && c > 3", in, false);
    assert_expr_result(reg, "!(a > 1 && b < 2)", in, false);
    assert_expr_result(reg, "!((a > 1) && (b < 2))", in, false);

    mbpf_registry_free(reg);
}

void test_short_circuit_semantics()
{
    mbpf_registry_t *reg = create_default_registry();

    Input and_left_false = {1, -1, 0, 0, 0};
    Input and_left_true  = {20, -1, 0, 0, 0};
    Input or_left_true   = {20, 1, 0, 0, 0};
    Input or_left_false  = {1, -1, 0, 0, 0};
    Input or_both_false  = {1, 1, 0, 0, 0};

    assert_expr_result(reg, "a > 10 && b < 0", and_left_false, false);
    assert_expr_result(reg, "a > 10 && b < 0", and_left_true, true);

    assert_expr_result(reg, "a > 10 || b < 0", or_left_true, true);
    assert_expr_result(reg, "a > 10 || b < 0", or_left_false, true);
    assert_expr_result(reg, "a > 10 || b < 0", or_both_false, false);

    mbpf_registry_free(reg);
}

void test_literals_and_constant_expressions()
{
    mbpf_registry_t *reg = create_default_registry();
    Input            in  = {0, 0, 0, 0, 0};

    assert_expr_result(reg, "1 < 2", in, true);
    assert_expr_result(reg, "1 > 2", in, false);
    assert_expr_result(reg, "1 <= 1 && 3 >= 3", in, true);
    assert_expr_result(reg, "1 != 1 || false", in, false);
    assert_expr_result(reg, "(true || false) && !false", in, true);

    mbpf_registry_free(reg);
}

void test_compile_failures()
{
    mbpf_registry_t *reg = create_default_registry();

    mbpf_program_t *program = nullptr;

    assert(mbpf_compile_expression(reg, "unknown > 1", nullptr, &program) ==
           MBPF_ERR_QUALIFIER_NOT_FOUND);
    assert(!program);

    assert(mbpf_compile_expression(reg, "", nullptr, &program) == MBPF_ERR_PARSE);
    assert(!program);

    assert(mbpf_compile_expression(reg, "(a > 1", nullptr, &program) == MBPF_ERR_PARSE);
    assert(!program);

    assert(mbpf_compile_expression(reg, "a >>> 1", nullptr, &program) == MBPF_ERR_PARSE);
    assert(!program);

    assert(mbpf_compile_expression(reg, "a > && b < 2", nullptr, &program) == MBPF_ERR_PARSE);
    assert(!program);

    mbpf_registry_free(reg);
}

void test_compile_options_and_api_args()
{
    mbpf_registry_t *reg     = create_default_registry();
    mbpf_program_t  *program = nullptr;
    Input            in      = {3, 1, 0, 0, 1};

    mbpf_compile_options_t options = {true, true, true};
    assert(mbpf_compile_expression(reg, "a > 1 && b < 2", &options, &program) == MBPF_OK);
    assert(program);

    bool out = false;
    assert(mbpf_execute(program, &in, &out) == MBPF_OK);
    assert(out == true);

    assert(mbpf_compile_expression(nullptr, "a > 1", nullptr, &program) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_compile_expression(reg, nullptr, nullptr, &program) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_compile_expression(reg, "a > 1", nullptr, nullptr) == MBPF_ERR_INVALID_ARG);

    assert(mbpf_execute(nullptr, &in, &out) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_execute(program, nullptr, &out) == MBPF_ERR_INVALID_ARG);
    assert(mbpf_execute(program, &in, nullptr) == MBPF_ERR_INVALID_ARG);

    mbpf_program_free(program);
    mbpf_registry_free(reg);
}

void test_execute_same_program_multiple_times()
{
    mbpf_registry_t *reg     = create_default_registry();
    mbpf_program_t  *program = nullptr;

    assert(mbpf_compile_expression(reg, "a > b", nullptr, &program) == MBPF_OK);
    assert(program);

    Input in1 = {5, 2, 0, 0, 0};
    Input in2 = {1, 3, 0, 0, 0};
    Input in3 = {9, 9, 0, 0, 0};

    bool out = false;
    assert(mbpf_execute(program, &in1, &out) == MBPF_OK);
    assert(out == true);

    assert(mbpf_execute(program, &in2, &out) == MBPF_OK);
    assert(out == false);

    assert(mbpf_execute(program, &in3, &out) == MBPF_OK);
    assert(out == false);

    mbpf_program_free(program);
    mbpf_registry_free(reg);
}

void test_long_expression_register_reuse()
{
    mbpf_registry_t *reg = create_default_registry();
    Input            in  = {7, 0, 0, 0, 1};

    // Long left-associated chain used to regress when codegen only increased register ids.
    std::string expr = "(a > 0)";
    for (int i = 0; i < 1500; ++i) {
        expr += " && (a > 0)";
    }

    assert_expr_result(reg, expr.c_str(), in, true);

    mbpf_registry_free(reg);
}

void test_program_serialize_deserialize_roundtrip()
{
    mbpf_registry_t *reg     = create_default_registry();
    mbpf_program_t  *program = nullptr;

    assert(mbpf_compile_expression(reg, "(a > 2 && b < 5) || flag", nullptr, &program) == MBPF_OK);
    assert(program);

    size_t serialized_size = 0;
    assert(mbpf_program_serialize(program, nullptr, &serialized_size) == MBPF_OK);
    assert(serialized_size > 0);

    std::vector<uint8_t> blob(serialized_size);
    size_t               io_size = blob.size();
    assert(mbpf_program_serialize(program, blob.data(), &io_size) == MBPF_OK);
    assert(io_size == blob.size());

    mbpf_program_t *loaded = nullptr;
    assert(mbpf_program_deserialize(blob.data(), blob.size(), &loaded) == MBPF_OK);
    assert(loaded);

    Input in_true  = {3, 2, 0, 0, 0};
    Input in_false = {1, 9, 0, 0, 0};

    bool out = false;
    assert(mbpf_execute(loaded, &in_true, &out) == MBPF_OK);
    assert(out == true);

    out = true;
    assert(mbpf_execute(loaded, &in_false, &out) == MBPF_OK);
    assert(out == false);

    mbpf_program_free(loaded);
    mbpf_program_free(program);
    mbpf_registry_free(reg);
}

void test_program_deserialize_invalid_blob()
{
    mbpf_program_t      *loaded = nullptr;
    std::vector<uint8_t> bad_blob(16, 0);

    assert(mbpf_program_deserialize(bad_blob.data(), bad_blob.size(), &loaded) ==
           MBPF_ERR_VERIFICATION);
    assert(loaded == nullptr);
}

void test_program_deserialize_to_memory_roundtrip()
{
    mbpf_registry_t *reg     = create_default_registry();
    mbpf_program_t  *program = nullptr;

    assert(mbpf_compile_expression(reg, "(a > 2 && b < 5) || flag", nullptr, &program) == MBPF_OK);
    assert(program);

    size_t serialized_size = 0;
    assert(mbpf_program_serialize(program, nullptr, &serialized_size) == MBPF_OK);
    assert(serialized_size > 0);

    std::vector<uint8_t> blob(serialized_size);
    size_t               io_size = blob.size();
    assert(mbpf_program_serialize(program, blob.data(), &io_size) == MBPF_OK);
    assert(io_size == blob.size());

    size_t          memory_size     = 0;
    mbpf_program_t *inplace_program = nullptr;
    assert(mbpf_program_deserialize_to_memory(
               blob.data(), blob.size(), nullptr, &memory_size, &inplace_program) == MBPF_OK);
    assert(memory_size > 0);
    assert(inplace_program == nullptr);

    std::vector<uint8_t> program_memory(memory_size);
    size_t               memory_io_size = program_memory.size();
    assert(
        mbpf_program_deserialize_to_memory(
            blob.data(), blob.size(), program_memory.data(), &memory_io_size, &inplace_program) ==
        MBPF_OK);
    assert(memory_io_size == program_memory.size());
    assert(inplace_program != nullptr);

    Input in_true  = {3, 2, 0, 0, 0};
    Input in_false = {1, 9, 0, 0, 0};

    bool out = false;
    assert(mbpf_execute(inplace_program, &in_true, &out) == MBPF_OK);
    assert(out == true);

    out = true;
    assert(mbpf_execute(inplace_program, &in_false, &out) == MBPF_OK);
    assert(out == false);

    mbpf_program_free(inplace_program);
    mbpf_program_free(program);
    mbpf_registry_free(reg);
}

void test_hexadecimal_support()
{
    mbpf_registry_t *reg = create_default_registry();

    Input input = {0x1234, 0, 0, 0, 0};
    assert_expr_result(reg, "a == 0x1234", input, true);
    assert_expr_result(reg, "a == 0x5678", input, false);

    mbpf_registry_free(reg);
}

void test_bitwise_operators()
{
    mbpf_registry_t *reg = create_default_registry();

    Input input = {0x1205, 0x1200, 0x0005, 0, 0};
    assert_expr_result(reg, "(a & 0x00FF) == 5", input, true);
    assert_expr_result(reg, "(a & 0x0F00) == 0x0200", input, true);
    assert_expr_result(reg, "(a & 0x00FF) == 6", input, false);
    assert_expr_result(reg, "(b | c) == a", input, true);
    assert_expr_result(reg, "(b | 0x0001) == a", input, false);

    mbpf_registry_free(reg);
}

void test_ip_literal_parser_support()
{
    auto ipv4_result = mbpf::frontend::parse_expression("a == 192.168.1.1");
    assert(ipv4_result.error_.empty());
    assert(ipv4_result.root_);
    assert(ipv4_result.root_->kind_ == mbpf::frontend::ExprKind::kEq);
    assert(ipv4_result.root_->right_);
    assert(ipv4_result.root_->right_->kind_ == mbpf::frontend::ExprKind::kIpv4);
    assert(ipv4_result.root_->right_->text_value_ == "192.168.1.1");

    auto ipv6_result = mbpf::frontend::parse_expression("a == 2001:db8::1");
    assert(ipv6_result.error_.empty());
    assert(ipv6_result.root_);
    assert(ipv6_result.root_->kind_ == mbpf::frontend::ExprKind::kEq);
    assert(ipv6_result.root_->right_);
    assert(ipv6_result.root_->right_->kind_ == mbpf::frontend::ExprKind::kIpv6);
    assert(ipv6_result.root_->right_->text_value_ == "2001:db8::1");
}

void test_ip_equality_support()
{
    mbpf_registry_t *reg = create_ip_registry();

    IpInput input = {};
    fill_ipv4(input.src_, "192.168.1.1");
    fill_ipv4(input.peer_, "10.0.0.1");
    fill_ipv6(input.dst_, "2001:db8::1");

    assert_expr_result(reg, "src == 192.168.1.1", input, true);
    assert_expr_result(reg, "src == 10.0.0.1", input, false);
    assert_expr_result(reg, "src != 192.168.1.1", input, false);
    assert_expr_result(reg, "src != 10.0.0.1", input, true);
    assert_expr_result(reg, "peer == 10.0.0.1", input, true);
    assert_expr_result(reg, "peer != 10.0.0.1", input, false);
    assert_expr_result(reg, "dst == 2001:db8::1", input, true);
    assert_expr_result(reg, "dst == 2001:db8::2", input, false);
    assert_expr_result(reg, "dst != 2001:db8::1", input, false);
    assert_expr_result(reg, "dst != 2001:db8::2", input, true);

    mbpf_registry_free(reg);
}

void test_ip_type_mismatch_failures()
{
    mbpf_registry_t *default_reg = create_default_registry();
    assert_compile_status(default_reg, "a == 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(default_reg, "a == 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(default_reg, "a != 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(default_reg, "a != 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    mbpf_registry_free(default_reg);

    mbpf_registry_t *ip_reg = create_ip_registry();
    assert_compile_status(ip_reg, "src > 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "dst > 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "src == 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "src != 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "dst == 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "dst != 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    assert_compile_status(ip_reg, "flag && src", MBPF_ERR_QUALIFIER_NOT_FOUND);
    mbpf_registry_free(ip_reg);
}

}  // namespace

int main()
{
    test_register_validation();
    test_all_comparison_operators();
    test_all_logical_operators();
    test_precedence_levels();
    test_parentheses_override_precedence();
    test_short_circuit_semantics();
    test_literals_and_constant_expressions();
    test_compile_failures();
    test_compile_options_and_api_args();
    test_execute_same_program_multiple_times();
    test_long_expression_register_reuse();
    test_program_serialize_deserialize_roundtrip();
    test_program_deserialize_invalid_blob();
    test_program_deserialize_to_memory_roundtrip();
    test_hexadecimal_support();
    test_bitwise_operators();
    test_ip_literal_parser_support();
    test_ip_equality_support();
    test_ip_type_mismatch_failures();
    return 0;
}
