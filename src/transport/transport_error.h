#pragma once

/// @file transport_error.h
/// Structured error from the transport layer.

#include <cstdint>
#include <string>
#include <stdexcept>

namespace canmatik {

/// Structured error originating from the J2534 transport layer or mock.
struct TransportError : public std::runtime_error {
    int32_t code;          ///< J2534 status code (or mock error code)
    std::string source;    ///< Originating function (e.g., "PassThruConnect")
    bool recoverable;      ///< Whether the session can continue after this error

    TransportError(int32_t code, const std::string& message,
                   const std::string& source = "", bool recoverable = false)
        : std::runtime_error(message)
        , code(code)
        , source(source)
        , recoverable(recoverable)
    {}
};

} // namespace canmatik
