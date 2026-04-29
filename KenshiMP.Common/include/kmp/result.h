#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Result<T> — Monadic error handling for the engine pipeline
// ═══════════════════════════════════════════════════════════════════════════
// Use Result<T> instead of raw booleans for operations that can fail.
// Carries error context (message + source location) through the call chain.
//
// Usage:
//   Result<Vec3> ReadPosition(uintptr_t ptr) {
//       float x, y, z;
//       if (!Memory::ReadVec3(ptr + offset, x, y, z))
//           return Err("Failed to read position at 0x{:X}", ptr);
//       return Ok(Vec3{x, y, z});
//   }
//
//   // Caller:
//   auto pos = ReadPosition(charPtr);
//   if (pos) { Use(*pos); }
//
//   // Or propagate errors:
//   KMP_TRY(pos, ReadPosition(charPtr));  // returns Err on failure
//   Use(pos);

#include <string>
#include <optional>
#include <utility>
#include <type_traits>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

namespace kmp {

// ── Error info ──
struct Error {
    std::string message;
    const char* file = nullptr;
    int         line = 0;
    const char* func = nullptr;

    Error() = default;
    Error(std::string msg, const char* f = nullptr, int l = 0, const char* fn = nullptr)
        : message(std::move(msg)), file(f), line(l), func(fn) {}

    void Log(spdlog::level::level_enum level = spdlog::level::warn) const {
        if (file) {
            spdlog::log(level, "Error: {} at {}:{} in {}", message, file, line, func ? func : "?");
        } else {
            spdlog::log(level, "Error: {}", message);
        }
    }
};

// ── Result<T> ──
template<typename T>
class Result {
public:
    // Success construction
    Result(T value) : m_value(std::move(value)), m_ok(true) {}

    // Error construction
    Result(Error err) : m_error(std::move(err)), m_ok(false) {}

    // Check success
    explicit operator bool() const { return m_ok; }
    bool IsOk() const { return m_ok; }
    bool IsErr() const { return !m_ok; }

    // Access value (asserts on error)
    T& operator*() { return Value(); }
    const T& operator*() const { return Value(); }

    T* operator->() { return &Value(); }
    const T* operator->() const { return &Value(); }

    T& Value() {
        if (!m_ok) {
            m_error.Log(spdlog::level::err);
            spdlog::critical("Result::Value() called on error result");
#ifndef NDEBUG
            __debugbreak();
#endif
        }
        return m_value;
    }

    const T& Value() const {
        if (!m_ok) {
            m_error.Log(spdlog::level::err);
            spdlog::critical("Result::Value() called on error result");
#ifndef NDEBUG
            __debugbreak();
#endif
        }
        return m_value;
    }

    // Access value with fallback
    T ValueOr(T fallback) const {
        return m_ok ? m_value : std::move(fallback);
    }

    // Access error
    const Error& GetError() const { return m_error; }

    // Log the error if present
    void LogIfError(spdlog::level::level_enum level = spdlog::level::warn) const {
        if (!m_ok) m_error.Log(level);
    }

    // Map: transform the value if Ok, propagate Err
    template<typename Fn>
    auto Map(Fn&& fn) -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));
        if (m_ok) return Result<U>(fn(m_value));
        return Result<U>(m_error);
    }

    // FlatMap / AndThen: chain Result-returning operations
    template<typename Fn>
    auto AndThen(Fn&& fn) -> decltype(fn(std::declval<T>())) {
        if (m_ok) return fn(m_value);
        using RetType = decltype(fn(std::declval<T>()));
        return RetType(m_error);
    }

private:
    T     m_value{};
    Error m_error;
    bool  m_ok;
};

// ── Result<void> specialization ──
template<>
class Result<void> {
public:
    Result() : m_ok(true) {}
    Result(Error err) : m_error(std::move(err)), m_ok(false) {}

    explicit operator bool() const { return m_ok; }
    bool IsOk() const { return m_ok; }
    bool IsErr() const { return !m_ok; }

    const Error& GetError() const { return m_error; }

    void LogIfError(spdlog::level::level_enum level = spdlog::level::warn) const {
        if (!m_ok) m_error.Log(level);
    }

private:
    Error m_error;
    bool  m_ok;
};

// ── Factory functions ──

template<typename T>
Result<T> Ok(T value) {
    return Result<T>(std::move(value));
}

inline Result<void> Ok() {
    return Result<void>();
}

// Create an error with source location
#define KMP_ERR(msg) \
    kmp::Error{msg, __FILE__, __LINE__, __FUNCTION__}

// Create an error Result with source location
#define Err(msg) \
    kmp::Error{msg, __FILE__, __LINE__, __FUNCTION__}

// Create a formatted error
#define KMP_ERR_FMT(fmtstr, ...) \
    kmp::Error{fmt::format(fmtstr, __VA_ARGS__), __FILE__, __LINE__, __FUNCTION__}

// ── KMP_TRY — propagate errors through the call chain ──
// Usage: KMP_TRY(varName, expression);
// If expression is an error Result, immediately returns the error.
// Otherwise, binds the Ok value to varName.
#define KMP_TRY(var, expr) \
    auto _kmp_try_##var = (expr); \
    if (_kmp_try_##var.IsErr()) return _kmp_try_##var.GetError(); \
    auto var = *_kmp_try_##var

// Like KMP_TRY but for Result<void> — no variable binding
#define KMP_TRY_VOID(expr) \
    do { \
        auto _kmp_try_void = (expr); \
        if (_kmp_try_void.IsErr()) return _kmp_try_void.GetError(); \
    } while (0)

} // namespace kmp
