#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <memory>
#include <queue>
#include <functional>
#include <json/json.h>

enum SysWireID { SYS_CLK = 0, SYS_RST = 1, SYS_BREAK = 2, USER_START = 10 };

enum class DataType { INT, BOOL, STRING, TOKEN, TOKEN_STREAM, CONTEXT_BUFFER };

inline std::string datatype_to_str(DataType t) {
    switch (t) {
        case DataType::INT: return "INT";
        case DataType::BOOL: return "BOOL";
        case DataType::STRING: return "STRING";
        case DataType::TOKEN: return "TOKEN";
        case DataType::TOKEN_STREAM: return "TOKEN_STREAM";
        case DataType::CONTEXT_BUFFER: return "CONTEXT_BUFFER";
    }
    return "UNKNOWN";
}

inline DataType str_to_datatype(const std::string& s) {
    if (s == "INT") return DataType::INT;
    if (s == "BOOL") return DataType::BOOL;
    if (s == "STRING") return DataType::STRING;
    if (s == "TOKEN") return DataType::TOKEN;
    if (s == "TOKEN_STREAM") return DataType::TOKEN_STREAM;
    if (s == "CONTEXT_BUFFER") return DataType::CONTEXT_BUFFER;
    return DataType::INT;
}

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

    Json::Value to_json() const {
        Json::Value j;
        j["type"] = datatype_to_str(type);
        switch (type) {
            case DataType::INT: j["value"] = i; break;
            case DataType::BOOL: j["value"] = b; break;
            case DataType::STRING: j["value"] = s; break;
            case DataType::TOKEN: j["value"] = s; break;
            case DataType::TOKEN_STREAM:
            case DataType::CONTEXT_BUFFER: {
                Json::Value arr(Json::arrayValue);
                for (auto& t : tokens) arr.append(t);
                j["value"] = arr;
                break;
            }
        }
        return j;
    }

    static Value from_json(const Json::Value& j) {
        auto t = str_to_datatype(j["type"].asString());
        auto& v = j["value"];
        switch (t) {
            case DataType::INT: return Value(v.asInt());
            case DataType::BOOL: return Value(v.asBool());
            case DataType::STRING: return Value(v.asString());
            case DataType::TOKEN: return Value::token(v.asString());
            case DataType::TOKEN_STREAM: {
                std::vector<std::string> ts;
                for (auto& e : v) ts.push_back(e.asString());
                return Value::token_stream(std::move(ts));
            }
            case DataType::CONTEXT_BUFFER: {
                std::vector<std::string> ts;
                for (auto& e : v) ts.push_back(e.asString());
                return Value::context_buffer(std::move(ts));
            }
        }
        return Value(0);
    }
};

class Component {
public:
    virtual ~Component() = default;
    virtual void compute(const std::vector<Value>& inputs, std::vector<Value>& outputs) = 0;

    virtual void save_config(Json::Value& config) const {}
    virtual void load_config(const Json::Value& config) {}

    virtual std::string get_name() const = 0;
    virtual std::string get_kind() const = 0;
    virtual std::vector<DataType> get_input_schema() const = 0;
    virtual std::vector<DataType> get_output_schema() const = 0;
};

// --- Node Factory ---

class NodeFactory {
public:
    using Creator = std::function<std::unique_ptr<Component>()>;
    static std::map<std::string, Creator>& registry() {
        static std::map<std::string, Creator> reg;
        return reg;
    }
    static void register_node(const std::string& kind, Creator fn) {
        registry()[kind] = std::move(fn);
    }
    static std::unique_ptr<Component> create(const std::string& kind) {
        auto& reg = registry();
        auto it = reg.find(kind);
        if (it == reg.end()) return nullptr;
        return it->second();
    }
    static std::vector<std::string> list_kinds() {
        std::vector<std::string> keys;
        for (auto& [k, _] : registry()) keys.push_back(k);
        return keys;
    }
};

#define REGISTER_NODE(Kind, Class) \
    static bool _reg_##Class = []{ \
        NodeFactory::register_node(Kind, []() -> std::unique_ptr<Component> { \
            return std::make_unique<Class>(); \
        }); \
        return true; \
    }()

// --- Compile Diagnostics ---

struct CompileError {
    std::string severity; // "error" or "warning"
    std::string message;
};

