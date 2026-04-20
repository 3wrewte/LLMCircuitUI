#pragma once

#include <vector>
#include <string>
#include <memory>

class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;

    virtual std::vector<std::string> infer(
        const std::vector<std::string>& context,
        int max_tokens,
        float temperature) = 0;

    virtual std::string get_model_name() const = 0;
    virtual bool is_ready() const = 0;
};
