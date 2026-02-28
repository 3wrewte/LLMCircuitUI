#include <drogon/drogon.h>
#include "LogicEngine.h"
#include "GraphController.h"

int main() {
    // 1. Instantiate the Engine
    // We create it here so its lifetime matches the application
    auto engine = std::make_unique<LogicEngine>();

    // 2. Link the Engine to the Controller
    GraphController::setEngine(engine.get());

    // 3. Build the Demo Graph (The Circuit)
    // Register-based loop: sum += 5 until sum >= 25
    auto [r1_cur, r1_nxt] = engine->add_register(DataType::INT, Value(0));
    int w_const = engine->add_wire(DataType::INT);
    int w_sum = engine->add_wire(DataType::INT);
    int w_break = engine->add_wire(DataType::BOOL);

    // Add Nodes
    engine->add_node(std::make_unique<ConstantNode>(5), {}, {w_const});
    engine->add_node(std::make_unique<AdderNode>(), {w_const, r1_cur}, {w_sum});
    
    // Threshold Node: breaks when sum >= 25
    class ThresholdNode : public Component {
    public:
        void compute(const std::vector<Value>& in, std::vector<Value>& out) override {
            out[0] = Value(in[0].i >= 25);
        }
        std::string get_name() const override { return "Threshold"; }
        std::vector<DataType> get_input_schema() const override { return {DataType::INT}; }
        std::vector<DataType> get_output_schema() const override { return {DataType::BOOL}; }
    };
    engine->add_node(std::make_unique<ThresholdNode>(), {w_sum}, {w_break});

    // Loopback: r1_next = w_sum
    // In a real MUX you'd use rst logic, here we just accumulate
    engine->add_node(std::make_unique<AdderNode>(), {w_sum, 0}, {r1_nxt}); // Hacky bypass

    // 4. Start Drogon
    std::cout << "Starting Circuit UI Server on http://localhost:8080" << std::endl;
    
    drogon::app()
        .setDocumentRoot("../web")
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .run();

    return 0;
}