// --- Logic Engine ---

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
    std::vector<CompileError> compile_errors;

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
    std::vector<Instance>& get_instances_mut() { return instances; }
    const std::map<int, RegisterEntry>& get_registers() const { return registers; }
    std::map<int, RegisterEntry>& get_registers_mut() { return registers; }
    Value read_wire(int id) { return wires[id]; }
    const std::map<int, DataType>& get_wire_types() const { return wire_types; }
    const std::vector<CompileError>& get_compile_errors() const { return compile_errors; }

    void reset() {
        instances.clear();
        registers.clear();
        wires.clear();
        wire_types.clear();
        execution_order.clear();
        compile_errors.clear();
        next_uid = 100;
    }

    // --- Compile with passes ---

    void compile() {
        compile_errors.clear();
        pass_type_check();
        pass_loop_detection();
        pass_dead_code_elim();
        pass_topo_sort();
    }

    void pass_type_check() {
        for (int i = 0; i < (int)instances.size(); ++i) {
            auto& inst = instances[i];
            auto in_schema = inst.logic->get_input_schema();
            if (inst.in_wires.size() != in_schema.size()) {
                compile_errors.push_back({"error",
                    "[" + std::to_string(i) + "] " + inst.logic->get_name() +
                    ": expected " + std::to_string(in_schema.size()) +
                    " inputs, got " + std::to_string(inst.in_wires.size())});
                continue;
            }
            for (int p = 0; p < (int)inst.in_wires.size(); ++p) {
                auto wire_type = wire_types[inst.in_wires[p]];
                if (wire_type != in_schema[p]) {
                    compile_errors.push_back({"error",
                        "[" + std::to_string(i) + "] " + inst.logic->get_name() +
                        " input " + std::to_string(p) + ": expected " +
                        datatype_to_str(in_schema[p]) + ", got " + datatype_to_str(wire_type)});
                }
            }
            auto out_schema = inst.logic->get_output_schema();
            if (inst.out_wires.size() != out_schema.size()) {
                compile_errors.push_back({"error",
                    "[" + std::to_string(i) + "] " + inst.logic->get_name() +
                    ": expected " + std::to_string(out_schema.size()) +
                    " outputs, got " + std::to_string(inst.out_wires.size())});
            }
            for (int p = 0; p < (int)inst.out_wires.size() && p < (int)out_schema.size(); ++p) {
                auto wire_type = wire_types[inst.out_wires[p]];
                if (wire_type != out_schema[p]) {
                    compile_errors.push_back({"error",
                        "[" + std::to_string(i) + "] " + inst.logic->get_name() +
                        " output " + std::to_string(p) + ": expected " +
                        datatype_to_str(out_schema[p]) + ", got " + datatype_to_str(wire_type)});
                }
            }
        }
    }

    void pass_loop_detection() {
        std::set<int> reg_cur_wires;
        for (auto& [id, reg] : registers) reg_cur_wires.insert(reg.cur_wire);

        std::map<int, std::vector<int>> graph;
        for (int i = 0; i < (int)instances.size(); ++i) {
            for (int in_w : instances[i].in_wires) {
                if (reg_cur_wires.count(in_w)) continue;
                if (in_w < USER_START) continue;
                for (int j = 0; j < (int)instances.size(); ++j) {
                    for (int out_w : instances[j].out_wires) {
                        if (out_w == in_w) {
                            graph[j].push_back(i);
                        }
                    }
                }
            }
        }

        std::vector<int> color(instances.size(), 0);
        std::vector<int> cycle_path;
        bool found = false;

        std::function<bool(int)> dfs = [&](int u) -> bool {
            color[u] = 1;
            for (int v : graph[u]) {
                if (color[v] == 1) {
                    cycle_path.push_back(v);
                    cycle_path.push_back(u);
                    return true;
                }
                if (color[v] == 0 && dfs(v)) {
                    cycle_path.push_back(u);
                    return true;
                }
            }
            color[u] = 2;
            return false;
        };

        for (int i = 0; i < (int)instances.size() && !found; ++i) {
            if (color[i] == 0) found = dfs(i);
        }

        if (found) {
            compile_errors.push_back({"error",
                "Combinational loop detected: " +
                [&]() {
                    std::string path;
                    for (int i = (int)cycle_path.size() - 1; i >= 0; --i) {
                        if (i < (int)cycle_path.size() - 1) path += " -> ";
                        path += instances[cycle_path[i]].logic->get_name() +
                                "[" + std::to_string(cycle_path[i]) + "]";
                    }
                    return path;
                }()});
        }
    }

    void pass_dead_code_elim() {
        std::set<int> reachable;
        std::set<int> driven_wires;
        for (auto& [id, reg] : registers) driven_wires.insert(reg.nxt_wire);
        for (int i = 0; i < (int)instances.size(); ++i) {
            for (int w : instances[i].in_wires) driven_wires.insert(w);
        }

        std::function<void(int)> mark = [&](int i) {
            if (reachable.count(i)) return;
            reachable.insert(i);
            for (int w : instances[i].in_wires) {
                for (int j = 0; j < (int)instances.size(); ++j) {
                    for (int ow : instances[j].out_wires) {
                        if (ow == w) mark(j);
                    }
                }
            }
        };

        for (int i = 0; i < (int)instances.size(); ++i) {
            for (int w : instances[i].out_wires) {
                if (driven_wires.count(w)) mark(i);
            }
        }

        int unreachable = (int)instances.size() - (int)reachable.size();
        if (unreachable > 0) {
            compile_errors.push_back({"warning",
                std::to_string(unreachable) + " unreachable node(s) detected"});
        }
    }

    void pass_topo_sort() {
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

        if ((int)execution_order.size() < (int)instances.size()) {
            compile_errors.push_back({"error",
                "Topological sort incomplete: " +
                std::to_string(instances.size() - execution_order.size()) +
                " node(s) in cycle"});
        }
    }

    bool has_errors() const {
        for (auto& e : compile_errors) if (e.severity == "error") return true;
        return false;
    }

    // --- Execution ---

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

    bool is_break() {
        return wires[SYS_BREAK].b;
    }

    void set_wire(int id, Value v) {
        wires[id] = std::move(v);
    }

    // --- Serialization ---

    Json::Value serialize() const {
        Json::Value root;
        root["version"] = 1;

        Json::Value wires_arr(Json::arrayValue);
        for (auto& [id, type] : wire_types) {
            Json::Value w;
            w["id"] = id;
            w["type"] = datatype_to_str(type);
            wires_arr.append(w);
        }
        root["wires"] = wires_arr;

        Json::Value regs_arr(Json::arrayValue);
        for (auto& [id, reg] : registers) {
            Json::Value r;
            r["id"] = id;
            r["cur_wire"] = reg.cur_wire;
            r["nxt_wire"] = reg.nxt_wire;
            r["init"] = reg.val.to_json();
            regs_arr.append(r);
        }
        root["registers"] = regs_arr;

        Json::Value nodes_arr(Json::arrayValue);
        for (int i = 0; i < (int)instances.size(); ++i) {
            auto& inst = instances[i];
            Json::Value n;
            n["kind"] = inst.logic->get_kind();
            n["inputs"] = Json::arrayValue;
            for (int w : inst.in_wires) n["inputs"].append(w);
            n["outputs"] = Json::arrayValue;
            for (int w : inst.out_wires) n["outputs"].append(w);
            Json::Value cfg;
            inst.logic->save_config(cfg);
            n["config"] = cfg;
            nodes_arr.append(n);
        }
        root["nodes"] = nodes_arr;

        return root;
    }

    static std::unique_ptr<LogicEngine> deserialize(const Json::Value& root) {
        auto eng = std::make_unique<LogicEngine>();

        for (auto& w : root["wires"]) {
            int id = w["id"].asInt();
            DataType type = str_to_datatype(w["type"].asString());
            while (eng->next_uid <= id) eng->next_uid++;
            eng->next_uid = id + 1;
            eng->wire_types[id] = type;
            Value v;
            switch (type) {
                case DataType::BOOL: v = Value(false); break;
                case DataType::STRING: v = Value(std::string("")); break;
                case DataType::TOKEN: v = Value::token(""); break;
                case DataType::TOKEN_STREAM: v = Value::token_stream(std::vector<std::string>{}); break;
                case DataType::CONTEXT_BUFFER: v = Value::context_buffer(std::vector<std::string>{}); break;
                default: v = Value(0); break;
            }
            eng->wires[id] = v;
        }

        for (auto& r : root["registers"]) {
            int id = r["id"].asInt();
            int cur_wire = r["cur_wire"].asInt();
            int nxt_wire = r["nxt_wire"].asInt();
            Value init = Value::from_json(r["init"]);
            while (eng->next_uid <= id) eng->next_uid++;
            eng->next_uid = id + 1;
            eng->registers[id] = {cur_wire, nxt_wire, init};

            if (!eng->wire_types.count(cur_wire)) {
                eng->wire_types[cur_wire] = init.type;
                eng->wires[cur_wire] = init;
            }
            if (!eng->wire_types.count(nxt_wire)) {
                eng->wire_types[nxt_wire] = init.type;
                eng->wires[nxt_wire] = init;
            }
            while (eng->next_uid <= cur_wire) eng->next_uid++;
            if (cur_wire >= eng->next_uid) eng->next_uid = cur_wire + 1;
            while (eng->next_uid <= nxt_wire) eng->next_uid++;
            if (nxt_wire >= eng->next_uid) eng->next_uid = nxt_wire + 1;
        }

        for (auto& n : root["nodes"]) {
            std::string kind = n["kind"].asString();
            auto comp = NodeFactory::create(kind);
            if (!comp) {
                std::cerr << "[Deserialize] unknown node kind: " << kind << std::endl;
                continue;
            }
            if (n.isMember("config")) {
                comp->load_config(n["config"]);
            }
            std::vector<int> ins, outs;
            for (auto& w : n["inputs"]) ins.push_back(w.asInt());
            for (auto& w : n["outputs"]) outs.push_back(w.asInt());
            eng->instances.push_back({std::move(comp), std::move(ins), std::move(outs)});
        }

        return eng;
    }
};

