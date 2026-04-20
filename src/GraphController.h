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
        ADD_METHOD_TO(GraphController::tick, "/api/tick", Post);
        ADD_METHOD_TO(GraphController::syncGraph, "/api/sync", Post);
        ADD_METHOD_TO(GraphController::saveCircuit, "/api/save", Post);
        ADD_METHOD_TO(GraphController::loadCircuit, "/api/load", Post);
        ADD_METHOD_TO(GraphController::inputText, "/api/input", Post);
    METHOD_LIST_END

    GraphController() {}

    static void setEngine(LogicEngine* engPtr) {
        engine = engPtr;
    }

    static void setExecuteEngine(ExecuteEngine* engPtr) {
        exec = engPtr;
    }

    void getGraph(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value root;
        root["nodes"] = Json::arrayValue;
        root["wires"] = Json::arrayValue;

        std::map<int, std::pair<int, int>> wire_drivers;

        auto& instances = engine->get_instances();
        for (int i = 0; i < (int)instances.size(); ++i) {
            auto& inst = instances[i];
            Json::Value nodeJson;
            nodeJson["id"] = i;
            nodeJson["name"] = inst.logic->get_name();
            nodeJson["kind"] = inst.logic->get_kind();

            nodeJson["x"] = node_positions.count(i) ? node_positions[i].x : (float)(i * 200 + 100);
            nodeJson["y"] = node_positions.count(i) ? node_positions[i].y : 150.0f;

            nodeJson["inputs"] = Json::arrayValue;
            for (auto type : inst.logic->get_input_schema())
                nodeJson["inputs"].append((int)type);

            nodeJson["outputs"] = Json::arrayValue;
            for (auto type : inst.logic->get_output_schema())
                nodeJson["outputs"].append((int)type);

            for (int p = 0; p < (int)inst.out_wires.size(); ++p) {
                wire_drivers[inst.out_wires[p]] = {i, p};
            }

            Json::Value config;
            inst.logic->save_config(config);
            nodeJson["config"] = config;

            root["nodes"].append(nodeJson);
        }

        int reg_base_id = (int)instances.size();
        auto& registers = engine->get_registers();
        int r_idx = 0;
        for (auto const& [reg_id, reg] : registers) {
            int ui_id = reg_base_id + r_idx;
            Json::Value regJson;
            regJson["id"] = ui_id;
            regJson["name"] = "Register";
            regJson["kind"] = "Register";

            float rx = node_positions.count(ui_id) ? node_positions[ui_id].x : 100.0f + (float)r_idx * 200;
            float ry = node_positions.count(ui_id) ? node_positions[ui_id].y : 400.0f;
            regJson["x"] = rx;
            regJson["y"] = ry;

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
                config["current"] = "[" + std::to_string(reg.val.tokens.size()) + " tokens]";
            }
            regJson["config"] = config;

            root["nodes"].append(regJson);
            r_idx++;
        }

        for (int i = 0; i < (int)instances.size(); ++i) {
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

        auto resp = HttpResponse::newHttpJsonResponse(root);
        callback(resp);
    }

    void tick(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (exec) exec->run_tick();
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("OK");
        callback(resp);
    }

    void syncGraph(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (json) {
            for (const auto& n : (*json)["nodes"]) {
                int id = n["id"].asInt();
                node_positions[id] = {n["x"].asFloat(), n["y"].asFloat()};
            }
        }
        callback(HttpResponse::newHttpResponse());
    }

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

    void loadCircuit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        std::string path = "circuit.json";
        if (json && json->isMember("path")) path = (*json)["path"].asString();

        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("Use CLI: load " + path);
        callback(resp);
    }

    void inputText(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (json && json->isMember("text") && engine) {
            std::string text = (*json)["text"].asString();
            for (auto& inst : engine->get_instances_mut()) {
                auto* ui = dynamic_cast<UserInputNode*>(inst.logic.get());
                if (ui) ui->value = text;
            }
        }
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("OK");
        callback(resp);
    }

private:
    static LogicEngine* engine;
    static ExecuteEngine* exec;
    std::map<int, Vec2> node_positions;
};

inline LogicEngine* GraphController::engine = nullptr;
inline ExecuteEngine* GraphController::exec = nullptr;
