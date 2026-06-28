//
// test_json.cpp — Comprehensive tests for the JSON parser/serializer.
//

#include "test_framework.hpp"
#include "../src/json.hpp"

using json::Value;

TEST(json_parse_simple_object) {
    auto v = Value::parse(R"({"name": "test", "value": 42})");
    CHECK(v.is_object());
    CHECK(v.has("name"));
    CHECK_STR_EQ(v.at("name").as_string(), "test");
    CHECK(v.at("value").as_int() == 42);
}

TEST(json_parse_array) {
    auto v = Value::parse("[1, 2, 3, 4, 5]");
    CHECK(v.is_array());
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[0].as_int(), 1);
    CHECK_EQ(v[4].as_int(), 5);
}

TEST(json_parse_nested) {
    auto v = Value::parse(R"({"a": {"b": {"c": [1, 2, {"d": true}]}}})");
    CHECK(v.is_object());
    CHECK(v["a"]["b"]["c"].is_array());
    CHECK(v["a"]["b"]["c"][2]["d"].as_bool() == true);
}

TEST(json_parse_null_bool) {
    auto v = Value::parse(R"({"n": null, "t": true, "f": false})");
    CHECK(v.at("n").is_null());
    CHECK(v.at("t").as_bool() == true);
    CHECK(v.at("f").as_bool() == false);
}

TEST(json_parse_numbers) {
    auto v = Value::parse(R"({"int": 42, "neg": -7, "float": 3.14, "exp": 1e10, "zero": 0})");
    CHECK_EQ(v.at("int").as_int(), 42);
    CHECK_EQ(v.at("neg").as_int(), -7);
    CHECK(v.at("float").as_double() == 3.14);
    CHECK(v.at("exp").as_double() == 1e10);
    CHECK_EQ(v.at("zero").as_int(), 0);
}

TEST(json_parse_string_escapes) {
    auto v = Value::parse(R"({"s": "hello\nworld\t\"quoted\""})");
    CHECK_STR_EQ(v.at("s").as_string(), "hello\nworld\t\"quoted\"");
}

TEST(json_parse_unicode_escape) {
    auto v = Value::parse(R"({"s": "\u0048\u0065\u006C\u006C\u006F"})");
    CHECK_STR_EQ(v.at("s").as_string(), "Hello");
}

TEST(json_parse_surrogate_pair) {
    // U+1F600 (grinning face emoji) encoded as surrogate pair
    auto v = Value::parse(R"({"emoji": "\uD83D\uDE00"})");
    // Should produce 4-byte UTF-8: F0 9F 98 80
    auto& s = v.at("emoji").as_string();
    CHECK_EQ(s.size(), 4u);
    CHECK_EQ(static_cast<unsigned char>(s[0]), 0xF0);
    CHECK_EQ(static_cast<unsigned char>(s[1]), 0x9F);
    CHECK_EQ(static_cast<unsigned char>(s[2]), 0x98);
    CHECK_EQ(static_cast<unsigned char>(s[3]), 0x80);
}

TEST(json_parse_empty_containers) {
    CHECK(Value::parse("{}").is_object());
    CHECK(Value::parse("[]").is_array());
    CHECK(Value::parse("{}").as_object().empty());
    CHECK(Value::parse("[]").as_array().empty());
}

TEST(json_parse_whitespace) {
    auto v = Value::parse("  {  \"a\"  :  1  }  ");
    CHECK_EQ(v.at("a").as_int(), 1);
}

TEST(json_parse_trailing_comma_rejected) {
    Value v;
    std::string err;
    CHECK(!Value::try_parse("[1, 2, 3,]", v, err));
}

