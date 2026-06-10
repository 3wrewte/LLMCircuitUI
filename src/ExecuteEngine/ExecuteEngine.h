#pragma once

#include "../LogicEngine.h"
#include "InferenceBackend.h"
#include "LlamaBackend.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <sstream>
#include <fstream>
#include <iostream>
#include <json/json.h>

class ExecuteEngine {
    std::unique_ptr<LogicEngine> logic;
    std::unique_ptr<InferenceBackend> backend;

    float default_temperature = 0.7f;
    int tick_count = 0;
    bool verbose = true;

    std::mutex mtx;
    std::atomic<bool> running{true};
    std::atomic<bool> run_loop_active{false};

    void configure_llm_nodes() {
        auto* be = backend.get();
        for (auto& inst : logic->get_instances_mut()) {
            if (auto* llm = dynamic_cast<LLMInferNode*>(inst.logic.get())) {
                float temp = default_temperature;
                llm->infer_callback = [be, temp](
                    const std::vector<int>& ctx) -> std::pair<std::string,int> {
                    if (!be || !be->is_ready()) {
                        static int n = 0;
                        std::string stub = "[stub_" + std::to_string(n++) + "]";
                        return {stub, -1};
                    }
                    auto [id, text] = be->infer_step(ctx, temp);
                    return {text, id};
                };
            }
            if (auto* tok = dynamic_cast<TokenizerNode*>(inst.logic.get())) {
                tok->tokenize_callback = [be](const std::string& s) {
                    return be ? be->tokenize(s, false) : std::vector<int>{};
                };
            }
            if (auto* dtk = dynamic_cast<DetokenizeNode*>(inst.logic.get())) {
                dtk->detokenize_callback = [be](int id) {
                    return be ? be->detokenize(id) : std::string();
                };
            }
            if (auto* sts = dynamic_cast<TokenStreamToStringNode*>(inst.logic.get())) {
                sts->detokenize_callback = [be](int id) {
                    return be ? be->detokenize(id) : std::string("[" + std::to_string(id) + "]");
                };
            }
            if (auto* sst = dynamic_cast<ShowTokenStreamNode*>(inst.logic.get())) {
                sst->detokenize_callback = [be](int id) {
                    return be ? be->detokenize(id) : std::string("[" + std::to_string(id) + "]");
                };
            }
            if (auto* sts2 = dynamic_cast<StringToTokenStreamNode*>(inst.logic.get())) {
                sts2->tokenize_callback = [be](const std::string& s) {
                    return be ? be->tokenize(s, false) : std::vector<int>{};
                };
            }
        }
    }

    void print_value(const Value& v) {
        switch (v.type) {
            case DataType::INT:  std::cout << v.i; break;
            case DataType::BOOL: std::cout << (v.b ? "true" : "false"); break;
            case DataType::STRING: std::cout << "\"" << v.s << "\""; break;
            case DataType::TOKEN: std::cout << "tok(\"" << v.s << "\")"; break;
            case DataType::TOKEN_ID: std::cout << "tkid(" << v.i << ")"; break;
            case DataType::TOKEN_STREAM:
                std::cout << "stream[" << v.token_ids.size() << "]{";
                for (size_t i = 0; i < v.token_ids.size() && i < 5; ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << v.token_ids[i];
                }
                if (v.token_ids.size() > 5) std::cout << ",...";
                std::cout << "}";
                break;
            case DataType::CONTEXT_BUFFER:
                std::cout << "ctx[" << v.token_ids.size() << "]{";
                for (size_t i = 0; i < v.token_ids.size() && i < 8; ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << v.token_ids[i];
                }
                if (v.token_ids.size() > 8) std::cout << ",...";
                std::cout << "}";
                break;
        }
    }

    void print_tick_header() {
        std::cout << "\n╔══ TICK " << tick_count << " ══╗" << std::endl;
    }

    void print_tick_footer() {
        std::cout << "╚════════════╝" << std::endl;
        for (auto& [id, reg] : logic->get_registers()) {
            std::cout << "  Reg[" << id << "] = ";
            print_value(reg.val);
            std::cout << std::endl;
        }
    }

    void print_wires() {
        std::cout << "=== Wires ===" << std::endl;
        for (auto& inst : logic->get_instances()) {
            for (int i = 0; i < (int)inst.out_wires.size(); ++i) {
                auto wid = inst.out_wires[i];
                std::cout << "  w" << wid << " = ";
                print_value(logic->read_wire(wid));
                std::cout << std::endl;
            }
        }
    }

