#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <iostream>

class TokenCache {
public:
    struct Stats {
        int hits = 0;
        int misses = 0;
        int entries = 0;
        int tokens_stored = 0;
    };

private:
    struct Entry {
        std::vector<std::string> tokens;
    };

    std::map<std::string, Entry> cache;
    Stats stats;

    static std::string hash_context(const std::vector<std::string>& ctx) {
        std::string h;
        for (const auto& s : ctx) {
            h.append(s);
            h.push_back('\0');
        }
        return h;
    }

public:
    std::optional<std::string> try_get(const std::vector<std::string>& context) {
        auto h = hash_context(context);
        auto it = cache.find(h);
        if (it != cache.end() && !it->second.tokens.empty()) {
            stats.hits++;
            auto tok = it->second.tokens.front();
            it->second.tokens.erase(it->second.tokens.begin());
            if (it->second.tokens.empty()) {
                cache.erase(it);
            }
            return tok;
        }
        stats.misses++;
        return std::nullopt;
    }

    void store_batch(const std::vector<std::string>& base_context,
                     const std::vector<std::string>& generated_tokens) {
        if (generated_tokens.empty()) return;

        stats.tokens_stored += (int)generated_tokens.size();

        auto ctx = base_context;
        for (int i = 0; i < (int)generated_tokens.size(); ++i) {
            auto h = hash_context(ctx);
            std::vector<std::string> remaining(
                generated_tokens.begin() + i, generated_tokens.end());
            cache[h] = Entry{std::move(remaining)};
            ctx.push_back(generated_tokens[i]);
        }

        stats.entries = (int)cache.size();
    }

    void invalidate_all() {
        cache.clear();
        stats.entries = 0;
    }

    const Stats& get_stats() const { return stats; }

    void print_stats() const {
        std::cout << "[TokenCache] hits=" << stats.hits
                  << " misses=" << stats.misses
                  << " entries=" << stats.entries
                  << " tokens_stored=" << stats.tokens_stored << std::endl;
    }
};
