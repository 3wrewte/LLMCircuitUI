#include "../src/LogicEngine.h"
#include <cassert>
#include <iostream>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL @" << __LINE__ << ": " #cond << std::endl; failures++; } } while(0)

static std::pair<int,int> build_ar_circuit(LogicEngine& eng) {
    auto [hist_cur, hist_nxt] = eng.add_register(
        DataType::TOKEN_STREAM, Value::token_stream(std::vector<int>{}));

    int w_sys     = eng.add_wire(DataType::STRING);
    int w_sys_tok = eng.add_wire(DataType::TOKEN_STREAM);
    int w_usr     = eng.add_wire(DataType::STRING);
    int w_usr_tok = eng.add_wire(DataType::TOKEN_STREAM);
    int w_ctx     = eng.add_wire(DataType::CONTEXT_BUFFER);
    int w_token   = eng.add_wire(DataType::TOKEN);
    int w_tkid    = eng.add_wire(DataType::TOKEN_ID);
    int w_out     = eng.add_wire(DataType::STRING);
    int w_stop    = eng.add_wire(DataType::BOOL);

    eng.add_node(std::make_unique<StringConstantNode>("System prompt"), {}, {w_sys});
    eng.add_node(std::make_unique<UserInputNode>(), {}, {w_usr});
    eng.add_node(std::make_unique<TokenizerNode>(), {w_sys}, {w_sys_tok});
    eng.add_node(std::make_unique<TokenizerNode>(), {w_usr}, {w_usr_tok});
    eng.add_node(std::make_unique<ContextBuildNode>(3),
        std::vector<int>{w_sys_tok, w_usr_tok, hist_cur}, std::vector<int>{w_ctx});
    eng.add_node(std::make_unique<LLMInferNode>(), {w_ctx}, std::vector<int>{w_token, w_tkid});
    eng.add_node(std::make_unique<TokenAccumNode>(),
        std::vector<int>{hist_cur, w_tkid}, std::vector<int>{hist_nxt});
    eng.add_node(std::make_unique<DetokenizeNode>(), {w_tkid}, {w_out});
    eng.add_node(std::make_unique<StopConditionNode>(), {w_token}, {w_stop});

    eng.compile();
    return {w_token, w_tkid};
}

void test_compile() {
    auto eng = std::make_unique<LogicEngine>();
    auto [w_token, w_tkid] = build_ar_circuit(*eng);

    for (auto& e : eng->get_compile_errors()) {
        if (e.severity == "error") {
            std::cerr << "  ERROR: " << e.message << std::endl;
            CHECK(false);
        }
    }
    CHECK(!eng->has_errors());
    std::cout << "  test_compile: OK" << std::endl;
}

void test_stub_tick() {
    auto eng = std::make_unique<LogicEngine>();
    auto [w_token, w_tkid] = build_ar_circuit(*eng);

    CHECK(!eng->has_errors());

    eng->tick();
    auto tok = eng->read_wire(w_token);
    auto id  = eng->read_wire(w_tkid);
    CHECK(tok.type == DataType::TOKEN);
    CHECK(id.type == DataType::TOKEN_ID);
    CHECK(tok.s == "[stub_0]");
    CHECK(id.i == -1);

    eng->tick();
    auto tok2 = eng->read_wire(w_token);
    CHECK(tok2.s == "[stub_1]");
    std::cout << "  test_stub_tick: OK" << std::endl;
}

void test_full_tick_loop() {
    auto eng = std::make_unique<LogicEngine>();
    auto [w_token, w_tkid] = build_ar_circuit(*eng);

    int ticks = 0;
    while (ticks < 5) {
        eng->tick();
        ticks++;
        auto tok = eng->read_wire(w_token);
        CHECK(tok.type == DataType::TOKEN);
        CHECK(!tok.s.empty());
    }

    // Verify register accumulated tokens
    for (auto& [id, reg] : eng->get_registers()) {
        CHECK(reg.val.type == DataType::TOKEN_STREAM);
        CHECK((int)reg.val.token_ids.size() == ticks);  // one new token per tick
    }
    std::cout << "  test_full_tick_loop: OK" << std::endl;
}

void test_new_node_schemas() {
    auto tok = std::make_unique<TokenizerNode>();
    CHECK(tok->get_input_schema().size() == 1);
    CHECK(tok->get_input_schema()[0] == DataType::STRING);
    CHECK(tok->get_output_schema().size() == 1);
    CHECK(tok->get_output_schema()[0] == DataType::TOKEN_STREAM);

    auto dtok = std::make_unique<DetokenizeNode>();
    CHECK(dtok->get_input_schema().size() == 1);
    CHECK(dtok->get_input_schema()[0] == DataType::TOKEN_ID);
    CHECK(dtok->get_output_schema().size() == 1);
    CHECK(dtok->get_output_schema()[0] == DataType::STRING);

    auto llm = std::make_unique<LLMInferNode>();
    CHECK(llm->get_output_schema().size() == 2);
    CHECK(llm->get_output_schema()[0] == DataType::TOKEN);
    CHECK(llm->get_output_schema()[1] == DataType::TOKEN_ID);

    auto accum = std::make_unique<TokenAccumNode>();
    CHECK(accum->get_input_schema()[0] == DataType::TOKEN_STREAM);
    CHECK(accum->get_input_schema()[1] == DataType::TOKEN_ID);

    std::cout << "  test_new_node_schemas: OK" << std::endl;
}

int main() {
    test_compile();
    test_stub_tick();
    test_full_tick_loop();
    test_new_node_schemas();

    std::cout << "test_nodes_ir: " << (failures ? "FAIL" : "PASS") << std::endl;
    return failures;
}
