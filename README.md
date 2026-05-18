# m-bpf

m-bpf 是一个轻量级表达式编译与执行库：把布尔表达式字符串编译为自定义字节码，并在内置 VM 中对输入上下文执行，返回布尔结果。

项目内部使用 C++ 实现，词法和语法分析基于 Flex + Bison，对外暴露稳定的 C API，适合嵌入到规则匹配、过滤判断、策略执行这类需要运行时表达式求值的场景。

## 特性

- 布尔表达式编译为自定义字节码
- 基于 VM 的解释执行，输入为 `void *ctx`，输出为布尔值
- 通过 qualifier 注册结构体字段的偏移、大小和类型
- 支持比较、逻辑运算、按位运算与括号分组
- 提供编译选项：常量折叠、短路优化、死代码消除
- 使用 CMake 构建，带基础单元测试与覆盖率入口

## 当前支持范围

已支持的表达式能力：

- 比较运算：`==`、`!=`、`>`、`<`、`>=`、`<=`
- 逻辑运算：`&&`、`||`、`!`
- 按位运算：`&`、`|`
- 分组：`(`、`)`
- 字面量：`true`、`false`、十进制整数以及十六进制整数（例如 `0x1234`）
- 类型：`bool`、`i8/u8`、`i16/u16`、`i32/u32`、`i64/u64`

表达式示例：

- `a == 0x1234`
- `(a & 0x00FF) == 5`
- `(mask | 0x10) == expected`

当前版本不覆盖：

- 函数调用
- 浮点运算
- 动态反射
- JIT 到真实 CPU 指令

## 构建依赖

- CMake 3.16+
- C++17 编译器
- Flex
- Bison

在 Debian/Ubuntu 上可使用：

```bash
sudo apt-get install cmake g++ flex bison
```

## 快速开始

### 构建

```bash
cmake -S . -B build
cmake --build build
```

如需禁用测试构建：

```bash
cmake -S . -B build -DMBPF_ENABLE_TESTS=OFF
cmake --build build
```

默认会生成两个静态库：

- `mbpf_core`：核心 C++ 实现
- `mbpf`：对外暴露的 C API 封装层

### 运行测试

```bash
ctest --test-dir build --output-on-failure
```

### 生成覆盖率

```bash
cmake -S . -B build -DMBPF_ENABLE_COVERAGE=ON
cmake --build build --target coverage
```

覆盖率目标会先执行测试，再通过 `gcov` 生成报告。

## 最小使用示例

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbpf.h"

typedef struct {
    int32_t a;
    int32_t b;
    uint8_t flag;
} input_t;

int main(void)
{
    mbpf_registry_t *registry = mbpf_registry_create();
    if (!registry) {
        return 1;
    }

    mbpf_qualifier_desc_t qa = {
        .name = "a",
        .offset = (uint32_t)offsetof(input_t, a),
        .size = 4,
        .type = MBPF_TYPE_I32,
    };
    mbpf_qualifier_desc_t qb = {
        .name = "b",
        .offset = (uint32_t)offsetof(input_t, b),
        .size = 4,
        .type = MBPF_TYPE_I32,
    };
    mbpf_qualifier_desc_t qflag = {
        .name = "flag",
        .offset = (uint32_t)offsetof(input_t, flag),
        .size = 1,
        .type = MBPF_TYPE_BOOL,
    };

    if (mbpf_register_qualifier(registry, &qa) != MBPF_OK ||
        mbpf_register_qualifier(registry, &qb) != MBPF_OK ||
        mbpf_register_qualifier(registry, &qflag) != MBPF_OK) {
        mbpf_registry_free(registry);
        return 1;
    }

    mbpf_program_t *program = NULL;
    if (mbpf_compile_expression(registry, "flag && (a > b)", NULL, &program) != MBPF_OK) {
        mbpf_registry_free(registry);
        return 1;
    }

    input_t input = {.a = 10, .b = 3, .flag = 1};
    bool result = false;

    if (mbpf_execute(program, &input, &result) != MBPF_OK) {
        mbpf_program_free(program);
        mbpf_registry_free(registry);
        return 1;
    }

    mbpf_program_free(program);
    mbpf_registry_free(registry);
    return result ? 0 : 2;
}
```

如果任一步失败，可以通过 `mbpf_last_error()` 读取最近一次错误信息。

## 对外 API

公开头文件位于 `include/mbpf.h`，核心接口包括：

- `mbpf_registry_create()`
- `mbpf_register_qualifier()`
- `mbpf_compile_expression()`
- `mbpf_execute()`
- `mbpf_program_free()`
- `mbpf_registry_free()`
- `mbpf_last_error()`

其中：

- `mbpf_registry_t` 用于保存当前可用的 qualifier 定义
- `mbpf_program_t` 是编译后的不透明程序句柄
- qualifier 的 `offset` 和 `size` 描述字段在输入上下文中的位置

## 许可证

仓库当前未声明许可证。如需对外分发或商用，请先补充明确的 license 文件。