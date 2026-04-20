#include <drogon/drogon.h>
#include "LogicEngine.h"
#include "GraphController.h"
#include "ExecuteEngine/ExecuteEngine.h"

int main() {
    auto engine = std::make_unique<LogicEngine>();

    // --- Build LLM Auto-Regressive Circuit ---
    // StrConst → Str2Stream ──┐
    //                          ├→ ContextBuild → LLMInfer → Token2Str
    //    Register[TOKEN_STREAM]┘       │
    //         ↑                        ↓
    //         └───────── TokenAccum ←──┘

    auto [hist_cur, hist_nxt] = engine->add_register(
        DataType::TOKEN_STREAM, Value::token_stream(std::vector<std::string>{}));

    int w_sysprompt = engine->add_wire(DataType::STRING);
    int w_sysstream = engine->add_wire(DataType::TOKEN_STREAM);
    int w_context   = engine->add_wire(DataType::CONTEXT_BUFFER);
    int w_token     = engine->add_wire(DataType::TOKEN);
    int w_output    = engine->add_wire(DataType::STRING);

    engine->add_node(std::make_unique<StringConstantNode>("You are a helpful AI."), {}, {w_sysprompt});
    engine->add_node(std::make_unique<StringToTokenStreamNode>(), {w_sysprompt}, {w_sysstream});
    engine->add_node(std::make_unique<ContextBuildNode>(2), std::vector<int>{w_sysstream, hist_cur}, std::vector<int>{w_context});
    engine->add_node(std::make_unique<LLMInferNode>(), {w_context}, {w_token});
    engine->add_node(std::make_unique<TokenAccumNode>(), std::vector<int>{hist_cur, w_token}, std::vector<int>{hist_nxt});
    engine->add_node(std::make_unique<TokenToStringNode>(), {w_token}, {w_output});

    engine->compile();

    auto cfg = ExecuteEngine::load_config("config.json");

    auto exec = std::make_unique<ExecuteEngine>(std::move(engine), cfg);

    GraphController::setEngine(exec->get_logic());

    exec->start_cli();

    std::cout << "Starting LLM Circuit UI Server on http://localhost:8080" << std::endl;

    drogon::app()
        .setDocumentRoot("../web")
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .run();

    return 0;
}