// --- Primitive Nodes ---

class AdderNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].i + in[1].i);
    }
    std::string get_name() const override { return "Adder"; }
    std::string get_kind() const override { return "Adder"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::INT, DataType::INT}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
};
REGISTER_NODE("Adder", AdderNode);

class ConstantNode : public Component {
public:
    int value = 0;
    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(value);
    }
    void save_config(Json::Value& config) const override { config["value"] = value; }
    void load_config(const Json::Value& config) override { value = config.get("value", 0).asInt(); }
    std::string get_name() const override { return "Constant"; }
    std::string get_kind() const override { return "Constant"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::INT}; }
};
REGISTER_NODE("Constant", ConstantNode);

class ThresholdNode : public Component {
public:
    int threshold = 25;
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].i >= threshold);
    }
    void save_config(Json::Value& config) const override { config["threshold"] = threshold; }
    void load_config(const Json::Value& config) override { threshold = config.get("threshold", 25).asInt(); }
    std::string get_name() const override { return "Threshold"; }
    std::string get_kind() const override { return "Threshold"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::INT}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("Threshold", ThresholdNode);

class StringConstantNode : public Component {
public:
    std::string text;
    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(text);
    }
    void save_config(Json::Value& config) const override { config["text"] = text; }
    void load_config(const Json::Value& config) override { text = config.get("text", "").asString(); }
    std::string get_name() const override { return "StrConst"; }
    std::string get_kind() const override { return "StrConst"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
    StringConstantNode() = default;
    explicit StringConstantNode(std::string t) : text(std::move(t)) {}
};
REGISTER_NODE("StrConst", StringConstantNode);

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
    std::string get_kind() const override { return "LLMInfer"; }
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
REGISTER_NODE("LLMInfer", LLMInferNode);

class TokenAccumNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        auto stream = in[0].tokens;
        stream.push_back(in[1].s);
        out[0] = Value::token_stream(std::move(stream));
    }
    std::string get_name() const override { return "TokenAccum"; }
    std::string get_kind() const override { return "TokenAccum"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN_STREAM, DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN_STREAM}; }
};
REGISTER_NODE("TokenAccum", TokenAccumNode);

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
    std::string get_kind() const override { return "ContextBuild"; }
    std::vector<DataType> get_input_schema() const override {
        return std::vector<DataType>(num_inputs, DataType::TOKEN_STREAM);
    }
    std::vector<DataType> get_output_schema() const override { return {DataType::CONTEXT_BUFFER}; }

    void save_config(Json::Value& config) const override { config["num_inputs"] = num_inputs; }
    void load_config(const Json::Value& config) override { num_inputs = config.get("num_inputs", 2).asInt(); }
};
REGISTER_NODE("ContextBuild", ContextBuildNode);

class TokenToStringNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].s);
    }
    std::string get_name() const override { return "Token2Str"; }
    std::string get_kind() const override { return "Token2Str"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
};
REGISTER_NODE("Token2Str", TokenToStringNode);

class StringToTokenNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value::token(in[0].s);
    }
    std::string get_name() const override { return "Str2Token"; }
    std::string get_kind() const override { return "Str2Token"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::STRING}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN}; }
};
REGISTER_NODE("Str2Token", StringToTokenNode);

class StringToTokenStreamNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value::token_stream(std::vector<std::string>{in[0].s});
    }
    std::string get_name() const override { return "Str2Stream"; }
    std::string get_kind() const override { return "Str2Stream"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::STRING}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::TOKEN_STREAM}; }
};
REGISTER_NODE("Str2Stream", StringToTokenStreamNode);

class TokenMatchNode : public Component {
public:
    std::string match_token;
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].s == match_token);
    }
    void save_config(Json::Value& config) const override { config["match"] = match_token; }
    void load_config(const Json::Value& config) override { match_token = config.get("match", "").asString(); }
    std::string get_name() const override { return "TokenMatch"; }
    std::string get_kind() const override { return "TokenMatch"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("TokenMatch", TokenMatchNode);

// --- P2-B: New Nodes ---

class UserInputNode : public Component {
public:
    std::string value;

    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(value);
    }
    void save_config(Json::Value& config) const override { config["value"] = value; }
    void load_config(const Json::Value& config) override { value = config.get("value", "").asString(); }
    std::string get_name() const override { return "UserInput"; }
    std::string get_kind() const override { return "UserInput"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
};
REGISTER_NODE("UserInput", UserInputNode);

class StopConditionNode : public Component {
public:
    std::vector<std::string> stop_tokens = {"<|endoftext|>", "<|im_end|>", "</s>"};

    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        const auto& tok = in[0].s;
        bool matched = false;
        for (auto& st : stop_tokens) {
            if (tok == st) { matched = true; break; }
        }
        out[0] = Value(matched);
    }
    void save_config(Json::Value& config) const override {
        Json::Value arr(Json::arrayValue);
        for (auto& t : stop_tokens) arr.append(t);
        config["stop_tokens"] = arr;
    }
    void load_config(const Json::Value& config) override {
        stop_tokens.clear();
        if (config.isMember("stop_tokens")) {
            for (auto& t : config["stop_tokens"]) stop_tokens.push_back(t.asString());
        }
    }
    std::string get_name() const override { return "StopCond"; }
    std::string get_kind() const override { return "StopCond"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("StopCond", StopConditionNode);

class MuxNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = in[0].b ? in[1] : in[2];
    }
    std::string get_name() const override { return "Mux"; }
    std::string get_kind() const override { return "Mux"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::BOOL, DataType::STRING, DataType::STRING}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
};
REGISTER_NODE("Mux", MuxNode);

class BoolConstantNode : public Component {
public:
    bool value = false;
    void compute(const std::vector<Value>&, std::vector<Value>& out) override {
        out[0] = Value(value);
    }
    void save_config(Json::Value& config) const override { config["value"] = value; }
    void load_config(const Json::Value& config) override { value = config.get("value", false).asBool(); }
    std::string get_name() const override { return "BoolConst"; }
    std::string get_kind() const override { return "BoolConst"; }
    std::vector<DataType> get_input_schema() const override { return {}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("BoolConst", BoolConstantNode);

class AndNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(in[0].b && in[1].b);
    }
    std::string get_name() const override { return "And"; }
    std::string get_kind() const override { return "And"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::BOOL, DataType::BOOL}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("And", AndNode);

class NotNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        out[0] = Value(!in[0].b);
    }
    std::string get_name() const override { return "Not"; }
    std::string get_kind() const override { return "Not"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::BOOL}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
};
REGISTER_NODE("Not", NotNode);

class TokenStreamToStringNode : public Component {
public:
    void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
        std::string result;
        for (auto& t : in[0].tokens) result += t;
        out[0] = Value(result);
    }
    std::string get_name() const override { return "Stream2Str"; }
    std::string get_kind() const override { return "Stream2Str"; }
    std::vector<DataType> get_input_schema() const override { return {DataType::TOKEN_STREAM}; }
    std::vector<DataType> get_output_schema() const override { return {DataType::STRING}; }
};
REGISTER_NODE("Stream2Str", TokenStreamToStringNode);
