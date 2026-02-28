#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include "LogicEngine.h"

using namespace drogon;

class GraphController : public HttpController<GraphController> {
public:
    struct Vec2 { float x, y; };

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(GraphController::getGraph, "/api/graph", Get);
        ADD_METHOD_TO(GraphController::tick, "/api/tick", Post);
    METHOD_LIST_END

    // Constructor: Initialize positions here
    GraphController() {
        // Default positions for our demo nodes
        node_positions[0] = {100.0f, 150.0f}; // Constant Node
        node_positions[1] = {300.0f, 150.0f}; // Adder Node
        node_positions[2] = {500.0f, 150.0f}; // Threshold Node
    }

    // Static setter to link the engine from main.cpp
    static void setEngine(LogicEngine* engPtr) {
        engine = engPtr;
    }

    void getGraph(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    

    Json::Value root;
    root["nodes"] = Json::arrayValue;
    root["wires"] = Json::arrayValue;

    // We need a way to track which node/port drives which wire
    // Map: WireID -> {node_index, port_index}
    std::map<int, std::pair<int, int>> wire_drivers;

    // --- 1. Process Functional Nodes (Instances) ---
    auto& instances = engine->get_instances(); // You may need a getter for this in LogicEngine
    for (int i = 0; i < instances.size(); ++i) {
        auto& inst = instances[i];
        Json::Value nodeJson;
        nodeJson["id"] = i;
        nodeJson["name"] = inst.logic->get_name();
        
        // Position from controller's map (default to 0,0 if not found)
        nodeJson["x"] = node_positions.count(i) ? node_positions[i].x : 0.0f;
        nodeJson["y"] = node_positions.count(i) ? node_positions[i].y : 0.0f;

        // Input Schema (Types)
        nodeJson["inputs"] = Json::arrayValue;
        for (auto type : inst.logic->get_input_schema()) 
            nodeJson["inputs"].append((int)type);

        // Output Schema (Types)
        nodeJson["outputs"] = Json::arrayValue;
        for (auto type : inst.logic->get_output_schema()) 
            nodeJson["outputs"].append((int)type);

        // Track outputs for wiring logic later
        for (int p = 0; p < inst.out_wires.size(); ++p) {
            wire_drivers[inst.out_wires[p]] = {i, p};
        }

        root["nodes"].append(nodeJson);
    }

    // --- 2. Process Registers as Nodes ---
    // (Starting ID after the last instance index)
    int reg_base_id = instances.size();
    auto& registers = engine->get_registers(); // Need a getter
    int r_idx = 0;
    for (auto const& [reg_id, reg] : registers) {
        int ui_id = reg_base_id + r_idx;
        Json::Value regJson;
        regJson["id"] = ui_id;
        regJson["name"] = "Register";
        regJson["x"] = node_positions.count(ui_id) ? node_positions[ui_id].x : 100.0f;
        regJson["y"] = node_positions.count(ui_id) ? node_positions[ui_id].y : 100.0f;
        
        regJson["inputs"] = Json::arrayValue; regJson["inputs"].append(0); // Assume INT
        regJson["outputs"] = Json::arrayValue; regJson["outputs"].append(0);

        // Register Q output drives the "current" wire
        wire_drivers[reg.cur_wire] = {ui_id, 0};
        
        root["nodes"].append(regJson);
        r_idx++;
    }

    // --- 3. Generate Wires by matching IDs ---
    // Check every node's inputs. If the wire ID has a driver, draw a connection.
    for (int i = 0; i < instances.size(); ++i) {
        auto& inst = instances[i];
        for (int p = 0; p < inst.in_wires.size(); ++p) {
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
        if (engine) engine->tick();
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("OK");
        callback(resp);
    }

private:
    static LogicEngine* engine; // Shared pointer to the engine instance
    std::map<int, Vec2> node_positions; // UI-specific state
};

// Define the static pointer (must be in the .h or a .cpp)
inline LogicEngine* GraphController::engine = nullptr;