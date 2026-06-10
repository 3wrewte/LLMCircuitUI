#include "../src/LogicEngine.h"
#include "../src/ExecuteEngine/LlamaBackend.h"
#include <iostream>
#include <cassert>

int main() {
    auto be = std::make_unique<LlamaBackend>("/home/wrewte/Programs/LLMCircuitUI/models/qwen3.5-2b.gguf");
    if (!be->is_ready()) {
        std::cerr << "Model not ready. Check model path." << std::endl;
        return 1;
    }
    std::cout << "[TEST] Model loaded: " << be->get_model_name() << std::endl;

    // Test tokenization
    auto tokens = be->tokenize("Hello world!", false);
    std::cout << "[TEST] Tokenize 'Hello world!' (no special): " << tokens.size() << " tokens: ";
    for (auto t : tokens) std::cout << t << " ";
    std::cout << std::endl;
    assert(!tokens.empty());

    // Test detokenization
    auto text = be->detokenize(tokens[0]);
    std::cout << "[TEST] Detokenize(" << tokens[0] << ") = '" << text << "'" << std::endl;
    assert(!text.empty());

    // Test inference with simple prompt
    auto prompt = be->tokenize("The capital of France is", false);
    auto [id, result] = be->infer_step(prompt, 0.0f);
    std::cout << "[TEST] Infer: token_id=" << id << " text='" << result << "'" << std::endl;
    assert(id >= 0);
    assert(!result.empty());

    // Test BOS token
    std::cout << "[TEST] BOS token: " << be->get_bos_token() << std::endl;

    std::cout << "\n[TEST] All LlamaBackend tests PASSED" << std::endl;
    return 0;
}
