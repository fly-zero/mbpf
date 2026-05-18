#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
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

struct RegistryDeleter
{
    void operator()(mbpf_registry_t *registry) const
    {
        if (registry) {
            mbpf_registry_free(registry);
        }
    }
};

struct ProgramDeleter
{
    void operator()(mbpf_program_t *program) const
    {
        if (program) {
            mbpf_program_free(program);
        }
    }
};

using RegistryPtr = std::unique_ptr<mbpf_registry_t, RegistryDeleter>;
using ProgramPtr  = std::unique_ptr<mbpf_program_t, ProgramDeleter>;

const char *last_error()
{
    const char *error = mbpf_last_error();
    return error ? error : "<no error>";
}

RegistryPtr create_default_registry()
{
    RegistryPtr reg(mbpf_registry_create());
    if (!reg) {
        ADD_FAILURE() << "mbpf_registry_create returned null";
        return {};
    }

    mbpf_qualifier_desc_t qa = {"a", static_cast<uint32_t>(offsetof(Input, a_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qb = {"b", static_cast<uint32_t>(offsetof(Input, b_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qc = {"c", static_cast<uint32_t>(offsetof(Input, c_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qd = {"d", static_cast<uint32_t>(offsetof(Input, d_)), 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qflag = {
        "flag", static_cast<uint32_t>(offsetof(Input, flag_)), 1, MBPF_TYPE_BOOL};

    if (mbpf_register_qualifier(reg.get(), &qa) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qb) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qc) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qd) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qflag) != MBPF_OK) {
        ADD_FAILURE() << "failed to create default registry: " << last_error();
        return {};
    }

    return reg;
}

RegistryPtr create_ip_registry()
{
    RegistryPtr reg(mbpf_registry_create());
    if (!reg) {
        ADD_FAILURE() << "mbpf_registry_create returned null";
        return {};
    }

    mbpf_qualifier_desc_t qsrc = {
        "src", static_cast<uint32_t>(offsetof(IpInput, src_)), 4, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qpeer = {
        "peer", static_cast<uint32_t>(offsetof(IpInput, peer_)), 4, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qdst = {
        "dst", static_cast<uint32_t>(offsetof(IpInput, dst_)), 16, MBPF_TYPE_IPV6};

    if (mbpf_register_qualifier(reg.get(), &qsrc) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qpeer) != MBPF_OK ||
        mbpf_register_qualifier(reg.get(), &qdst) != MBPF_OK) {
        ADD_FAILURE() << "failed to create ip registry: " << last_error();
        return {};
    }

    return reg;
}

void fill_ipv4(uint8_t (&out)[4], const char *value)
{
    ASSERT_EQ(inet_pton(AF_INET, value, out), 1) << value;
}

void fill_ipv6(uint8_t (&out)[16], const char *value)
{
    ASSERT_EQ(inet_pton(AF_INET6, value, out), 1) << value;
}

template <typename T>
void expect_expr_result(mbpf_registry_t *reg, const char *expr, const T &in, bool expected)
{
    mbpf_program_t *raw_program = nullptr;
    ASSERT_EQ(mbpf_compile_expression(reg, expr, nullptr, &raw_program), MBPF_OK)
        << last_error() << " for expression: " << expr;
    ProgramPtr program(raw_program);
    ASSERT_NE(program.get(), nullptr);

    bool out = !expected;
    ASSERT_EQ(mbpf_execute(program.get(), &in, &out), MBPF_OK)
        << last_error() << " for expression: " << expr;
    EXPECT_EQ(out, expected) << expr;
}

void expect_compile_status(mbpf_registry_t *reg, const char *expr, int expected)
{
    mbpf_program_t *raw_program = nullptr;
    EXPECT_EQ(mbpf_compile_expression(reg, expr, nullptr, &raw_program), expected)
        << last_error() << " for expression: " << expr;
    EXPECT_EQ(raw_program, nullptr);
    if (raw_program) {
        mbpf_program_free(raw_program);
    }
}

TEST(RegistryTest, ValidatesRegistrationArguments)
{
    RegistryPtr reg(mbpf_registry_create());
    ASSERT_NE(reg.get(), nullptr);

    mbpf_qualifier_desc_t qa        = {"a", 0, 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qbad_size = {"bad_size", 4, 8, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qoverflow = {"overflow", UINT32_MAX - 1, 4, MBPF_TYPE_I32};
    mbpf_qualifier_desc_t qbad_ipv4 = {"bad_ipv4", 8, 8, MBPF_TYPE_IPV4};
    mbpf_qualifier_desc_t qbad_ipv6 = {"bad_ipv6", 16, 8, MBPF_TYPE_IPV6};

    EXPECT_EQ(mbpf_register_qualifier(nullptr, &qa), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), nullptr), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qa), MBPF_OK);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qa), MBPF_ERR_DUP_QUALIFIER);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qbad_size), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qbad_ipv4), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qbad_ipv6), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_register_qualifier(reg.get(), &qoverflow), MBPF_ERR_INVALID_ARG);
}

