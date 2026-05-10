#include "common/error.hpp"

#include <cstdarg>

namespace mbpf {

namespace {
thread_local std::string g_last_error;
}

void set_last_error(std::string &&message)
{
    g_last_error = std::move(message);
}

void set_last_error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char       buf[1024];
    auto const len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    g_last_error = std::string(buf, len);
}

const char *last_error_cstr()
{
    return g_last_error.c_str();
}

}  // namespace mbpf
