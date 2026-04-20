#pragma once

#include "../LogicEngine.h"
#include "InferenceBackend.h"
#include "APIBackend.h"
#include "TokenCache.h"
#include <memory>
#include <mutex>
#include <thread>
#include <sstream>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <curl/curl.h>

class ExecuteEngine {
    std::unique_ptr<LogicEngine> logic;
    std::unique_ptr<InferenceBackend> backend;
    TokenCache cache;

    int batch_size = 8;
    float default_temperature = 0.7f;
    int tick_count = 0;
    bool verbose = true;
    std::string current_circuit_path;

    std::mutex mtx;
    bool running = true;

    void configure_llm_nodes() {
        for (auto& inst : logic->get_instances_mut()) {
            auto* llm = dynamic_cast<LLMInferNode*>(inst.logic.get());
            if (!llm) continue;

            auto* be = backend.get();
            auto* ca = &cache;
            int bs = batch_size;
            float temp = default_temperature;
            bool vb = verbose;

            llm->infer_callback = [be, ca, bs, temp, vb](
                const std::vector<std::string>& context) -> std::string {

                auto cached = ca->try_get(context);
                if (cached) {
                    if (vb) std::cout << "  [CACHE HIT]" << std::endl;
                    return *cached;
                }

                if (!be || !be->is_ready()) {
                    static int stub_counter = 0;
                    return "[stub_" + std::to_string(stub_counter++) + "]";
                }

                if (vb) std::cout << "  [CACHE MISS] calling API batch=" << bs << std::endl;
                auto tokens = be->infer(context, bs, temp);

                if (tokens.empty()) {
                    return "[api_error]";
                }

                if ((int)tokens.size() > 1) {
                    ca->store_batch(context, tokens);
                    if (vb) {
                        std::cout << "  [BATCH] stored " << tokens.size()
                                  << " tokens in cache" << std::endl;
                    }
                }

                return tokens[0];
            };
        }
    }

    void print_value(const Value& v) {
        switch (v.type) {
            case DataType::INT:  std::cout << v.i; break;
            case DataType::BOOL: std::cout << (v.b ? "true" : "false"); break;
            case DataType::STRING: std::cout << "\"" << v.s << "\""; break;
            case DataType::TOKEN: std::cout << "tok(\"" << v.s << "\")"; break;
            case DataType::TOKEN_STREAM:
                std::cout << "stream[" << v.tokens.size() << "]{";
                for (size_t i = 0; i < v.tokens.size() && i < 5; ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << "\"" << v.tokens[i] << "\"";
                }
                if (v.tokens.size() > 5) std::cout << ",...";
                std::cout << "}";
                break;
            case DataType::CONTEXT_BUFFER:
                std::cout << "ctx[" << v.tokens.size() << "]{";
                for (size_t i = 0; i < v.tokens.size() && i < 3; ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << "\"" << v.tokens[i].substr(0, 30) << (v.tokens[i].size() > 30 ? "..." : "") << "\"";
                }
                if (v.tokens.size() > 3) std::cout << ",...";
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
            std::cout << "  UserInput[" << ui->value << "] set to \"" << val << "\"" << std::endl;
        }
        if (!found) {
            std::cout << "  No UserInput node in circuit" << std::endl;
        }
    }

    void reset_registers() {
        for (auto& [id, reg] : logic->get_registers_mut()) {
            switch (reg.val.type) {
                case DataType::INT: reg.val = Value(0); break;
                case DataType::BOOL: reg.val = Value(false); break;
                case DataType::STRING: reg.val = Value(std::string("")); break;
                case DataType::TOKEN: reg.val = Value::token(""); break;
                case DataType::TOKEN_STREAM: reg.val = Value::token_stream(std::vector<std::string>{}); break;
                case DataType::CONTEXT_BUFFER: reg.val = Value::context_buffer(std::vector<std::string>{}); break;
            }
        }
        tick_count = 0;
        cache.invalidate_all();
        std::cout << "  Registers reset, cache cleared" << std::endl;
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
        current_circuit_path = path;
        tick_count = 0;
        cache.invalidate_all();
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
        current_circuit_path = path;
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
            } else if (cmd == "cache" || cmd == "c") {
                cache.print_stats();
            } else if (cmd == "load") {
                std::string path;
                iss >> path;
                do_load(path);
            } else if (cmd == "save") {
                std::string path;
                if (iss >> path) {
                    do_save(path);
                } else if (!current_circuit_path.empty()) {
                    do_save(current_circuit_path);
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
                reset_registers();
            } else if (cmd == "compile") {
                logic->compile();
                for (auto& e : logic->get_compile_errors()) {
                    std::cout << "  " << e.severity << ": " << e.message << std::endl;
                }
                if (!logic->has_errors()) {
                    std::cout << "  Compile OK" << std::endl;
                }
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
            } else if (cmd == "help" || cmd == "h") {
                std::cout << "  tick [N]       — run N ticks (default 1)" << std::endl;
                std::cout << "  run [N]        — run up to N ticks (default 1000), stop on BREAK" << std::endl;
                std::cout << "  input <text>   — set value on UserInput node(s)" << std::endl;
                std::cout << "  wires          — show all wire values" << std::endl;
                std::cout << "  nodes          — show all nodes" << std::endl;
                std::cout << "  regs           — show register values" << std::endl;
                std::cout << "  cache          — show token cache stats" << std::endl;
                std::cout << "  load <file>    — load circuit from JSON" << std::endl;
                std::cout << "  save [file]    — save circuit to JSON" << std::endl;
                std::cout << "  reset          — reset registers + clear cache" << std::endl;
                std::cout << "  compile        — re-run compile passes" << std::endl;
                std::cout << "  list           — list available node kinds" << std::endl;
                std::cout << "  verbose [on|off] — toggle debug output" << std::endl;
                std::cout << "  quit           — shutdown" << std::endl;
            } else if (cmd == "quit" || cmd == "q") {
                running = false;
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
        std::string api_key;
        std::string base_url = "https://api.siliconflow.cn/v1";
        std::string model = "Qwen/Qwen3-8B";
        int batch_size = 8;
        float temperature = 0.7f;
        bool verbose = true;
    };

    ExecuteEngine(std::unique_ptr<LogicEngine> eng, Config cfg)
        : logic(std::move(eng)),
          batch_size(cfg.batch_size),
          default_temperature(cfg.temperature),
          verbose(cfg.verbose) {

        curl_global_init(CURL_GLOBAL_DEFAULT);

        if (!cfg.api_key.empty() && cfg.api_key != "PASTE_YOUR_API_KEY_HERE") {
            backend = std::make_unique<APIBackend>(
                cfg.api_key, cfg.base_url, cfg.model);
            std::cout << "[ExecuteEngine] API backend configured" << std::endl;
        } else {
            std::cout << "[ExecuteEngine] No API key — running in STUB mode" << std::endl;
        }

        configure_llm_nodes();
    }

    ~ExecuteEngine() {
        running = false;
        curl_global_cleanup();
    }

    LogicEngine* get_logic() { return logic.get(); }

    void run_tick() {
        std::lock_guard<std::mutex> lock(mtx);
        tick_count++;
        logic->tick();
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

        cfg.api_key = root.get("api_key", "").asString();
        cfg.base_url = root.get("base_url", "https://api.siliconflow.cn/v1").asString();
        cfg.model = root.get("model", "Qwen/Qwen3-8B").asString();
        cfg.batch_size = root.get("batch_size", 8).asInt();
        cfg.temperature = root.get("temperature", 0.7).asFloat();
        cfg.verbose = root.get("verbose", true).asBool();

        return cfg;
    }
};
