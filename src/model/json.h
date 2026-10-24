// A small JSON reader for replay.json: a document tree, with limits on depth and size so a
// hostile file cannot exhaust the stack or memory. No dependencies.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rp::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    /// The member called `key`, or nullptr (also when this is not an object).
    const Value* find(const std::string& key) const;
};

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Parses `text` (UTF-8). Throws Error with the byte offset of the first problem.
Value parse(const std::string& text, int maxDepth = 64);

}  // namespace rp::json
