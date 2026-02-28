#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>

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

    // --- Execution ---
    void tick() {
        // 1. Reg Value -> Cur Wire
        for (auto& [id, reg] : registers) {
            wires[reg.cur_wire] = reg.val;
        }

        // 2. Compute Nodes (Assuming sorted for now)
        for (auto& inst : instances) {
            std::vector<Value> in_vals;
            for (int id : inst.in_wires) in_vals.push_back(wires[id]);
            
            std::vector<Value> out_vals(inst.out_wires.size());
            inst.logic->compute(in_vals, out_vals);
            
            for (size_t i = 0; i < inst.out_wires.size(); ++i) {
                wires[inst.out_wires[i]] = out_vals[i];
            }
        }

        // 3. Nxt Wire -> Reg Value
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
    std::string get_name() const override { return "Constant"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::INT, DataType::INT}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
    ConstantNode(int v){
        value = v;
    }
};