TEST(ExpressionTest, SupportsComparisonOperators)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input in = {5, 5, 3, 7, 1};

    expect_expr_result(reg.get(), "a == b", in, true);
    expect_expr_result(reg.get(), "a != c", in, true);
    expect_expr_result(reg.get(), "a > c", in, true);
    expect_expr_result(reg.get(), "c < a", in, true);
    expect_expr_result(reg.get(), "a >= b", in, true);
    expect_expr_result(reg.get(), "c <= b", in, true);

    expect_expr_result(reg.get(), "a == c", in, false);
    expect_expr_result(reg.get(), "a < c", in, false);
    expect_expr_result(reg.get(), "c >= d", in, false);
}

TEST(ExpressionTest, SupportsLogicalOperators)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input in = {2, 1, 0, -1, 1};

    expect_expr_result(reg.get(), "true", in, true);
    expect_expr_result(reg.get(), "false", in, false);
    expect_expr_result(reg.get(), "!false", in, true);
    expect_expr_result(reg.get(), "!true", in, false);
    expect_expr_result(reg.get(), "true && false", in, false);
    expect_expr_result(reg.get(), "true || false", in, true);
    expect_expr_result(reg.get(), "flag && (a > 1)", in, true);
    expect_expr_result(reg.get(), "flag && (a < 1)", in, false);
}

TEST(ExpressionTest, RespectsPrecedenceLevels)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input i1 = {2, 0, 0, 0, 0};
    Input i2 = {0, 3, 4, 0, 0};
    Input i3 = {0, 3, 0, 0, 0};

    expect_expr_result(reg.get(), "a > 1 || b > 2 && c > 3", i1, true);
    expect_expr_result(reg.get(), "a > 1 || b > 2 && c > 3", i2, true);
    expect_expr_result(reg.get(), "a > 1 || b > 2 && c > 3", i3, false);

    expect_expr_result(reg.get(), "!false || false && false", i1, true);
    expect_expr_result(reg.get(), "!(a > 1) || b > 2 && c > 3", i2, true);
}

TEST(ExpressionTest, ParenthesesOverridePrecedence)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input in = {2, 0, 0, 0, 0};
    expect_expr_result(reg.get(), "a > 1 || b > 2 && c > 3", in, true);
    expect_expr_result(reg.get(), "(a > 1 || b > 2) && c > 3", in, false);
    expect_expr_result(reg.get(), "!(a > 1 && b < 2)", in, false);
    expect_expr_result(reg.get(), "!((a > 1) && (b < 2))", in, false);
}

TEST(ExpressionTest, PreservesShortCircuitSemantics)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input and_left_false = {1, -1, 0, 0, 0};
    Input and_left_true  = {20, -1, 0, 0, 0};
    Input or_left_true   = {20, 1, 0, 0, 0};
    Input or_left_false  = {1, -1, 0, 0, 0};
    Input or_both_false  = {1, 1, 0, 0, 0};

    expect_expr_result(reg.get(), "a > 10 && b < 0", and_left_false, false);
    expect_expr_result(reg.get(), "a > 10 && b < 0", and_left_true, true);
    expect_expr_result(reg.get(), "a > 10 || b < 0", or_left_true, true);
    expect_expr_result(reg.get(), "a > 10 || b < 0", or_left_false, true);
    expect_expr_result(reg.get(), "a > 10 || b < 0", or_both_false, false);
}

