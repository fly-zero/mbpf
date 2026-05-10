#pragma once

#include <string>

namespace mbpf {

void set_last_error(std::string &&message);

void set_last_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

const char *last_error_cstr();

}  // namespace mbpf