    void print_nodes() {
        std::cout << "=== Nodes ===" << std::endl;
        auto& instances = logic->get_instances();
        int reg_base = (int)instances.size();
        for (int i = 0; i < (int)instances.size(); ++i) {
            auto& inst = instances[i];
            std::cout << "  [" << i << "] " << inst.logic->get_name();
            Json::Value cfg;
            inst.logic->save_config(cfg);
            if (!cfg.empty()) {
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                std::cout << " " << Json::writeString(wb, cfg);
            }
            std::cout << std::endl;
        }
        int r_idx = 0;
        for (auto& [id, reg] : logic->get_registers()) {
            std::cout << "  [" << (reg_base + r_idx) << "] Register ";
            print_value(reg.val);
            std::cout << std::endl;
            r_idx++;
        }
    }

    void find_and_set_user_input(const std::string& val) {
        bool found = false;
        for (auto& inst : logic->get_instances_mut()) {
            auto* ui = dynamic_cast<UserInputNode*>(inst.logic.get());
            if (!ui) continue;
            ui->value = val;
            found = true;
            std::cout << "  UserInput set to \"" << val << "\"" << std::endl;
        }
        if (!found) std::cout << "  No UserInput node in circuit" << std::endl;
    }

    void do_load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "  Cannot open: " << path << std::endl;
            return;
        }
        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string errors;
        if (!Json::parseFromStream(rb, f, &root, &errors)) {
            std::cerr << "  Parse error: " << errors << std::endl;
            return;
        }
        auto new_engine = LogicEngine::deserialize(root);
        new_engine->compile();
        if (new_engine->has_errors()) {
            std::cerr << "  Compile errors:" << std::endl;
            for (auto& e : new_engine->get_compile_errors()) {
                std::cerr << "    " << e.severity << ": " << e.message << std::endl;
            }
        }
        logic = std::move(new_engine);
        configure_llm_nodes();
        tick_count = 0;
        if (backend) backend->reset();
        std::cout << "  Loaded: " << path << " ("
                  << logic->get_instances().size() << " nodes, "
                  << logic->get_registers().size() << " regs)" << std::endl;
    }

    void do_save(const std::string& path) {
        auto json = logic->serialize();
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "  ";
        std::ofstream f(path);
        f << Json::writeString(wb, json);
        f.close();
        std::cout << "  Saved: " << path << std::endl;
    }

    void cli_loop() {
        std::cout << "\n=== LLM Circuit CLI ===" << std::endl;
        std::cout << "Type 'help' for commands" << std::endl;
        std::cout << "> " << std::flush;

        std::string line;
        while (running && std::getline(std::cin, line)) {
            if (line.empty()) {
                std::cout << "> " << std::flush;
                continue;
            }

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            std::lock_guard<std::mutex> lock(mtx);

            if (cmd == "tick" || cmd == "t") {
                int n = 1;
                iss >> n;
                for (int i = 0; i < n && running; ++i) {
                    tick_count++;
                    if (verbose) print_tick_header();
                    logic->tick();
                    if (verbose) print_tick_footer();
                }
            } else if (cmd == "run" || cmd == "r") {
                int max_ticks = 1000;
                iss >> max_ticks;
                for (int i = 0; i < max_ticks && running; ++i) {
                    tick_count++;
                    if (verbose) print_tick_header();
                    logic->tick();
                    if (verbose) print_tick_footer();
                    if (logic->is_break()) {
                        std::cout << "  ** BREAK signal **" << std::endl;
                        break;
                    }
                }
            } else if (cmd == "wires" || cmd == "w") {
                print_wires();
            } else if (cmd == "nodes" || cmd == "n") {
                print_nodes();
            } else if (cmd == "regs") {
                for (auto& [id, reg] : logic->get_registers()) {
                    std::cout << "  Reg[" << id << "] = ";
                    print_value(reg.val);
                    std::cout << std::endl;
                }
            } else if (cmd == "load") {
                std::string path;
                iss >> path;
                do_load(path);
            } else if (cmd == "save") {
                std::string path;
                if (iss >> path) {
                    do_save(path);
                } else {
                    std::cout << "  Usage: save <path>" << std::endl;
                }
            } else if (cmd == "input" || cmd == "i") {
                std::string val;
                std::getline(iss, val);
                while (!val.empty() && val.front() == ' ') val.erase(val.begin());
                if (val.empty()) {
                    std::cout << "  Usage: input <text>" << std::endl;
                } else {
                    find_and_set_user_input(val);
                }
            } else if (cmd == "reset") {
                reset_circuit();
            } else if (cmd == "compile") {
                logic->compile();
                for (auto& e : logic->get_compile_errors()) {
                    std::cout << "  " << e.severity << ": " << e.message << std::endl;
                }
                if (!logic->has_errors()) std::cout << "  Compile OK" << std::endl;
            } else if (cmd == "list" || cmd == "ls") {
                auto kinds = NodeFactory::list_kinds();
                std::cout << "  Available node kinds:" << std::endl;
                for (auto& k : kinds) std::cout << "    " << k << std::endl;
            } else if (cmd == "verbose" || cmd == "v") {
                std::string arg;
                iss >> arg;
                if (arg == "off" || arg == "0") verbose = false;
                else verbose = true;
                std::cout << "  verbose=" << (verbose ? "on" : "off") << std::endl;
            } else if (cmd == "quit" || cmd == "q") {
                running = false;
                run_loop_active = false;
                std::cout << "Shutting down..." << std::endl;
                drogon::app().quit();
                break;
            } else {
                std::cout << "Unknown command: " << cmd << " (type 'help')" << std::endl;
            }

            std::cout << "> " << std::flush;
        }
    }