TEST(ExpressionTest, SupportsLiteralAndConstantExpressions)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input in = {0, 0, 0, 0, 0};

    expect_expr_result(reg.get(), "1 < 2", in, true);
    expect_expr_result(reg.get(), "1 > 2", in, false);
    expect_expr_result(reg.get(), "1 <= 1 && 3 >= 3", in, true);
    expect_expr_result(reg.get(), "1 != 1 || false", in, false);
    expect_expr_result(reg.get(), "(true || false) && !false", in, true);
}

TEST(CompileTest, ReportsCompileFailures)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    mbpf_program_t *raw_program = nullptr;

    EXPECT_EQ(mbpf_compile_expression(reg.get(), "unknown > 1", nullptr, &raw_program),
              MBPF_ERR_QUALIFIER_NOT_FOUND);
    EXPECT_EQ(raw_program, nullptr);

    EXPECT_EQ(mbpf_compile_expression(reg.get(), "", nullptr, &raw_program), MBPF_ERR_PARSE);
    EXPECT_EQ(raw_program, nullptr);

    EXPECT_EQ(mbpf_compile_expression(reg.get(), "(a > 1", nullptr, &raw_program), MBPF_ERR_PARSE);
    EXPECT_EQ(raw_program, nullptr);

    EXPECT_EQ(mbpf_compile_expression(reg.get(), "a >>> 1", nullptr, &raw_program), MBPF_ERR_PARSE);
    EXPECT_EQ(raw_program, nullptr);

    EXPECT_EQ(mbpf_compile_expression(reg.get(), "a > && b < 2", nullptr, &raw_program),
              MBPF_ERR_PARSE);
    EXPECT_EQ(raw_program, nullptr);
}

TEST(ApiTest, SupportsCompileOptionsAndArgumentValidation)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    mbpf_program_t *raw_program = nullptr;
    Input           in          = {3, 1, 0, 0, 1};

    mbpf_compile_options_t options = {true, true, true};
    ASSERT_EQ(mbpf_compile_expression(reg.get(), "a > 1 && b < 2", &options, &raw_program), MBPF_OK)
        << last_error();
    ProgramPtr program(raw_program);
    ASSERT_NE(program.get(), nullptr);

    bool out = false;
    ASSERT_EQ(mbpf_execute(program.get(), &in, &out), MBPF_OK) << last_error();
    EXPECT_TRUE(out);

    mbpf_program_t *unused_program = nullptr;
    EXPECT_EQ(mbpf_compile_expression(nullptr, "a > 1", nullptr, &unused_program),
              MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_compile_expression(reg.get(), nullptr, nullptr, &unused_program),
              MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_compile_expression(reg.get(), "a > 1", nullptr, nullptr), MBPF_ERR_INVALID_ARG);

    EXPECT_EQ(mbpf_execute(nullptr, &in, &out), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_execute(program.get(), nullptr, &out), MBPF_ERR_INVALID_ARG);
    EXPECT_EQ(mbpf_execute(program.get(), &in, nullptr), MBPF_ERR_INVALID_ARG);
}

TEST(ExpressionTest, ExecutesSameProgramMultipleTimes)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    mbpf_program_t *raw_program = nullptr;
    ASSERT_EQ(mbpf_compile_expression(reg.get(), "a > b", nullptr, &raw_program), MBPF_OK)
        << last_error();
    ProgramPtr program(raw_program);
    ASSERT_NE(program.get(), nullptr);

    Input in1 = {5, 2, 0, 0, 0};
    Input in2 = {1, 3, 0, 0, 0};
    Input in3 = {9, 9, 0, 0, 0};

    bool out = false;
    ASSERT_EQ(mbpf_execute(program.get(), &in1, &out), MBPF_OK);
    EXPECT_TRUE(out);

    ASSERT_EQ(mbpf_execute(program.get(), &in2, &out), MBPF_OK);
    EXPECT_FALSE(out);

    ASSERT_EQ(mbpf_execute(program.get(), &in3, &out), MBPF_OK);
    EXPECT_FALSE(out);
}

TEST(ExpressionTest, ReusesRegistersInLongExpressions)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input       in   = {7, 0, 0, 0, 1};
    std::string expr = "(a > 0)";
    for (int i = 0; i < 1500; ++i) {
        expr += " && (a > 0)";
    }

    expect_expr_result(reg.get(), expr.c_str(), in, true);
}

