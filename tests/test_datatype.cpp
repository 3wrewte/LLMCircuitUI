#include "../src/LogicEngine.h"
#include <cassert>
#include <iostream>

int main() {
    int failures = 0;

    // --- TOKEN_ID enum ---
    {
        assert(static_cast<int>(DataType::TOKEN_ID) == 6);
        assert(datatype_to_str(DataType::TOKEN_ID) == "TOKEN_ID");
        assert(str_to_datatype("TOKEN_ID") == DataType::TOKEN_ID);
    }

    // --- Value::token_id ---
    {
        Value v = Value::token_id(42);
        assert(v.type == DataType::TOKEN_ID);
        assert(v.i == 42);
    }

    // --- Value::token_stream (vector<int>) ---
    {
        Value v = Value::token_stream(std::vector<int>{1, 2, 3});
        assert(v.type == DataType::TOKEN_STREAM);
        assert(v.token_ids.size() == 3);
        assert(v.token_ids[0] == 1);
        assert(v.token_ids[1] == 2);
        assert(v.token_ids[2] == 3);
    }

    // --- Value::context_buffer (vector<int>) ---
    {
        Value v = Value::context_buffer(std::vector<int>{10, 20, 30});
        assert(v.type == DataType::CONTEXT_BUFFER);
        assert(v.token_ids.size() == 3);
        assert(v.token_ids[0] == 10);
    }

    // --- to_json / from_json roundtrip: TOKEN_ID ---
    {
        Value v = Value::token_id(99);
        Json::Value j = v.to_json();
        assert(j["type"].asString() == "TOKEN_ID");
        assert(j["value"].asInt() == 99);
        Value v2 = Value::from_json(j);
        assert(v2.type == DataType::TOKEN_ID);
        assert(v2.i == 99);
    }

    // --- to_json / from_json roundtrip: TOKEN_STREAM with ints ---
    {
        Value v = Value::token_stream(std::vector<int>{7, 8, 9});
        Json::Value j = v.to_json();
        assert(j["type"].asString() == "TOKEN_STREAM");
        assert(j["value"].isArray());
        assert(j["value"].size() == 3);
        assert(j["value"][0].asInt() == 7);
        Value v2 = Value::from_json(j);
        assert(v2.type == DataType::TOKEN_STREAM);
        assert(v2.token_ids.size() == 3);
        assert(v2.token_ids[0] == 7);
    }

    // --- to_json / from_json roundtrip: CONTEXT_BUFFER with ints ---
    {
        Value v = Value::context_buffer(std::vector<int>{100, 200});
        Json::Value j = v.to_json();
        assert(j["type"].asString() == "CONTEXT_BUFFER");
        Value v2 = Value::from_json(j);
        assert(v2.type == DataType::CONTEXT_BUFFER);
        assert(v2.token_ids.size() == 2);
        assert(v2.token_ids[0] == 100);
    }

    // --- Legacy types still work ---
    {
        Value vi(5);
        assert(vi.type == DataType::INT && vi.i == 5);
        Value vb(true);
        assert(vb.type == DataType::BOOL && vb.b == true);
        Value vs(std::string("hello"));
        assert(vs.type == DataType::STRING && vs.s == "hello");
        Value vt = Value::token("tok");
        assert(vt.type == DataType::TOKEN && vt.s == "tok");
    }

    std::cout << "test_datatype: " << (failures ? "FAIL" : "PASS") << std::endl;
    return failures;
}
