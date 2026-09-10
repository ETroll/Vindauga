#pragma once
#include <QString>
#include <optional>
#include <utility>

namespace vindauga {

// Simple result type for synchronous functions that can fail.
// Use Qt signals for asynchronous errors, not this.
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result error(QString message) { return Result(std::move(message), true); }

    bool isOk() const { return m_value.has_value(); }
    explicit operator bool() const { return isOk(); }

    const T& value() const { return *m_value; }
    T& value() { return *m_value; }
    const QString& error() const { return m_error; }

private:
    explicit Result(T value) : m_value(std::move(value)) {}
    Result(QString message, bool) : m_error(std::move(message)) {}

    std::optional<T> m_value;
    QString m_error;
};

} // namespace vindauga
