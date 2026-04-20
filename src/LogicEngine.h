#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <queue>
#include <functional>
#include <json/json.h>

enum SysWireID { SYS_CLK = 0, SYS_RST = 1, SYS_BREAK = 2, USER_START = 10 };

enum class DataType { INT, BOOL, STRING, TOKEN, TOKEN_STREAM, CONTEXT_BUFFER };

struct Value {
    DataType type;
    int i = 0;
    bool b = false;
    std::string s;
    std::vector<std::string> tokens;

    Value() : type(DataType::INT) {}
    Value(int v) : type(DataType::INT), i(v) {}
    Value(bool v) : type(DataType::BOOL), b(v) {}
    Value(std::string v) : type(DataType::STRING), s(std::move(v)) {}

    static Value token(const std::string& t) {
        Value v; v.type = DataType::TOKEN; v.s = t; return v;
    }
    static Value token_stream(const std::vector<std::string>& ts) {
        Value v; v.type = DataType::TOKEN_STREAM; v.tokens = ts; return v;
    }
    static Value token_stream(std::vector<std::string>&& ts) {
        Value v; v.type = DataType::TOKEN_STREAM; v.tokens = std::move(ts); return v;
    }
    static Value context_buffer(const std::vector<std::string>& cb) {
        Value v; v.type = DataType::CONTEXT_BUFFER; v.tokens = cb; return v;
    }
    static Value context_buffer(std::vector<std::string>&& cb) {
        Value v; v.type = DataType::CONTEXT_BUFFER; v.tokens = std::move(cb); return v;
    }
};

class Component {
public:
    virtual ~Component() = default;
    virtual void compute(const std::vector<Value>& inputs, std::vector<Value>& outputs) = 0;

    virtual void save_config(Json::Value& config) const {}
    virtual void load_config(const Json::Value& config) {}

    virtual std::string get_name() const = 0;
    virtual std::vector<DataType> get_input_schema() const = 0;
    virtual std::vector<DataType> get_output_schema() const = 0;
};

class LogicEngine {
private:
    int next_uid = 100;

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
    std::function<void(int, const std::vector<Value>&, const std::vector<Value>&)> on_node_computed;
    int add_wire(DataType type) {
        int id = next_uid++;
        wire_types[id] = type;
        switch (type) {
            case DataType::BOOL: wires[id] = Value(false); break;
            case DataType::STRING: wires[id] = Value(std::string("")); break;
            case DataType::TOKEN: wires[id] = Value::token(""); break;
            case DataType::TOKEN_STREAM: wires[id] = Value::token_stream(std::vector<std::string>{}); break;
            case DataType::CONTEXT_BUFFER: wires[id] = Value::context_buffer(std::vector<std::string>{}); break;
            default: wires[id] = Value(0); break;
        }
        return id;
    }

    struct RegPair { int cur; int nxt; };
    RegPair add_register(DataType type, Value init_val) {
        int cur = add_wire(type);
        int nxt = add_wire(type);
        int reg_id = next_uid++;
        registers[reg_id] = {cur, nxt, std::move(init_val)};
        return {cur, nxt};
    }

    int add_node(std::unique_ptr<Component> node, std::vector<int> ins, std::vector<int> outs) {
        instances.push_back({std::move(node), std::move(ins), std::move(outs)});
        return (int)instances.size() - 1;
    }

    const std::vector<Instance>& get_instances() const { return instances; }
    const std::map<int, RegisterEntry>& get_registers() const { return registers; }
    Value read_wire(int id) { return wires[id]; }

    void compile() {
        std::map<int, int> in_degree;
        std::map<int, std::vector<int>> adj;

        for (int i = 0; i < (int)instances.size(); ++i) {
            int deps = 0;
            for (int input_wire : instances[i].in_wires) {
                bool is_source = (input_wire < USER_START);
                for (auto const& [rid, reg] : registers) if (reg.cur_wire == input_wire) is_source = true;
                if (!is_source) {
                    adj[input_wire].push_back(i);
                    deps++;
                }
            }
            in_degree[i] = deps;
        }

        std::queue<int> q;
        for (int i = 0; i < (int)instances.size(); ++i) if (in_degree[i] == 0) q.push(i);

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

    void tick() {
        for (auto& [id, reg] : registers) {
            wires[reg.cur_wire] = reg.val;
        }

        for (int idx : execution_order) {
            auto& comp = instances[idx];
            std::vector<Value> in_vals;
            for (int in_id : comp.in_wires) in_vals.push_back(wires[in_id]);

            std::vector<Value> out_vals(comp.out_wires.size());
            comp.logic->compute(in_vals, out_vals);

            if (on_node_computed) on_node_computed(idx, in_vals, out_vals);

            for (size_t i = 0; i < comp.out_wires.size(); ++i) {
                wires[comp.out_wires[i]] = out_vals[i];
            }
        }

        for (auto& [id, reg] : registers) {
            reg.val = wires[reg.nxt_wire];
        }
    }
};

// --- Primitive Nodes ---

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
    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(value);
    }
    void save_config(Json::Value& config) const override { config["value"] = value; }
    void load_config(const Json::Value& config) override { value = config["value"].asInt(); }
    std::string get_name() const override { return "Constant"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
    explicit ConstantNode(int v) : value(v) {}
};

