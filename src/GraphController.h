#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include "LogicEngine.h"
#include "ExecuteEngine/ExecuteEngine.h"

using namespace drogon;

class GraphController : public HttpController<GraphController> {
public:
    struct Vec2 { float x, y; };

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(GraphController::getGraph, "/api/graph", Get);
        ADD_METHOD_TO(GraphController::getNodeTypeList, "/api/node_types", Get);
        ADD_METHOD_TO(GraphController::tick, "/api/tick", Post);
        ADD_METHOD_TO(GraphController::runCircuit, "/api/run", Post);
        ADD_METHOD_TO(GraphController::stopCircuit, "/api/stop", Post);
        ADD_METHOD_TO(GraphController::resetCircuit, "/api/reset", Post);
        ADD_METHOD_TO(GraphController::syncGraph, "/api/sync", Post);
        ADD_METHOD_TO(GraphController::saveCircuit, "/api/save", Post);
        ADD_METHOD_TO(GraphController::inputText, "/api/input", Post);
        ADD_METHOD_TO(GraphController::addNode, "/api/add_node", Post);
        ADD_METHOD_TO(GraphController::addWire, "/api/add_wire", Post);
        ADD_METHOD_TO(GraphController::removeNode, "/api/remove_node", Post);
        ADD_METHOD_TO(GraphController::removeWire, "/api/remove_wire", Post);
        ADD_METHOD_TO(GraphController::undoAction, "/api/undo", Post);
        ADD_METHOD_TO(GraphController::redoAction, "/api/redo", Post);
    METHOD_LIST_END

    GraphController() {}

    static void setEngine(LogicEngine* engPtr) { engine = engPtr; }
    static void setExecuteEngine(ExecuteEngine* engPtr) { exec = engPtr; }

    // --- Undo/Redo ---

    Json::Value capture_snapshot() {
        Json::Value snap;
        snap["circuit"] = engine->serialize();
        Json::Value pos(Json::arrayValue);
        for (auto& [id, p] : node_positions) {
            Json::Value item;
            item["id"] = id;
            item["x"] = p.x;
            item["y"] = p.y;
            pos.append(item);
        }
        snap["positions"] = pos;
        return snap;
    }

    void restore_snapshot(const Json::Value& snap) {
        auto new_engine = LogicEngine::deserialize(snap["circuit"]);
        new_engine->compile();
        node_positions.clear();
        for (auto& p : snap["positions"]) {
            node_positions[p["id"].asInt()] = {p["x"].asFloat(), p["y"].asFloat()};
        }
        engine = exec->replace_logic(std::move(new_engine));
    }

    void push_undo() {
        undo_stack.push_back(capture_snapshot());
        redo_stack.clear();
        if (undo_stack.size() > 50) undo_stack.erase(undo_stack.begin());
    }

    // --- API: Get Graph ---

