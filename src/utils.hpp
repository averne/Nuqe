#pragma once

#include <cmath>
#include <cstring>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <type_traits>
#include <switch.h>

#include "mtp_codes.hpp"

#define _STRINGIFY(x)      #x
#define _CONCATENATE(x, y) x##y
#define  STRINGIFY(x)      _STRINGIFY(x)
#define  CONCATENATE(x, y) _CONCATENATE(x, y)

#define ANONYMOUS_VAR CONCATENATE(v_, __COUNTER__)

#define ASSERT_SIZE(t, sz) static_assert(sizeof(t) == sz, "Wrong size for " STRINGIFY(t))
#define ASSERT_STANDARD_LAYOUT(t) static_assert(std::is_standard_layout_v<t>, STRINGIFY(t) " is not standard layout")

#define UNUSED(x) ((void)(x))

#define NON_COPYABLE(c)                       \
    public:                                   \
        c(const c &) = delete;                \
        c &operator =(const c &) = delete

#define NON_MOVEABLE(c)                       \
    public:                                   \
        c(c &&) = delete;                     \
        c &operator =(c &&) = delete

namespace nq {

template <typename Rep, typename Period>
constexpr inline std::uint64_t to_ns(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::nanoseconds(duration).count();
}

static inline std::u16string to_utf16(const std::string &str) {
    return std::u16string(str.begin(), str.end());
}

static inline std::string to_utf8(const std::u16string &str) {
    return std::string(str.begin(), str.end());
}

class ScopeGuard {
    NON_COPYABLE(ScopeGuard);
    NON_MOVEABLE(ScopeGuard);

    public:
        template <typename T>
        [[nodiscard]] static ScopeGuard create(T &&f) {
            return ScopeGuard(std::forward<T>(f));
        }

        ~ScopeGuard() {
            if (this->f)
                this->run();
        }

        void run() {
            this->f();
            this->dismiss();
        }

        void dismiss() {
            f = nullptr;
        }

    private:
        template<class T>
        ScopeGuard(T &&f): f(std::forward<T>(f)) { }

    private:
        std::function<void()> f;
};

struct Result {
    std::uint32_t value = 0;

    constexpr inline Result() = default;
    constexpr inline Result(std::uint32_t code): value(code) { }
    constexpr inline Result(std::uint32_t module, std::uint32_t desc): value((module & 0x1ff) | ((desc & 0x1fff) << 9)) { }

    static constexpr inline Result success() { return static_cast<Result>(0); }
    static constexpr inline Result failure() { return static_cast<Result>(-1); }

    constexpr inline bool succeeded() const {
        return this->value == 0;
    }

    constexpr inline bool failed() const {
        return this->value != 0;
    }

    constexpr inline operator bool() const { return succeeded(); }
    constexpr inline explicit operator std::uint32_t() const { return this->value; }

    constexpr inline std::uint32_t code()   const { return this->value;}
    constexpr inline std::uint32_t module() const { return (this->value & 0x1ff) | 2000; }
    constexpr inline std::uint32_t desc()   const { return (this->value >> 9) & 0x3fff; }

    constexpr inline bool operator==(const Result &rhs) const {
        return this->value == rhs.code();
    }

    constexpr inline bool operator!=(const Result &rhs) const {
        return !(*this == rhs);
    }

    constexpr inline bool operator==(const std::uint32_t &rhs) const {
        return this->value == rhs;
    }

    constexpr inline bool operator!=(const std::uint32_t &rhs) const {
        return !(*this == rhs);
    }
};

#if defined(DEBUG) && !defined(LOG_NXLINK) && !defined(LOG_FILE) && !defined(LOG_CONSOLE)
#   pragma message "Debug code is enabled but no logging backend was specified"
#endif

namespace log {

#ifdef DEBUG
namespace impl {

inline std::atomic_bool running = false;
inline std::chrono::time_point<std::chrono::system_clock> start_time;

#ifdef LOG_FILE
inline FILE *fp;
#endif

} // namespace impl
#endif // DEBUG

static inline bool is_running() {
#ifdef DEBUG
    return impl::running;
#else
    return false;
#endif
}

static inline std::uint32_t initialize() {
#ifdef DEBUG
    if (is_running())
        return 1;

#if defined(LOG_NXLINK)
    socketInitializeDefault();
    nxlinkStdio();
#elif defined(LOG_FILE)
    impl::fp = fopen("/Nuqe.log", "w");
#elif defined(LOG_CONSOLE)
    consoleInit(nullptr);
#endif

    impl::start_time = std::chrono::system_clock::now();
    impl::running = true;
#endif
    return 0;
}

static inline std::uint32_t finalize() {
#ifdef DEBUG
    if (!is_running())
        return 1;

    impl::running = false;

#if defined(LOG_NXLINK)
    socketExit();
#elif defined(LOG_FILE)
    fclose(impl::fp);
#elif defined(LOG_CONSOLE)
    consoleExit(nullptr);
#endif
#endif
    return 0;
}

template <typename ...Args>
static inline void log(const std::string &fmt, Args &&...args) {
#ifdef DEBUG
    if (!is_running())
        return;

    std::string format = "[%#.3fs] " + fmt;

#if defined(LOG_NXLINK) || defined(LOG_CONSOLE)
    printf(
#elif defined(LOG_FILE)
    fprintf(impl::fp,
#endif
        format.c_str(),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - impl::start_time).count() / 1000.0f,
        std::forward<Args>(args)...
    );
#ifdef LOG_FILE
    fflush(impl::fp);
#elif defined(LOG_CONSOLE)
    consoleUpdate(nullptr);
#endif
#endif
}

[[maybe_unused]] static void data(const void *data, size_t size, const char *prefix) {
#ifdef DEBUG
    size_t prefix_len = (prefix) ? strlen(prefix) : 0;
    std::string str(prefix_len + ceil(size / 16.0) * 74 + 1, ' ');
    char *str_data = (char *)str.data();
    if (prefix)
        std::memcpy(str_data, prefix, prefix_len);
    size_t data_idx = prefix_len + 4, ascii_idx = data_idx + 53;
    for (size_t i = 0; i < size; ++i) {
        str_data[data_idx + snprintf(&str_data[data_idx], 3, "%02x", ((u8 *)data)[i])] = ' ';
        str_data[ascii_idx] = (' ' <= ((u8 *)data)[i] && ((u8 *)data)[i] <= '~') ? ((u8 *)data)[i] : '.';
        data_idx += 3;
        ++ascii_idx;
        if ((i + 1) % 16 == 0) {
            str_data[data_idx + 1] = '|';
            str_data[ascii_idx] = '\n';
            data_idx += 25;
            ascii_idx = data_idx + 53;
        } else if ((i + 1) % 8 == 0) {
            ++data_idx;
        } else if (i + 1 == size) {
            if ((i & 0xf) < 8) ++data_idx;
            str_data[data_idx + (16 - i % 16) * 3 - 2] = '|';
        }
    }
    *(u16 *)&str_data[str.size() - 2] = '\n';
    log(str);
#endif
}

} // namespace log

} // namespace nq