TEST(SerializationTest, SupportsSerializeDeserializeRoundTrip)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    mbpf_program_t *raw_program = nullptr;
    ASSERT_EQ(mbpf_compile_expression(reg.get(), "(a > 2 && b < 5) || flag", nullptr, &raw_program),
              MBPF_OK)
        << last_error();
    ProgramPtr program(raw_program);
    ASSERT_NE(program.get(), nullptr);

    size_t serialized_size = 0;
    ASSERT_EQ(mbpf_program_serialize(program.get(), nullptr, &serialized_size), MBPF_OK);
    ASSERT_GT(serialized_size, 0u);

    std::vector<uint8_t> blob(serialized_size);
    size_t               io_size = blob.size();
    ASSERT_EQ(mbpf_program_serialize(program.get(), blob.data(), &io_size), MBPF_OK);
    ASSERT_EQ(io_size, blob.size());

    mbpf_program_t *raw_loaded = nullptr;
    ASSERT_EQ(mbpf_program_deserialize(blob.data(), blob.size(), &raw_loaded), MBPF_OK);
    ProgramPtr loaded(raw_loaded);
    ASSERT_NE(loaded.get(), nullptr);

    Input in_true  = {3, 2, 0, 0, 0};
    Input in_false = {1, 9, 0, 0, 0};

    bool out = false;
    ASSERT_EQ(mbpf_execute(loaded.get(), &in_true, &out), MBPF_OK);
    EXPECT_TRUE(out);

    out = true;
    ASSERT_EQ(mbpf_execute(loaded.get(), &in_false, &out), MBPF_OK);
    EXPECT_FALSE(out);
}

TEST(SerializationTest, RejectsInvalidBlob)
{
    mbpf_program_t      *raw_loaded = nullptr;
    std::vector<uint8_t> bad_blob(16, 0);

    EXPECT_EQ(mbpf_program_deserialize(bad_blob.data(), bad_blob.size(), &raw_loaded),
              MBPF_ERR_VERIFICATION);
    EXPECT_EQ(raw_loaded, nullptr);
}

TEST(SerializationTest, SupportsDeserializeToMemoryRoundTrip)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    mbpf_program_t *raw_program = nullptr;
    ASSERT_EQ(mbpf_compile_expression(reg.get(), "(a > 2 && b < 5) || flag", nullptr, &raw_program),
              MBPF_OK)
        << last_error();
    ProgramPtr program(raw_program);
    ASSERT_NE(program.get(), nullptr);

    size_t serialized_size = 0;
    ASSERT_EQ(mbpf_program_serialize(program.get(), nullptr, &serialized_size), MBPF_OK);
    ASSERT_GT(serialized_size, 0u);

    std::vector<uint8_t> blob(serialized_size);
    size_t               io_size = blob.size();
    ASSERT_EQ(mbpf_program_serialize(program.get(), blob.data(), &io_size), MBPF_OK);
    ASSERT_EQ(io_size, blob.size());

    size_t          memory_size = 0;
    mbpf_program_t *raw_inplace = nullptr;
    ASSERT_EQ(mbpf_program_deserialize_to_memory(
                  blob.data(), blob.size(), nullptr, &memory_size, &raw_inplace),
              MBPF_OK);
    ASSERT_GT(memory_size, 0u);
    ASSERT_EQ(raw_inplace, nullptr);

    std::vector<uint8_t> program_memory(memory_size);
    size_t               memory_io_size = program_memory.size();
    ASSERT_EQ(mbpf_program_deserialize_to_memory(
                  blob.data(), blob.size(), program_memory.data(), &memory_io_size, &raw_inplace),
              MBPF_OK);
    ASSERT_EQ(memory_io_size, program_memory.size());
    ProgramPtr inplace_program(raw_inplace);
    ASSERT_NE(inplace_program.get(), nullptr);

    Input in_true  = {3, 2, 0, 0, 0};
    Input in_false = {1, 9, 0, 0, 0};

    bool out = false;
    ASSERT_EQ(mbpf_execute(inplace_program.get(), &in_true, &out), MBPF_OK);
    EXPECT_TRUE(out);

    out = true;
    ASSERT_EQ(mbpf_execute(inplace_program.get(), &in_false, &out), MBPF_OK);
    EXPECT_FALSE(out);
}

