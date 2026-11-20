// Common exception types used across the workbench core.
//
// The core reports problems with exceptions so that callers (CLI, HTTP
// transport, tests) can turn them into user-facing messages in one place.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace wb {

/// Invalid user-supplied parameter (e.g. negative conductivity, zero workers).
class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Malformed input file. Carries the 1-based line number when known (0 = n/a).
class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t line, const std::string& message)
        : std::runtime_error(compose(line, message)), line_(line) {}

    std::size_t line() const noexcept { return line_; }

private:
    static std::string compose(std::size_t line, const std::string& message) {
        return line == 0 ? message : "line " + std::to_string(line) + ": " + message;
    }
    std::size_t line_;
};

/// Raised when a long-running operation observes its cancellation token.
class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("operation cancelled by user") {}
};

/// Numerical failure such as a zero pivot in a linear solve.
class NumericalError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace wb
