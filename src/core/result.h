#pragma once

/// @file result.h
/// Lightweight Result<T, E> type for C++20 (replaces std::expected from C++23).

#include <string>
#include <variant>

namespace canmatik {

/// A simple Result type: holds either a value T or an error string.
/// For T=void, use Result<void>.
template <typename T>
class Result {
public:
    /// Construct a success result.
    Result(T value) : data_(std::move(value)) {} // NOLINT(implicit)

    /// Construct an error result (use Result<T>::error()).
    struct ErrorTag {};
    Result(ErrorTag, std::string err) : data_(std::move(err)) {}

    /// Create an error result.
    static Result error(std::string msg) { return Result(ErrorTag{}, std::move(msg)); }

    [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return has_value(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] T& operator*() & { return value(); }
    [[nodiscard]] const T* operator->() const { return &std::get<T>(data_); }
    [[nodiscard]] T* operator->() { return &std::get<T>(data_); }

    [[nodiscard]] const std::string& error() const { return std::get<std::string>(data_); }

private:
    std::variant<T, std::string> data_;
};

/// Specialization for void (success with no value, or error string).
template <>
class Result<void> {
public:
    Result() : err_() {}

    struct ErrorTag {};
    Result(ErrorTag, std::string err) : err_(std::move(err)) {}

    static Result error(std::string msg) { return Result(ErrorTag{}, std::move(msg)); }

    [[nodiscard]] bool has_value() const { return err_.empty(); }
    explicit operator bool() const { return has_value(); }

    [[nodiscard]] const std::string& error() const { return err_; }

private:
    std::string err_;
};

} // namespace canmatik
