#pragma once

#include <string>
#include <utility>

namespace infra {
namespace util {

template <typename T>
struct Result {
    T value;
    bool ok;
    std::string error;

    static Result<T> success(T val) {
        Result<T> r;
        r.value = std::move(val);
        r.ok = true;
        return r;
    }

    static Result<T> failure(std::string err) {
        Result<T> r;
        r.ok = false;
        r.error = std::move(err);
        return r;
    }

private:
    Result() : ok(false) {}
};

} // namespace util
} // namespace infra