#define SCOPE_GUARD(x) auto ANONYMOUS_VAR = ::nq::ScopeGuard::create(x)

#ifdef DEBUG
#   define TRACE(fmt, ...) ::nq::log::log("[TRACE]: " fmt, ##__VA_ARGS__)
#   define INFO(fmt, ...)  ::nq::log::log("[INFO]:  " fmt, ##__VA_ARGS__)
#   define WARN(fmt, ...)  ::nq::log::log("[WARN]:  " fmt, ##__VA_ARGS__)
#   define ERROR(fmt, ...) ::nq::log::log("[ERROR]: " fmt, ##__VA_ARGS__)
#   define FATAL(fmt, ...) ::nq::log::log("[FATAL]: " fmt, ##__VA_ARGS__)

#   define DTRACE(d, sz)   ::nq::log::data(d, sz, "[TRACE]: " STRINGIFY(d) ":\n")
#   define DINFO(d, sz)    ::nq::log::data(d, sz, "[INFO]:  " STRINGIFY(d) ":\n")
#   define DWARN(d, sz)    ::nq::log::data(d, sz, "[WARN]:  " STRINGIFY(d) ":\n")
#   define DERROR(d, sz)   ::nq::log::data(d, sz, "[ERROR]: " STRINGIFY(d) ":\n")
#   define DFATAL(d, sz)   ::nq::log::data(d, sz, "[FATAL]: " STRINGIFY(d) ":\n")
#else
#   define TRACE(fmt, ...) ({})
#   define INFO(fmt, ...)  ({})
#   define WARN(fmt, ...)  ({})
#   define ERROR(fmt, ...) ({})
#   define FATAL(fmt, ...) ({})

#   define DTRACE(d, sz)   ({})
#   define DINFO(d, sz)    ({})
#   define DWARN(d, sz)    ({})
#   define DERROR(d, sz)   ({})
#   define DFATAL(d, sz)   ({})
#endif

#define TRY(x, cb) ({                    \
    if (bool _rc = (x); !_rc) {          \
        ({cb;});                         \
    }                                    \
})
#define TRY_GOTO(x, l)    TRY(x, goto l)
#define TRY_RETURN(x)     TRY(x, return _rc)
#define TRY_RETURNV(x, v) TRY(x, return v)
#define TRY_FATAL(x)      TRY(x, fatalThrow(_rc))
#define TRY_LOG(x)        TRY(x, ERROR(STRINGIFY(x) " failed\n"))

#define R_TRY(x, cb) ({                         \
    if (::nq::Result _rc = (x); _rc.failed()) { \
        ({cb;});                                \
    }                                           \
})
#define R_TRY_GOTO(x, l)    R_TRY(x, goto l)
#define R_TRY_RETURN(x)     R_TRY(x, return _rc)
#define R_TRY_RETURNV(x, v) R_TRY(x, return v)
#define R_TRY_FATAL(x)      R_TRY(x, fatalThrow(_rc))
#define R_TRY_LOG(x)        R_TRY(x, ERROR(STRINGIFY(x) " failed with %#x (%04d-%04d)\n", _rc, _rc.module(), _rc.desc()))

#define MTP_TRY(x, cb) ({                                                        \
    if (::nq::mtp::ResponseCode _rc = (x); _rc != ::nq::mtp::ResponseCode::OK) { \
        ({cb;});                                                                 \
    }                                                                            \
})
#define MTP_TRY_GOTO(x, l)    MTP_TRY(x, goto l)
#define MTP_TRY_RETURN(x)     MTP_TRY(x, return _rc)
#define MTP_TRY_RETURNV(x, v) MTP_TRY(x, return v)
#define MTP_TRY_FATAL(x)      MTP_TRY(x, fatalThrow(_rc))
#define MTP_TRY_LOG(x)        MTP_TRY(x, ERROR(STRINGIFY(x) " failed with %#x\n", _rc))