    void getGraph(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value root;
        root["nodes"] = Json::arrayValue;
        root["wires"] = Json::arrayValue;

        std::map<int, std::pair<int, int>> wire_drivers;

        auto& instances = engine->get_instances();
        int n_inst = (int)instances.size();

        for (int i = 0; i < n_inst; ++i) {
            auto& inst = instances[i];
            Json::Value nodeJson;
            nodeJson["id"] = i;
            nodeJson["name"] = inst.logic->get_name();
            nodeJson["kind"] = inst.logic->get_kind();
            nodeJson["x"] = node_positions.count(i) ? node_positions[i].x : (float)(i % 5 * 220 + 80);
            nodeJson["y"] = node_positions.count(i) ? node_positions[i].y : (float)(i / 5 * 200 + 80);

            nodeJson["inputs"] = Json::arrayValue;
            for (auto type : inst.logic->get_input_schema()) nodeJson["inputs"].append((int)type);
            nodeJson["outputs"] = Json::arrayValue;
            for (auto type : inst.logic->get_output_schema()) nodeJson["outputs"].append((int)type);

            for (int p = 0; p < (int)inst.out_wires.size(); ++p) {
                wire_drivers[inst.out_wires[p]] = {i, p};
            }

            Json::Value config;
            inst.logic->save_config(config);
            nodeJson["config"] = config;
            root["nodes"].append(nodeJson);
        }

        int reg_base = n_inst;
        auto& registers = engine->get_registers();
        int r_idx = 0;
        for (auto const& [reg_id, reg] : registers) {
            int ui_id = reg_base + r_idx;
            Json::Value regJson;
            regJson["id"] = ui_id;
            regJson["name"] = "Register";
            regJson["kind"] = "Register";
            regJson["x"] = node_positions.count(ui_id) ? node_positions[ui_id].x : (float)(r_idx * 220 + 80);
            regJson["y"] = node_positions.count(ui_id) ? node_positions[ui_id].y : 500.0f;

            regJson["inputs"] = Json::arrayValue;
            regJson["inputs"].append((int)reg.val.type);
            regJson["outputs"] = Json::arrayValue;
            regJson["outputs"].append((int)reg.val.type);

            wire_drivers[reg.cur_wire] = {ui_id, 0};

            Json::Value config;
            if (reg.val.type == DataType::INT) config["current"] = reg.val.i;
            else if (reg.val.type == DataType::BOOL) config["current"] = reg.val.b;
            else if (reg.val.type == DataType::STRING) config["current"] = reg.val.s;
            else if (reg.val.type == DataType::TOKEN) config["current"] = reg.val.s;
            else if (reg.val.type == DataType::TOKEN_STREAM || reg.val.type == DataType::CONTEXT_BUFFER) {
                Json::Value arr(Json::arrayValue);
                for (size_t t = 0; t < reg.val.token_ids.size() && t < 20; ++t) arr.append(reg.val.token_ids[t]);
                config["current"] = arr;
            }
            regJson["config"] = config;
            root["nodes"].append(regJson);
            r_idx++;
        }

        // System nodes (CLK, RST, BREAK)
        int sys_base = n_inst + (int)registers.size();
        struct SysNode { const char* name; int wire_id; bool is_output; DataType type; float x, y; };
        SysNode sys_nodes[] = {
            {"CLK",  SYS_CLK,   true,  DataType::INT,  80, 30},
            {"RST",  SYS_RST,   true,  DataType::BOOL, 300, 30},
            {"BREAK", SYS_BREAK, false, DataType::BOOL, 520, 30},
        };

        for (int si = 0; si < 3; ++si) {
            auto& sn = sys_nodes[si];
            int sys_id = sys_base + si;
            Json::Value sysJson;
            sysJson["id"] = sys_id;
            sysJson["name"] = sn.name;
            sysJson["kind"] = "System";
            sysJson["system"] = true;
            sysJson["x"] = node_positions.count(sys_id) ? node_positions[sys_id].x : sn.x;
            sysJson["y"] = node_positions.count(sys_id) ? node_positions[sys_id].y : sn.y;

            if (sn.is_output) {
                sysJson["inputs"] = Json::arrayValue;
                Json::Value outs(Json::arrayValue); outs.append((int)sn.type);
                sysJson["outputs"] = outs;
                wire_drivers[sn.wire_id] = {sys_id, 0};
            } else {
                Json::Value ins(Json::arrayValue); ins.append((int)sn.type);
                sysJson["inputs"] = ins;
                sysJson["outputs"] = Json::arrayValue;
            }

            Json::Value config;
            if (sn.wire_id == SYS_BREAK) {
                config["active"] = engine->read_wire(SYS_BREAK).b;
            } else if (sn.wire_id == SYS_CLK) {
                config["tick"] = engine->read_wire(SYS_CLK).i;
            }
            sysJson["config"] = config;
            root["nodes"].append(sysJson);
        }

        // Generate wires: Instance inputs
        for (int i = 0; i < n_inst; ++i) {
            auto& inst = instances[i];
            for (int p = 0; p < (int)inst.in_wires.size(); ++p) {
                int w_id = inst.in_wires[p];
                if (wire_drivers.count(w_id)) {
                    Json::Value wireJson;
                    wireJson["fromNode"] = wire_drivers[w_id].first;
                    wireJson["fromPort"] = wire_drivers[w_id].second;
                    wireJson["toNode"] = i;
                    wireJson["toPort"] = p;
                    root["wires"].append(wireJson);
                }
            }
        }

        // Generate wires: Register inputs (nxt_wire) — BUG FIX
        r_idx = 0;
        for (auto const& [reg_id, reg] : registers) {
            int ui_id = reg_base + r_idx;
            if (wire_drivers.count(reg.nxt_wire)) {
                Json::Value wireJson;
                wireJson["fromNode"] = wire_drivers[reg.nxt_wire].first;
                wireJson["fromPort"] = wire_drivers[reg.nxt_wire].second;
                wireJson["toNode"] = ui_id;
                wireJson["toPort"] = 0;
                root["wires"].append(wireJson);
            }
            r_idx++;
        }

        // Generate wires: System BREAK input
        {
            int break_id = sys_base + 2;
            int bsrc = engine->get_break_source();
            if (bsrc >= 0) {
                for (int i = 0; i < n_inst; ++i) {
                    for (int p = 0; p < (int)instances[i].out_wires.size(); ++p) {
                        if (instances[i].out_wires[p] == bsrc) {
                            Json::Value wireJson;
                            wireJson["fromNode"] = i;
                            wireJson["fromPort"] = p;
                            wireJson["toNode"] = break_id;
                            wireJson["toPort"] = 0;
                            root["wires"].append(wireJson);
                        }
                    }
                }
                for (auto& [rid, reg] : engine->get_registers()) {
                    if (reg.cur_wire == bsrc) {
                        Json::Value wireJson;
                        wireJson["fromNode"] = n_inst + rid;
                        wireJson["fromPort"] = 0;
                        wireJson["toNode"] = break_id;
                        wireJson["toPort"] = 0;
                        root["wires"].append(wireJson);
                    }
                }
            }
        }

        root["tickCount"] = exec ? exec->get_tick_count() : 0;
        root["runActive"] = exec ? exec->is_run_active() : false;

        auto resp = HttpResponse::newHttpJsonResponse(root);
        callback(resp);
    }

