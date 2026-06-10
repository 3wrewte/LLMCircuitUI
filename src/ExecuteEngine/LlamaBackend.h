#pragma once

#include "InferenceBackend.h"
#include <llama.h>
#include <ggml-backend.h>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

class LlamaBackend : public InferenceBackend {
    static bool s_backend_initialized;

    llama_model * model_ = nullptr;
    const llama_vocab * vocab_ = nullptr;
    llama_context * ctx_ = nullptr;
    llama_sampler * sampler_ = nullptr;

    int bos_id_ = -1;
    std::string model_name_;
    bool ready_ = false;
    int decoded_count_ = 0;

    void init_backend_once() {
        if (!s_backend_initialized) {
            ggml_backend_load_all();
            s_backend_initialized = true;
        }
    }

    void create_sampler(float temperature) {
        auto sparams = llama_sampler_chain_default_params();
        sampler_ = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(temperature));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    void ensure_context() {
        if (ctx_) return;
        if (!model_) return;

        auto ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 4096;
        ctx_params.n_batch = 2048;
        ctx_params.n_threads = 4;
        ctx_params.n_threads_batch = 4;

        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) {
            std::cerr << "[LlamaBackend] Failed to create context" << std::endl;
        }
    }

public:
    LlamaBackend(const std::string& model_path, int n_gpu_layers = 0) {
        init_backend_once();
        load_model(model_path, n_gpu_layers);
    }

    ~LlamaBackend() {
        if (sampler_) llama_sampler_free(sampler_);
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    void load_model(const std::string& model_path, int n_gpu_layers) {
        auto mparams = llama_model_default_params();
        mparams.n_gpu_layers = n_gpu_layers;

        model_ = llama_model_load_from_file(model_path.c_str(), mparams);
        if (!model_) {
            std::cerr << "[LlamaBackend] Failed to load model: " << model_path << std::endl;
            return;
        }

        vocab_ = llama_model_get_vocab(model_);
        bos_id_ = llama_vocab_bos(vocab_);
        model_name_ = model_path;

        create_sampler(0.7f);
        ensure_context();

        ready_ = true;
        std::cout << "[LlamaBackend] Loaded: " << model_path
                  << " bos=" << bos_id_ << std::endl;
    }

    std::vector<int> tokenize(const std::string& text, bool add_special) override {
        if (!vocab_) return {};

        int n_tokens = -llama_tokenize(vocab_, text.c_str(), text.size(), nullptr, 0, add_special, true);
        if (n_tokens <= 0) return {};

        std::vector<int> tokens(n_tokens);
        llama_tokenize(vocab_, text.c_str(), text.size(),
                       tokens.data(), n_tokens, add_special, true);
        return tokens;
    }

    std::string detokenize(int token_id) override {
        if (!vocab_) return "";

        char buf[256];
        int n = llama_token_to_piece(vocab_, token_id, buf, sizeof(buf), 0, true);
        if (n > 0) return std::string(buf, n);
        return "";
    }

    int get_bos_token() const override { return bos_id_; }

    bool is_eog(int token_id) const override {
        if (!vocab_) return false;
        return llama_vocab_is_eog(vocab_, token_id);
    }

    std::pair<int, std::string> infer_step(
        const std::vector<int>& context_ids, float temperature) override {

        if (!ready_ || !ctx_ || !model_) return {-1, "[error]"};
        ensure_context();

        if (decoded_count_ == 0) {
            std::vector<int> full;
            if (bos_id_ != -1) full.push_back(bos_id_);
            full.insert(full.end(), context_ids.begin(), context_ids.end());

            if (full.empty()) return {-1, ""};

            llama_batch batch = llama_batch_get_one(full.data(), full.size());
            if (llama_decode(ctx_, batch) != 0) {
                std::cerr << "[LlamaBackend] Initial decode failed" << std::endl;
                return {-1, "[error]"};
            }
            decoded_count_ = full.size();
        } else {
            int already_decoded = decoded_count_ - 1;
            if ((int)context_ids.size() > already_decoded) {
                for (int i = already_decoded; i < (int)context_ids.size(); i++) {
                    int tok = context_ids[i];
                    llama_batch batch = llama_batch_get_one(&tok, 1);
                    if (llama_decode(ctx_, batch) != 0) {
                        std::cerr << "[LlamaBackend] Incremental decode failed at idx "
                                  << i << std::endl;
                        return {-1, "[error]"};
                    }
                    decoded_count_++;
                }
            } else if ((int)context_ids.size() < already_decoded) {
                reset();
                return infer_step(context_ids, temperature);
            }
        }

        int new_token = llama_sampler_sample(sampler_, ctx_, -1);

        std::string text = detokenize(new_token);

        return {new_token, text};
    }

    std::string get_model_name() const override { return model_name_; }
    bool is_ready() const override { return ready_; }

    void reset() override {
        if (sampler_) llama_sampler_free(sampler_);
        if (ctx_) llama_free(ctx_);
        ctx_ = nullptr;
        sampler_ = nullptr;
        decoded_count_ = 0;
        create_sampler(0.7f);
        ensure_context();
    }
};

bool LlamaBackend::s_backend_initialized = false;
