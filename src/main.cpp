#include <drogon/drogon.h>
#include "LogicEngine.h"
#include "GraphController.h"
#include "ExecuteEngine/ExecuteEngine.h"
#include <fstream>

static std::unique_ptr<LogicEngine> build_demo_circuit() {
    auto engine = std::make_unique<LogicEngine>();

    // Auto-regressive LLM loop with user input:
    //
    // StrConst(system prompt) → Tokenizer ──┐
    //                                        ├→ ContextBuild → LLMInfer →┬→ TokenAccum → Reg[TOKEN_STREAM]
    // UserInput → Tokenizer ───────────────┘                            ├→ Detokenize → StopCond → SYS_BREAK
    //                                                                   └→ TOKEN_ID → TokenAccum

    auto [hist_cur, hist_nxt] = engine->add_register(
        DataType::TOKEN_STREAM, Value::token_stream(std::vector<int>{}));

    int w_sysprompt  = engine->add_wire(DataType::STRING);
    int w_sysstream  = engine->add_wire(DataType::TOKEN_STREAM);
    int w_userinput  = engine->add_wire(DataType::STRING);
    int w_userstream = engine->add_wire(DataType::TOKEN_STREAM);
    int w_context    = engine->add_wire(DataType::CONTEXT_BUFFER);
    int w_token      = engine->add_wire(DataType::TOKEN);
    int w_token_id   = engine->add_wire(DataType::TOKEN_ID);
    int w_output     = engine->add_wire(DataType::STRING);
    int w_stop       = engine->add_wire(DataType::BOOL);

    engine->add_node(std::make_unique<StringConstantNode>("You are a helpful AI."), {}, {w_sysprompt});
    engine->add_node(std::make_unique<UserInputNode>(), {}, {w_userinput});
    engine->add_node(std::make_unique<TokenizerNode>(), {w_sysprompt}, {w_sysstream});
    engine->add_node(std::make_unique<TokenizerNode>(), {w_userinput}, std::vector<int>{w_userstream});
    engine->add_node(std::make_unique<ContextBuildNode>(3), std::vector<int>{w_sysstream, w_userstream, hist_cur}, std::vector<int>{w_context});
    engine->add_node(std::make_unique<LLMInferNode>(), {w_context}, std::vector<int>{w_token, w_token_id});
    engine->add_node(std::make_unique<TokenAccumNode>(), std::vector<int>{hist_cur, w_token_id}, std::vector<int>{hist_nxt});
    engine->add_node(std::make_unique<DetokenizeNode>(), {w_token_id}, {w_output});
    engine->add_node(std::make_unique<StopConditionNode>(), {w_token}, {w_stop});

    engine->compile();

    return engine;
}

int main() {
    std::unique_ptr<LogicEngine> engine;

    std::ifstream cfile("circuit.json");
    if (cfile.is_open()) {
        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string errors;
        if (Json::parseFromStream(rb, cfile, &root, &errors)) {
            engine = LogicEngine::deserialize(root);
            engine->compile();
            std::cout << "[main] Loaded circuit.json ("
                      << engine->get_instances().size() << " nodes)" << std::endl;
        } else {
            std::cerr << "[main] circuit.json parse error: " << errors << std::endl;
            std::cerr << "[main] Falling back to demo circuit" << std::endl;
            engine = build_demo_circuit();
        }
    } else {
        std::cout << "[main] No circuit.json found, building demo circuit" << std::endl;
        engine = build_demo_circuit();
    }

    auto cfg = ExecuteEngine::load_config("config.json");
    auto exec = std::make_unique<ExecuteEngine>(std::move(engine), cfg);

    GraphController::setEngine(exec->get_logic());
    GraphController::setExecuteEngine(exec.get());

    exec->start_cli();

    std::cout << "Starting LLM Circuit UI Server on http://localhost:8080" << std::endl;

    drogon::app()
        .setDocumentRoot("../web")
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .run();

    return 0;
}
