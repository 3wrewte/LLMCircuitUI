#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <json/json.h>

enum SysWireID { SYS_CLK = 0, SYS_RST = 1, SYS_BREAK = 2, USER_START = 10 };//reserved wire id for system wires
// --- 1. Type System ---
enum class DataType { INT, BOOL, STRING, TENSOR };

struct Value {
    DataType type;
    union {
        int i;
        bool b;
    };
    std::string s; // Simplified for this example

    Value() : type(DataType::INT), i(0) {}
    Value(int v) : type(DataType::INT), i(v) {}
    Value(bool v) : type(DataType::BOOL), b(v) {}
};

// --- 2. Abstract Component Base ---
class Component {
public:
    virtual ~Component() = default;
    virtual void compute(const std::vector<Value>& inputs, std::vector<Value>& outputs) = 0;
    
    virtual void save_config(Json::Value& config) const {}
    virtual void load_config(const Json::Value& config) {}
    // UI Metadata
    virtual std::string get_name() const = 0;
    virtual std::vector<DataType> get_input_schema() const = 0;
    virtual std::vector<DataType> get_output_schema() const = 0;
};

// --- 3. The Engine (The "Motherboard") ---
class LogicEngine {
private:
    int next_uid = 100; // Start user IDs at 100
    
    struct RegisterEntry {
        int cur_wire;
        int nxt_wire;
        Value val;
    };

    std::map<int, Value> wires;
    std::map<int, DataType> wire_types;
    std::map<int, RegisterEntry> registers;
    
    struct Instance {
        std::unique_ptr<Component> logic;
        std::vector<int> in_wires;
        std::vector<int> out_wires;
    };
    std::vector<Instance> instances;

    std::vector<int> execution_order;

public:
    // --- Factory Methods ---
    int add_wire(DataType type) {
        int id = next_uid++;
        wire_types[id] = type;
        wires[id] = (type == DataType::BOOL) ? Value(false) : Value(0);
        return id;
    }

    struct RegPair { int cur; int nxt; };
    RegPair add_register(DataType type, Value init_val) {
        int cur = add_wire(type);
        int nxt = add_wire(type);
        int reg_id = next_uid++; 
        registers[reg_id] = {cur, nxt, init_val};
        return {cur, nxt};
    }

    int add_node(std::unique_ptr<Component> node, std::vector<int> ins, std::vector<int> outs) {
        instances.push_back({std::move(node), ins, outs});
        return instances.size() - 1;
    }

    const std::vector<Instance>& get_instances() const { return instances; }
    const std::map<int, RegisterEntry>& get_registers() const { return registers; }

    void compile() {
        std::map<int, int> in_degree; 
        std::map<int, std::vector<int>> adj; // Wire -> Component dependency
        
        // Build graph: Which wires drive which components?
        for (int i = 0; i < instances.size(); ++i) {
            int deps = 0;
            for (int input_wire : instances[i].in_wires) {
                // If the input is NOT a system wire or a register output, it's a dependency
                bool is_source = (input_wire < USER_START);
                for(auto const& [rid, reg] : registers) if(reg.cur_wire == input_wire) is_source = true;
                
                if (!is_source) {
                    adj[input_wire].push_back(i);
                    deps++;
                }
            }
            in_degree[i] = deps;
        }

        std::queue<int> q;
        for (int i = 0; i < instances.size(); ++i) if (in_degree[i] == 0) q.push(i);

        execution_order.clear();
        while (!q.empty()) {
            int u = q.front(); q.pop();
            execution_order.push_back(u);
            for (int out_wire : instances[u].out_wires) {
                for (int v : adj[out_wire]) {
                    if (--in_degree[v] == 0) q.push(v);
                }
            }
        }
    }

    // --- Execution ---
    void tick() {
        // Step 1: Reg current -> Wires
        for (auto& [id, reg] : registers) {
            wires[reg.cur_wire] = reg.val;
        }

        // Step 2: Solve Wires Topologically
        for (int idx : execution_order) {
            auto& comp = instances[idx];
            std::vector<Value> in_vals;
            for (int in_id : comp.in_wires) in_vals.push_back(wires[in_id]);
            
            std::vector<Value> out_vals(comp.out_wires.size());
            comp.logic->compute(in_vals, out_vals);
            
            for (size_t i = 0; i < comp.out_wires.size(); ++i) {
                wires[comp.out_wires[i]] = out_vals[i];
            }
        }

        // Step 3: Update Registers (Clock Edge)
        for (auto& [id, reg] : registers) {
            reg.val = wires[reg.nxt_wire];
        }

    }

    Value read_wire(int id) { return wires[id]; }
};

// --- 4. Example Component Implementation ---
class AdderNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].i + in[1].i);
    }
    std::string get_name() const override { return "Adder"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::INT, DataType::INT}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
};

class ConstantNode : public Component {
public:
    int value;
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(value);
    }

    void save_config(Json::Value& config) const override { config["value"] = value; }
    void load_config(const Json::Value& config) override { value = config["value"].asInt(); }
    std::string get_name() const override { return "Constant"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
    ConstantNode(int v){
        value = v;
    }
};