TEST(ExpressionTest, SupportsHexadecimalLiterals)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input input = {0x1234, 0, 0, 0, 0};
    expect_expr_result(reg.get(), "a == 0x1234", input, true);
    expect_expr_result(reg.get(), "a == 0x5678", input, false);
}

TEST(ExpressionTest, SupportsBitwiseOperators)
{
    auto reg = create_default_registry();
    ASSERT_NE(reg.get(), nullptr);

    Input input = {0x1205, 0x1200, 0x0005, 0, 0};
    expect_expr_result(reg.get(), "(a & 0x00FF) == 5", input, true);
    expect_expr_result(reg.get(), "(a & 0x0F00) == 0x0200", input, true);
    expect_expr_result(reg.get(), "(a & 0x00FF) == 6", input, false);
    expect_expr_result(reg.get(), "(b | c) == a", input, true);
    expect_expr_result(reg.get(), "(b | 0x0001) == a", input, false);
}

TEST(ParserSupportTest, SupportsIpLiterals)
{
    auto ipv4_result = mbpf::frontend::parse_expression("a == 192.168.1.1");
    ASSERT_TRUE(ipv4_result.error_.empty()) << ipv4_result.error_;
    ASSERT_NE(ipv4_result.root_, nullptr);
    ASSERT_EQ(ipv4_result.root_->kind_, mbpf::frontend::ExprKind::kEq);
    ASSERT_NE(ipv4_result.root_->right_, nullptr);
    EXPECT_EQ(ipv4_result.root_->right_->kind_, mbpf::frontend::ExprKind::kIpv4);
    EXPECT_EQ(ipv4_result.root_->right_->text_value(), "192.168.1.1");

    auto ipv6_result = mbpf::frontend::parse_expression("a == 2001:db8::1");
    ASSERT_TRUE(ipv6_result.error_.empty()) << ipv6_result.error_;
    ASSERT_NE(ipv6_result.root_, nullptr);
    ASSERT_EQ(ipv6_result.root_->kind_, mbpf::frontend::ExprKind::kEq);
    ASSERT_NE(ipv6_result.root_->right_, nullptr);
    EXPECT_EQ(ipv6_result.root_->right_->kind_, mbpf::frontend::ExprKind::kIpv6);
    EXPECT_EQ(ipv6_result.root_->right_->text_value(), "2001:db8::1");
}

TEST(IpExpressionTest, SupportsEqualityAndInequality)
{
    auto reg = create_ip_registry();
    ASSERT_NE(reg.get(), nullptr);

    IpInput input = {};
    fill_ipv4(input.src_, "192.168.1.1");
    fill_ipv4(input.peer_, "10.0.0.1");
    fill_ipv6(input.dst_, "2001:db8::1");

    expect_expr_result(reg.get(), "src == 192.168.1.1", input, true);
    expect_expr_result(reg.get(), "src == 10.0.0.1", input, false);
    expect_expr_result(reg.get(), "src != 192.168.1.1", input, false);
    expect_expr_result(reg.get(), "src != 10.0.0.1", input, true);
    expect_expr_result(reg.get(), "peer == 10.0.0.1", input, true);
    expect_expr_result(reg.get(), "peer != 10.0.0.1", input, false);
    expect_expr_result(reg.get(), "dst == 2001:db8::1", input, true);
    expect_expr_result(reg.get(), "dst == 2001:db8::2", input, false);
    expect_expr_result(reg.get(), "dst != 2001:db8::1", input, false);
    expect_expr_result(reg.get(), "dst != 2001:db8::2", input, true);
}

TEST(IpExpressionTest, RejectsTypeMismatchAndUnsupportedOperators)
{
    auto default_reg = create_default_registry();
    ASSERT_NE(default_reg.get(), nullptr);
    expect_compile_status(default_reg.get(), "a == 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(default_reg.get(), "a == 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(default_reg.get(), "a != 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(default_reg.get(), "a != 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);

    auto ip_reg = create_ip_registry();
    ASSERT_NE(ip_reg.get(), nullptr);
    expect_compile_status(ip_reg.get(), "src > 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "dst > 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "src == 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "src != 2001:db8::1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "dst == 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "dst != 192.168.1.1", MBPF_ERR_TYPE_MISMATCH);
    expect_compile_status(ip_reg.get(), "flag && src", MBPF_ERR_QUALIFIER_NOT_FOUND);
}

}  // namespace