    // --- API: Node Types ---
    void getNodeTypeList(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value root(Json::arrayValue);
        for (auto& kind : NodeFactory::list_kinds()) {
            auto comp = NodeFactory::create(kind);
            if (!comp) continue;
            Json::Value entry;
            entry["kind"] = kind;
            entry["name"] = comp->get_name();
            Json::Value ins(Json::arrayValue);
            for (auto t : comp->get_input_schema()) ins.append((int)t);
            entry["inputs"] = ins;
            Json::Value outs(Json::arrayValue);
            for (auto t : comp->get_output_schema()) outs.append((int)t);
            entry["outputs"] = outs;
            root.append(entry);
        }
        callback(HttpResponse::newHttpJsonResponse(root));
    }

    // --- API: Tick ---
    void tick(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (exec) exec->run_tick();
        callback(HttpResponse::newHttpResponse());
    }

    // --- API: Run / Stop / Reset ---
    void runCircuit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (exec) exec->start_run_loop(10000);
        callback(HttpResponse::newHttpResponse());
    }

    void stopCircuit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (exec) exec->stop_run_loop();
        callback(HttpResponse::newHttpResponse());
    }

    void resetCircuit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (exec) exec->reset_circuit();
        callback(HttpResponse::newHttpResponse());
    }

    // --- API: Sync positions + config ---
    void syncGraph(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (json) {
            for (const auto& n : (*json)["nodes"]) {
                int id = n["id"].asInt();
                node_positions[id] = {n["x"].asFloat(), n["y"].asFloat()};
                if (n.isMember("config") && n.isMember("kind")) {
                    int n_inst = (int)engine->get_instances().size();
                    if (id >= 0 && id < n_inst) {
                        auto& inst = engine->get_instances_mut()[id];
                        inst.logic->load_config(n["config"]);
                    }
                }
            }
        }
        callback(HttpResponse::newHttpResponse());
    }

    // --- API: Save ---
    void saveCircuit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        std::string path = "circuit.json";
        if (json && json->isMember("path")) path = (*json)["path"].asString();
        if (engine) {
            auto data = engine->serialize();
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "  ";
            std::ofstream f(path);
            f << Json::writeString(wb, data);
            f.close();
        }
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("Saved to " + path);
        callback(resp);
    }

    // --- API: Input ---
    void inputText(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (json && json->isMember("text") && exec) {
            exec->set_user_input((*json)["text"].asString());
        }
        callback(HttpResponse::newHttpResponse());
    }

    // --- API: Add Node ---
    void addNode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (!json) { callback(HttpResponse::newHttpResponse()); return; }

        push_undo();

        int old_n_inst = (int)engine->get_instances().size();

        std::string kind = (*json).get("kind", "").asString();
        int id = engine->create_node_with_wires(kind, (*json).get("config", Json::Value()));
        if (id >= 0) {
            // Shift positions for all IDs >= old_n_inst since register/sys IDs moved
            std::map<int, Vec2> new_positions;
            for (auto& [pid, pos] : node_positions) {
                if (pid >= old_n_inst) new_positions[pid + 1] = pos;
                else new_positions[pid] = pos;
            }
            node_positions = new_positions;

            float x = (*json).get("x", 200.0).asFloat();
            float y = (*json).get("y", 200.0).asFloat();
            node_positions[id] = {x, y};
            engine->compile();
            if (exec) exec->reconfigure_llm();
        }

        Json::Value resp;
        resp["id"] = id;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    // --- API: Add Wire ---
    void addWire(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (!json) { callback(HttpResponse::newHttpResponse()); return; }

        int fromNode = (*json)["fromNode"].asInt();
        int fromPort = (*json)["fromPort"].asInt();
        int toNode = (*json)["toNode"].asInt();
        int toPort = (*json)["toPort"].asInt();

        int n_inst = (int)engine->get_instances().size();
        int n_reg = engine->get_register_count();
        int n_sys = 3;
        int sys_base = n_inst + n_reg;

        push_undo();

        // Find source wire
        int source_wire = -1;
        if (fromNode >= 0 && fromNode < n_inst) {
            source_wire = engine->get_output_wire(fromNode, fromPort);
        } else if (fromNode >= n_inst && fromNode < n_inst + n_reg) {
            if (fromPort == 0) source_wire = engine->get_register_cur_wire(fromNode - n_inst);
        } else if (fromNode >= sys_base && fromNode < sys_base + n_sys) {
            int sys_idx = fromNode - sys_base;
            if (sys_idx == 0) source_wire = SYS_CLK;
            else if (sys_idx == 1) source_wire = SYS_RST;
        }

        if (source_wire < 0) {
            Json::Value resp; resp["ok"] = false; resp["error"] = "invalid source";
            callback(HttpResponse::newHttpJsonResponse(resp));
            return;
        }

        // Connect to target
        if (toNode >= 0 && toNode < n_inst) {
            engine->connect_input(toNode, toPort, source_wire);
        } else if (toNode >= n_inst && toNode < n_inst + n_reg) {
            engine->connect_register_input(toNode - n_inst, source_wire);
        } else if (toNode >= sys_base && toNode < sys_base + n_sys) {
            int sys_idx = toNode - sys_base;
            if (sys_idx == 2) {
                engine->set_break_source(source_wire);
            }
        }

        engine->compile();

        Json::Value resp; resp["ok"] = true;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    // --- API: Remove Node ---
    void removeNode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (!json) { callback(HttpResponse::newHttpResponse()); return; }

        int id = (*json)["id"].asInt();
        int n_inst = (int)engine->get_instances().size();

        if (id < 0 || id >= n_inst) {
            callback(HttpResponse::newHttpResponse()); return;
        }

        push_undo();

        if (engine->get_break_source() >= 0) {
            auto& inst = engine->get_instances()[id];
            for (int w : inst.out_wires) {
                if (w == engine->get_break_source()) {
                    engine->set_break_source(-1);
                    break;
                }
            }
        }

        engine->remove_node(id);

        // Shift positions for IDs > id down by 1
        std::map<int, Vec2> new_positions;
        for (auto& [pid, pos] : node_positions) {
            if (pid == id) continue;
            if (pid > id) new_positions[pid - 1] = pos;
            else new_positions[pid] = pos;
        }
        node_positions = new_positions;

        Json::Value resp; resp["ok"] = true;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    // --- API: Remove Wire ---
    void removeWire(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (!json) { callback(HttpResponse::newHttpResponse()); return; }

        int toNode = (*json)["toNode"].asInt();
        int toPort = (*json)["toPort"].asInt();

        int n_inst = (int)engine->get_instances().size();

        push_undo();

        if (toNode >= 0 && toNode < n_inst) {
            engine->disconnect_input(toNode, toPort);
        } else if (toNode >= n_inst && toNode < n_inst + engine->get_register_count()) {
            engine->disconnect_register_input(toNode - n_inst);
        } else {
            int sys_base = n_inst + engine->get_register_count();
            int n_sys = 3;
            if (toNode >= sys_base && toNode < sys_base + n_sys) {
                int sys_idx = toNode - sys_base;
                if (sys_idx == 2) {
                    engine->set_break_source(-1);
                }
            }
        }

        engine->compile();

        Json::Value resp; resp["ok"] = true;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    // --- API: Undo / Redo ---
    void undoAction(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (undo_stack.empty()) {
            Json::Value resp; resp["ok"] = false;
            callback(HttpResponse::newHttpJsonResponse(resp));
            return;
        }

        redo_stack.push_back(capture_snapshot());
        restore_snapshot(undo_stack.back());
        undo_stack.pop_back();

        Json::Value resp; resp["ok"] = true;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

    void redoAction(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (redo_stack.empty()) {
            Json::Value resp; resp["ok"] = false;
            callback(HttpResponse::newHttpJsonResponse(resp));
            return;
        }

        undo_stack.push_back(capture_snapshot());
        restore_snapshot(redo_stack.back());
        redo_stack.pop_back();

        Json::Value resp; resp["ok"] = true;
        callback(HttpResponse::newHttpJsonResponse(resp));
    }

private:
    static LogicEngine* engine;
    static ExecuteEngine* exec;
    std::map<int, Vec2> node_positions;
    std::vector<Json::Value> undo_stack;
    std::vector<Json::Value> redo_stack;
};

inline LogicEngine* GraphController::engine = nullptr;
inline ExecuteEngine* GraphController::exec = nullptr;