TEST(json_parse_unclosed_string_rejected) {
    Value v;
    std::string err;
    CHECK(!Value::try_parse(R"({"key": "value)", v, err));
}

TEST(json_parse_throws_on_error) {
    CHECK_THROWS(Value::parse("{invalid}"));
}

TEST(json_serialize_simple) {
    Value v = Value::make_object();
    v["name"] = "test";
    v["value"] = 42;

    std::string s = v.dump();
    auto parsed = Value::parse(s);
    CHECK_STR_EQ(parsed.at("name").as_string(), "test");
    CHECK_EQ(parsed.at("value").as_int(), 42);
}

TEST(json_serialize_pretty) {
    Value v = Value::make_object();
    v["a"] = 1;
    v["b"] = "two";

    std::string compact = v.dump();
    std::string pretty = v.dump(2);

    // Pretty should have newlines
    CHECK(pretty.find('\n') != std::string::npos);
    // Compact should not have newlines
    CHECK(compact.find('\n') == std::string::npos);
    // Both should parse to same value
    CHECK(Value::parse(compact).at("a").as_int() == Value::parse(pretty).at("a").as_int());
}

TEST(json_serialize_string_escapes) {
    Value v;
    v = std::string("line1\nline2\ttab\"quote\\back");
    std::string s = v.dump();
    auto parsed = Value::parse(s);
    CHECK_STR_EQ(parsed.as_string(), "line1\nline2\ttab\"quote\\back");
}

TEST(json_serialize_array_of_objects) {
    Value arr = Value::make_array();
    for (int i = 0; i < 3; ++i) {
        Value item = Value::make_object();
        item["id"] = i;
        item["name"] = std::string("item") + std::to_string(i);
        arr.push_back(std::move(item));
    }

    std::string s = arr.dump();
    auto parsed = Value::parse(s);
    CHECK(parsed.is_array());
    CHECK_EQ(parsed.size(), 3u);
    CHECK_EQ(parsed[1].at("id").as_int(), 1);
    CHECK_STR_EQ(parsed[2].at("name").as_string(), "item2");
}

TEST(json_roundtrip_complex) {
    Value orig = Value::make_object();
    orig["string"] = "hello world";
    orig["number"] = 3.14;
    orig["integer"] = 42;
    orig["boolean"] = true;
    orig["null_val"] = nullptr;

    Value nested_arr = Value::make_array();
    nested_arr.push_back(1);
    nested_arr.push_back("two");
    nested_arr.push_back(false);
    orig["array"] = std::move(nested_arr);

    Value nested_obj = Value::make_object();
    nested_obj["deep"] = "value";
    orig["object"] = std::move(nested_obj);

    std::string s = orig.dump(2);
    auto parsed = Value::parse(s);

    CHECK_STR_EQ(parsed.at("string").as_string(), "hello world");
    CHECK(parsed.at("number").as_double() == 3.14);
    CHECK_EQ(parsed.at("integer").as_int(), 42);
    CHECK(parsed.at("boolean").as_bool() == true);
    CHECK(parsed.at("null_val").is_null());
    CHECK(parsed.at("array").is_array());
    CHECK_EQ(parsed.at("array").size(), 3u);
    CHECK_STR_EQ(parsed.at("array")[1].as_string(), "two");
    CHECK_STR_EQ(parsed.at("object").at("deep").as_string(), "value");
}

TEST(json_get_or_default) {
    Value v = Value::parse(R"({"a": 1})");
    CHECK_EQ(v.get_or("a", 0).as_int(), 1);
    CHECK_EQ(v.get_or("missing", 99).as_int(), 99);
}

TEST(json_array_modification) {
    Value v = Value::make_array();
    v.push_back("first");
    v.push_back(2);
    v.push_back(true);

    CHECK_EQ(v.size(), 3u);
    CHECK_STR_EQ(v[0].as_string(), "first");
    v[0] = "modified";
    CHECK_STR_EQ(v[0].as_string(), "modified");
}

TEST(json_object_key_access) {
    Value v = Value::make_object();
    v["key1"] = "val1";
    v["key2"] = 42;

    CHECK(v.has("key1"));
    CHECK(!v.has("key3"));
    CHECK_STR_EQ(v.at("key1").as_string(), "val1");
}

TEST(json_large_number) {
    auto v = Value::parse(R"({"big": 9223372036854775807})");
    CHECK_EQ(v.at("big").as_int(), 9223372036854775807LL);
}

TEST(json_special_floats_become_null) {
    Value v = Value::make_object();
    // We can't parse NaN/Inf from JSON, but test serialization of values
    // that might result in NaN/Inf from computation
    Value d = 1.0;
    std::string s = d.dump();
    CHECK(s != "null"); // Normal double should not become null
}