class ThresholdNode : public Component {
public:
    int threshold = 25;
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].i >= threshold);
    }
    void save_config(Json::Value& config) const override { config["threshold"] = threshold; }
    void load_config(const Json::Value& config) override { threshold = config["threshold"].asInt(); }
    std::string get_name() const override { return "Threshold"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::INT}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
    explicit ThresholdNode(int t = 25) : threshold(t) {}
};

class StringConstantNode : public Component {
public:
    std::string text;
    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(text);
    }
    void save_config(Json::Value& config) const override { config["text"] = text; }
    void load_config(const Json::Value& config) override { text = config["text"].asString(); }
    std::string get_name() const override { return "StrConst"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
    explicit StringConstantNode(std::string t = "") : text(std::move(t)) {}
};

// --- LLM Nodes ---

class LLMInferNode : public Component {
public:
    std::string model_name = "stub";
    float temperature = 0.7f;
    std::function<std::string(const std::vector<std::string>&)> infer_callback;

    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        if (infer_callback) {
            out[0] = Value::token(infer_callback(in[0].tokens));
        } else {
            static int counter = 0;
            out[0] = Value::token("[stub_" + std::to_string(counter++) + "]");
        }
    }

    std::string get_name() const override { return "LLMInfer"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::CONTEXT_BUFFER}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN}; }

    void save_config(Json::Value& config) const override {
        config["model"] = model_name;
        config["temperature"] = temperature;
    }
    void load_config(const Json::Value& config) override {
        model_name = config.get("model", "stub").asString();
        temperature = config.get("temperature", 0.7).asFloat();
    }
};

class TokenAccumNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        auto stream = in[0].tokens;
        stream.push_back(in[1].s);
        out[0] = Value::token_stream(std::move(stream));
    }
    std::string get_name() const override { return "TokenAccum"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN_STREAM, DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN_STREAM}; }
};

class ContextBuildNode : public Component {
    int num_inputs;
public:
    explicit ContextBuildNode(int n = 2) : num_inputs(n) {}

    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        std::vector<std::string> result;
        for (const auto& v : in) {
            if (v.type == DataType::TOKEN_STREAM || v.type == DataType::CONTEXT_BUFFER) {
                result.insert(result.end(), v.tokens.begin(), v.tokens.end());
            } else if (v.type == DataType::STRING) {
                result.push_back(v.s);
            } else if (v.type == DataType::TOKEN) {
                result.push_back(v.s);
            }
        }
        out[0] = Value::context_buffer(std::move(result));
    }

    std::string get_name() const override { return "ContextBuild"; }
    std::vector<DataType> get_input_schema() const override {
        return std::vector<DataType>(num_inputs, DataType::TOKEN_STREAM);
    }
    std::vector<DataType> get_output_schema() const override { return {DataType::CONTEXT_BUFFER}; }

    void save_config(Json::Value& config) const override { config["num_inputs"] = num_inputs; }
    void load_config(const Json::Value& config) override { num_inputs = config["num_inputs"].asInt(); }
};

class TokenToStringNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].s);
    }
    std::string get_name() const override { return "Token2Str"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
};

class StringToTokenNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value::token(in[0].s);
    }
    std::string get_name() const override { return "Str2Token"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::STRING}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN}; }
};

class StringToTokenStreamNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value::token_stream(std::vector<std::string>{in[0].s});
    }
    std::string get_name() const override { return "Str2Stream"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::STRING}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN_STREAM}; }
};

class TokenMatchNode : public Component {
public:
    std::string match_token;
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].s == match_token);
    }
    void save_config(Json::Value& config) const override { config["match"] = match_token; }
    void load_config(const Json::Value& config) override { match_token = config["match"].asString(); }
    std::string get_name() const override { return "TokenMatch"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
    explicit TokenMatchNode(std::string t = "") : match_token(std::move(t)) {}
};
