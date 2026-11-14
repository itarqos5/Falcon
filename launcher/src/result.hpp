#pragma once

#include <optional>
#include <string>
#include <utility>

namespace launcher {

// What went wrong, in a form the UI can act on: a short stable `code` (the UI chooses what to offer for it) and a `message` fit to show.
struct Error {
    std::string code;
    std::string message;
};

template <class T>
struct Result {
    std::optional<T> value;
    Error error;

    [[nodiscard]] bool ok() const { return value.has_value(); }
    explicit operator bool() const { return ok(); }
    T& operator*() { return *value; }
    const T& operator*() const { return *value; }
    T* operator->() { return &*value; }
    const T* operator->() const { return &*value; }

    [[nodiscard]] static Result success(T v) {
        Result r;
        r.value = std::move(v);
        return r;
    }
    [[nodiscard]] static Result fail(std::string code, std::string message) {
        Result r;
        r.error = Error{std::move(code), std::move(message)};
        return r;
    }
    [[nodiscard]] static Result fail(Error e) {
        Result r;
        r.error = std::move(e);
        return r;
    }
};

}  // namespace launcher