public:
    struct Config {
        std::string model_path;
        int n_gpu_layers = 0;
        float temperature = 0.7f;
        bool verbose = true;
    };

    ExecuteEngine(std::unique_ptr<LogicEngine> eng, Config cfg)
        : logic(std::move(eng)),
          default_temperature(cfg.temperature),
          verbose(cfg.verbose) {

        if (!cfg.model_path.empty()) {
            backend = std::make_unique<LlamaBackend>(cfg.model_path, cfg.n_gpu_layers);
            std::cout << "[ExecuteEngine] Llama backend configured" << std::endl;
        } else {
            std::cout << "[ExecuteEngine] No model path — running in STUB mode" << std::endl;
        }

        configure_llm_nodes();
    }

    ~ExecuteEngine() {
        running = false;
        run_loop_active = false;
    }

    LogicEngine* get_logic() { return logic.get(); }
    int get_tick_count() const { return tick_count; }

    // --- Single tick (thread-safe) ---
    void run_tick() {
        std::lock_guard<std::mutex> lock(mtx);
        tick_count++;
        logic->tick();
    }

    // --- Run loop ---
    void start_run_loop(int max_ticks = 1000) {
        if (run_loop_active) return;
        run_loop_active = true;
        std::thread([this, max_ticks]() {
            for (int i = 0; i < max_ticks && run_loop_active && running; ++i) {
                std::lock_guard<std::mutex> lock(mtx);
                tick_count++;
                logic->tick();
                if (logic->is_break()) break;
            }
            run_loop_active = false;
        }).detach();
    }

    void stop_run_loop() { run_loop_active = false; }
    bool is_run_active() const { return run_loop_active; }

    // --- Reset ---
    void reset_circuit() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& [id, reg] : logic->get_registers_mut()) {
            switch (reg.val.type) {
                case DataType::INT: reg.val = Value(0); break;
                case DataType::BOOL: reg.val = Value(false); break;
                case DataType::STRING: reg.val = Value(std::string("")); break;
                case DataType::TOKEN: reg.val = Value::token(""); break;
                case DataType::TOKEN_ID: reg.val = Value::token_id(-1); break;
                case DataType::TOKEN_STREAM: reg.val = Value::token_stream(std::vector<int>{}); break;
                case DataType::CONTEXT_BUFFER: reg.val = Value::context_buffer(std::vector<int>{}); break;
            }
        }
        tick_count = 0;
        if (backend) backend->reset();
        std::cout << "  Registers and KV cache reset" << std::endl;
    }

    // --- Replace engine (for undo/redo) ---
    LogicEngine* replace_logic(std::unique_ptr<LogicEngine> new_engine) {
        std::lock_guard<std::mutex> lock(mtx);
        logic = std::move(new_engine);
        configure_llm_nodes();
        return logic.get();
    }

    void reconfigure_llm() {
        std::lock_guard<std::mutex> lock(mtx);
        configure_llm_nodes();
    }

    // --- Input ---
    void set_user_input(const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& inst : logic->get_instances_mut()) {
            auto* ui = dynamic_cast<UserInputNode*>(inst.logic.get());
            if (ui) ui->value = text;
        }
    }

    void start_cli() {
        std::thread cli_thread([this]() { cli_loop(); });
        cli_thread.detach();
    }

    bool is_running() const { return running; }

    static Config load_config(const std::string& path) {
        Config cfg;
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[ExecuteEngine] config not found: " << path
                      << " — using defaults / stub mode" << std::endl;
            return cfg;
        }

        Json::CharReaderBuilder rb;
        Json::Value root;
        std::string errors;
        if (!Json::parseFromStream(rb, f, &root, &errors)) {
            std::cerr << "[ExecuteEngine] config parse error: " << errors << std::endl;
            return cfg;
        }

        cfg.model_path = root.get("model_path", "").asString();
        cfg.n_gpu_layers = root.get("n_gpu_layers", 0).asInt();
        cfg.temperature = root.get("temperature", 0.7).asFloat();
        cfg.verbose = root.get("verbose", true).asBool();

        return cfg;
    }
};
