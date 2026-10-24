#include "check.h"
#include "model/json.h"

using rp::json::parse;

int main() {
    const auto v = parse(R"({"a": [1, -2.5e3, true, false, null], "s": "x\"\\\/\né🦠", "o": {}})");
    CHECK(v.isObject());
    const auto* a = v.find("a");
    CHECK(a && a->isArray() && a->arr.size() == 5);
    CHECK(a->arr[0].num == 1 && a->arr[1].num == -2500);
    CHECK(a->arr[2].b && !a->arr[3].b && a->arr[4].isNull());
    CHECK(v.find("s")->str == "x\"\\/\n\xc3\xa9\xf0\x9f\xa6\xa0");
    CHECK(v.find("o")->isObject() && v.find("missing") == nullptr);
    CHECK_THROWS(parse("{"));
    CHECK_THROWS(parse("[1,]"));
    CHECK_THROWS(parse("{\"a\":1} x"));
    CHECK_THROWS(parse("\"\x01\""));
    CHECK_THROWS(parse("1e999"));
    CHECK_THROWS(parse(std::string(100, '[') + std::string(100, ']')));  // deeper than 64
    CHECK(parse(std::string(60, '[') + std::string(60, ']')).isArray());
    return finish("json");
}
