#pragma once

#include "InferenceBackend.h"
#include <curl/curl.h>
#include <json/json.h>
#include <iostream>
#include <string>
#include <vector>

class APIBackend : public InferenceBackend {
    std::string api_key;
    std::string base_url;
    std::string model;
    bool ready = false;

    static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buf = static_cast<std::string*>(userdata);
        buf->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    }

    std::vector<std::string> split_into_tokens(const std::string& text) {
        std::vector<std::string> tokens;
        std::string current;

        for (char c : text) {
            if (c == ' ' || c == '\t') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.emplace_back(1, c);
            } else if (c == '\n') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.emplace_back(1, '\n');
            } else {
                current += c;
            }
        }
        if (!current.empty()) tokens.push_back(current);

        if (tokens.empty()) tokens.emplace_back("");

        return tokens;
    }

public:
    APIBackend(std::string key, std::string url, std::string mdl)
        : api_key(std::move(key)), base_url(std::move(url)), model(std::move(mdl)) {
        ready = !api_key.empty() && api_key != "PASTE_YOUR_API_KEY_HERE";
        if (ready) {
            std::cout << "[APIBackend] ready, model=" << model << " url=" << base_url << std::endl;
        } else {
            std::cout << "[APIBackend] NOT ready — api_key not configured" << std::endl;
        }
    }

    std::vector<std::string> infer(
        const std::vector<std::string>& context,
        int max_tokens,
        float temperature) override {

        if (!ready) {
            std::cerr << "[APIBackend] cannot infer — not configured" << std::endl;
            return {};
        }

        // Context format:
        //   context[0] = system prompt string
        //   context[1] = user input string (optional)
        //   context[2..N-1] = previously generated token strings (the assistant reply so far)
        //
        // We reconstruct proper chat messages so the model sees a coherent conversation.

        Json::Value body;
        body["model"] = model;
        body["max_tokens"] = max_tokens;
        body["temperature"] = temperature;
        body["stream"] = false;

        Json::Value messages(Json::arrayValue);

        std::string system_prompt;
        std::string user_text;
        std::string assistant_so_far;

        if (!context.empty()) {
            system_prompt = context[0];
        }
        if (context.size() > 1) {
            user_text = context[1];
        }
        for (size_t i = 2; i < context.size(); ++i) {
            assistant_so_far += context[i];
        }

        // System message
        if (!system_prompt.empty()) {
            Json::Value sys_msg;
            sys_msg["role"] = "system";
            sys_msg["content"] = system_prompt;
            messages.append(sys_msg);
        }

        // User message
        Json::Value user_msg;
        user_msg["role"] = "user";
        user_msg["content"] = user_text.empty() ? "Hello" : user_text;
        messages.append(user_msg);

        // Assistant prefix (previously generated tokens)
        if (!assistant_so_far.empty()) {
            Json::Value asst_msg;
            asst_msg["role"] = "assistant";
            asst_msg["content"] = assistant_so_far;
            messages.append(asst_msg);
        }

        body["messages"] = messages;

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        auto body_str = Json::writeString(wb, body);

        std::string auth_header = "Authorization: Bearer " + api_key;
        std::string url = base_url + "/chat/completions";

        std::cout << "[APIBackend] POST " << url << " max_tokens=" << max_tokens
                  << " context_parts=" << context.size()
                  << " asst_prefix=" << assistant_so_far.size() << " chars" << std::endl;

        std::string response;
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "[APIBackend] curl_easy_init failed" << std::endl;
            return {};
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[APIBackend] curl error: " << curl_easy_strerror(res) << std::endl;
            return {};
        }

        std::cout << "[APIBackend] HTTP " << http_code << " (" << response.size() << " bytes)" << std::endl;

        Json::CharReaderBuilder rb;
        Json::Value root;
        std::istringstream iss(response);
        std::string errors;
        if (!Json::parseFromStream(rb, iss, &root, &errors)) {
            std::cerr << "[APIBackend] JSON parse error: " << errors << std::endl;
            std::cerr << "[APIBackend] raw: " << response.substr(0, 500) << std::endl;
            return {};
        }

        if (!root.isObject()) {
            std::cerr << "[APIBackend] non-JSON response: " << response.substr(0, 200) << std::endl;
            return {};
        }

        if (root.isMember("error")) {
            std::cerr << "[APIBackend] API error: " << root["error"].toStyledString() << std::endl;
            return {};
        }

        std::string content;
        try {
            if (!root.isMember("choices") || !root["choices"].isArray() || root["choices"].empty()) {
                std::cerr << "[APIBackend] no choices in response" << std::endl;
                std::cerr << "[APIBackend] raw: " << response.substr(0, 500) << std::endl;
                return {};
            }
            auto& choice = root["choices"][0];
            content = choice["message"]["content"].asString();
        } catch (const std::exception& e) {
            std::cerr << "[APIBackend] response parse exception: " << e.what() << std::endl;
            std::cerr << "[APIBackend] raw: " << response.substr(0, 500) << std::endl;
            return {};
        }

        std::cout << "[APIBackend] response (" << content.size() << " chars): "
                  << content.substr(0, 120) << (content.size() > 120 ? "..." : "") << std::endl;

        auto tokens = split_into_tokens(content);
        std::cout << "[APIBackend] split into " << tokens.size() << " word-tokens" << std::endl;

        return tokens;
    }

    std::string get_model_name() const override { return model; }
    bool is_ready() const override { return ready; }
};
