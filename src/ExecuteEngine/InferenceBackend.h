#pragma once

#include <vector>
#include <string>
#include <utility>
#include <memory>

class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;

    virtual std::vector<int> tokenize(const std::string& text, bool add_special) = 0;
    virtual std::string detokenize(int token_id) = 0;
    virtual int get_bos_token() const = 0;
    virtual bool is_eog(int token_id) const = 0;

    virtual std::pair<int, std::string> infer_step(
        const std::vector<int>& context_ids, float temperature) = 0;

    virtual std::string get_model_name() const = 0;
    virtual bool is_ready() const = 0;
    virtual void reset() = 0;
};
