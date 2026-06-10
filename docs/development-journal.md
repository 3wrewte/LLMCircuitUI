# Development Journal

## Project Evolution (P0 → P3)

### P0 — Core Dataflow Engine
- DataType system: INT, BOOL, STRING, TOKEN, TOKEN_STREAM, CONTEXT_BUFFER
- Value struct with type-safe factory methods (no implicit coercion)
- Core LLM nodes: LLMInferNode, TokenAccumNode, ContextBuildNode, Token2Str, Str2Token, Str2Stream, TokenMatch
- Primitive nodes: Adder, Constant, StringConstant, Threshold
- Demo auto-regressive circuit in main.cpp
- Stub mode (no API key → returns `[stub_N]`)

### P1 — Backend & Execution
- `InferenceBackend` virtual interface → `APIBackend` (libcurl, sync HTTP POST)
- SiliconFlow API (`api.siliconflow.com/v1`, model Qwen/Qwen3-8B) compatible with OpenAI chat format
- `TokenCache` with speculative batch caching — store K tokens from one API call, serve from cache for K-1 subsequent ticks
- `ExecuteEngine` orchestrator: CLI debug interface, tick loop, run loop
- Cache hit rate ~93% achieved during testing

### P2 — Node Factory & Compile
- REGISTER_NODE macro: auto-registration of components
- Serialize/deserialize: circuit.json format (wires, registers, nodes with kind/config)
- New nodes: UserInputNode, StopConditionNode, MuxNode, AndNode, NotNode, BoolConstNode, Stream2Str, ShowStrNode, ShowTokenStreamNode
- Compile passes: type check, combinational loop detection (DFS 3-color), dead code elimination, topological sort
- CLI: load/save/list/compile commands

### P3 — UI & Runtime (Current Phase)
- Canvas-based visual graph editor (blueprint.html)
- Three-layer architecture: Visual Graph → REST API (GraphController) → IR (LogicEngine)
- REST endpoints (15): graph, node_types, tick, run, stop, reset, sync, save, input, add_node, add_wire, remove_node, remove_wire, undo, redo
- Drag-to-create from palette, port-to-port wiring with type validation, double-click config editing
- System nodes: CLK (INT output), RST (BOOL output), BREAK (BOOL input)
- Delete node/wire, auto-polling during run, input box integration

---

## Critical Design Issues & Resolutions

### 1. Token Granularity: Character → Word Level (P3)

**Problem**: Initially, `APIBackend` split API responses into individual characters:
```
"Hello!" → ["H","e","l","l","o","!"]
```
This caused two cascading bugs:

1. **Context corruption**: The model saw `"M a c h i n e   l e a r n i n g"` as individual character-tokens in the assistant prefix, so it couldn't understand what it had generated before

2. **Token proliferation**: "Hello!  How can I" (23 chars) produced 23 ticks of single-char output, making the token stream useless for display and slowing execution

**Solution**: Rewrote `split_into_tokens()` to use **word-level splitting**:
- Split on spaces, tabs, and newlines
- Spaces and newlines become their own tokens
- Non-whitespace words stay intact as single tokens
- Result: "Hello! 😊 How can I" → 11 tokens instead of 23

This required corresponding fixes in:
- `APIBackend::infer()`: Context reconstruction of `assistant_so_far`
- Token display in UI: `cfgLines()` array joining

### 2. Context Reconstruction in API Calls (P3)

**Problem**: The original context reconstruction joined tokens with space separators:
```cpp
for (size_t i = 2; i < context.size(); ++i) {
    if (i > 2) assistant_so_far += " ";  // BAD: double-spaces!
    assistant_so_far += context[i];
}
```
Since spaces are now their own tokens (`" "`), this produced:
- `"Machine" + " " + " " + " " + "learning"` = `"Machine   learning"`

**Solution**: Remove space separators — the tokens already contain their whitespace:
```cpp
for (size_t i = 2; i < context.size(); ++i) {
    assistant_so_far += context[i];  // Direct concatenation
}
```

### 3. Newline Token Encoding (P3)

**Problem**: `split_into_tokens()` stored newlines as the literal two-char string `"\\n"` instead of the actual `\n` character. This meant the assistant prefix in API calls contained literal `\n` text, not real newlines.

**Solution**: Store the actual newline character:
```cpp
tokens.emplace_back(1, '\n');  // NOT "\\n"
```

### 4. BREAK Wire Connection (P3)

**Problem**: The BREAK system node is a boolean INPUT — it should be driven by a wire from some circuit output (e.g., StopCond). But:
1. System wires (SYS_CLK=0, SYS_RST=1, SYS_BREAK=2) are special and not driven by any instance
2. The `add_wire` handler had no mechanism to "connect a wire to SYS_BREAK"
3. The getGraph handler tried to render BREAK connections by scanning instance output wires for wire ID == 2 (SYS_BREAK), which never matched in practice

**Solution**: Added `break_source_wire` tracking in `LogicEngine`:
- Private field `int break_source_wire = -1`
- During `tick()`, after combinational evaluation: `wires[SYS_BREAK] = wires[break_source_wire]`
- `addWire` handler sets `engine->set_break_source(source_wire)` when connecting to BREAK system node
- `removeWire` handler resets `break_source_wire = -1` when disconnecting from BREAK
- `removeNode` handler checks if deleted node was the break source and resets
- `getGraph()` renders a wire from the break source node's output port to the BREAK system node
- **Serialization**: `break_source_wire` included in `serialize()`/`deserialize()` so undo/redo preserves it
- Public API: `set_break_source(id)`, `get_break_source()`

### 5. Register Input Wire Rendering (P3)

**Problem**: `GraphController::getGraph()` only generated wires for Instance `in_wires`, completely missing Register `nxt_wire` inputs. This meant:
- Register nodes appeared with no incoming wires in the UI
- Users couldn't see the data flow into registers

**Solution**: Added a second loop iterating registers and connecting `reg.nxt_wire` drivers to the Register UI node's input port 0.

### 6. Position Shifting Bug on Add/Remove (P3)

**Problem**: The UI node IDs include both instances (0..N-1) and non-instance nodes (registers N..N+R-1, system N+R..N+R+2). When adding an instance:
- All IDs >= old_n_inst shifted up by +1
- `node_positions` map wasn't updated
- Result: Register and system nodes visually "teleported" to wrong positions

**Solution**: In `addNode`: shift `node_positions` for all IDs >= old_n_inst by +1.
In `removeNode`: shift IDs > removed_id by -1, and skip the removed ID.

### 7. Text Wrapping in Config Display (P3)

**Problem**: `cfgLines(key, val, innerW)` was passed `innerW` (pixel width, ~160) as the `maxChars` parameter. The function expected a character count (24), so text lines overflowed the node border.

**Solution**: Changed to `cfgLines(key, val, 24)` to match `nodeH()` which already uses `maxChars = 24`.

### 8. Config Display for Arrays (P3)

**Problem**: `cfgLines()` joined array values with spaces: `val.slice(0, 20).join(' ')`. For token streams containing `["\n", "Hello!", " ", "😊"]`, this produced an unreadable mess with double spaces.

**Solution**: Join arrays directly (no separator) for compact display, then truncate at 40 chars with ellipsis.

### 9. tickCount Always Zero (P3)

**Problem**: `getGraph()` hardcoded `root["tickCount"] = 0`.

**Solution**: Added `get_tick_count()` accessor to ExecuteEngine, used in graph response.

### 10. Default API URL (P3)

Changed default from `api.siliconflow.cn` (China) to `api.siliconflow.com` (international) as per user preference.

---

## The API Tradeoff: Why Switch to llama.cpp

The current architecture was built around the OpenAI-compatible chat API, which forced several **architectural sacrifices**:

### Current API-Induced Constraints

1. **Token granularity is faked**: We don't have real LLM tokens. The `split_into_tokens()` function uses heuristic word splitting — spaces, newlines as boundaries. Real LLM tokenizers have vocabularies of 32K-128K tokens, where " learning" might be one token and "supercalifragilistic" might be 5 tokens. Our fake splitting means:
   - Token streams don't match model internal state
   - Cache keys are noisy (word tokens ≠ real tokens)
   - Cannot do advanced token manipulation (logit forcing, token steering)

2. **Context assembly is wasteful**: We send the *entire* conversation history on every API call, including `assistant` prefix reconstruction. With local inference, we'd have a real KV cache — send one token at a time, cache the computed state.

3. **Batch speculation is heuristic**: The TokenCache pre-generates K tokens and stores them keyed by context hash. With local inference, we could generate tokens lazily — the KV cache incrementally extends with each new token.

4. **No streaming support**: Current backend uses `stream=false`. Real LLM apps need token-by-token streaming for responsive UI.

5. **API latency is high**: Each API call is ~500ms-2s. Local inference on a modest GPU is ~10-20ms per token.

6. **No logit access**: Cannot inspect token probabilities, apply logit bias, or implement custom sampling strategies — all of which are trivial with llama.cpp.

### What llama.cpp Would Enable

- **Real token IDs**: Each token is an actual vocabulary index, matching the model's internal representation
- **KV cache**: Incremental inference — feed token N, get distribution for token N+1, store KV state
- **One token per tick**: The 1-token-per-tick architecture was designed FOR local inference. Each tick = one forward pass
- **Logit manipulation**: Token steering, classifier-free guidance, speculative decoding
- **No network dependency**: Deterministic, reproducible, offline-capable
- **Lower latency**: ~10-20ms per token vs 500ms+ for API

### What to Keep from Current Architecture

- **LogicEngine / Tick Loop**: Works identically — just swap the backend
- **Node types and schemas**: LLMInferNode's interface (context → token) stays the same
- **ContextBuildNode / TokenAccumNode**: Still needed, but context now holds real token IDs
- **GraphController / UI**: Completely unchanged
- **Compile passes**: All still valid
- **Undo/redo, serialization**: No changes needed

### Migration Plan

1. **New `InferenceBackend` subclass**: `LlamaBackend` wrapping llama.cpp C API
2. **Context format change**: Token IDs (int32) instead of word strings
3. **TokenCache rework**: Keyed by token-ID sequences instead of word strings
4. **KV cache per circuit**: Store llama_context per LLMInferNode instance
5. **LLMInferNode enhancement**: Accept logit bias, temperature per-tick
6. **Drop APIBackend** or keep as fallback

---

## Code Organization

```
LLMCircuitUI/
├── src/
│   ├── LogicEngine.h          # IR: types, Value, Component, LogicEngine, compile passes,
│   │                            serialize/deserialize, dynamic editing, ~1020 lines
│   ├── GraphController.h      # REST API layer: 15 endpoints, undo/redo, ~535 lines
│   ├── main.cpp               # App entry, circuit loading, ExecuteEngine init
│   └── ExecuteEngine/
│       ├── InferenceBackend.h  # Virtual interface for LLM backends
│       ├── APIBackend.h        # SiliconFlow/libcurl backend, word-level token splitting
│       ├── TokenCache.h        # Speculative batch cache (context hash → token list)
│       └── ExecuteEngine.h     # Orchestrator: run/stop loop, CLI, reset, config loading
├── web/
│   └── blueprint.html          # Canvas UI: palette, wiring, config panel, system nodes,
│                                 drag/drop, undo/redo, ShowStr display, ~405 lines
├── docs/
│   ├── architecture.md         # Three-layer design, tick phases, batch emission, compile passes
│   ├── cli.md                  # CLI commands and circuit.json format
│   ├── nodes.md                # All node types reference
│   ├── types.md                # Data type system
│   └── development-journal.md  # This file
├── circuit.json                # Default demo circuit (committed to git)
├── build/
│   ├── config.json             # API key (NOT in git — lives in build/ only)
│   └── circuit.json            # Working copy (modified by save)
└── CMakeLists.txt              # C++17, Drogon web framework, jsoncpp, libcurl
```

---

## Build Environment

- **OS**: Fedora 42, GCC, CMake
- **Dependencies**: Drogon (cloned in `libs/drogon/`), jsoncpp, libcurl, libcurl-devel
- **Build command**: `make -j$(nproc) LLM_Circuit_UI` from `build/` directory
- **CMake already configured** in build/

---

## Key API Endpoints (for reference)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/graph` | GET | Full circuit state: nodes, wires, positions, tickCount |
| `/api/node_types` | GET | Available node kinds for palette |
| `/api/tick` | POST | Advance one tick |
| `/api/run` | POST | Start auto-run loop |
| `/api/stop` | POST | Stop auto-run loop |
| `/api/reset` | POST | Reset all registers and clear cache |
| `/api/sync` | POST | Update node positions and configs |
| `/api/save` | POST | Save to circuit.json |
| `/api/input` | POST | Set UserInput node text |
| `/api/add_node` | POST | Create new node, auto-create wires |
| `/api/add_wire` | POST | Connect output port to input port |
| `/api/remove_node` | POST | Delete node + cascade-disconnect |
| `/api/remove_wire` | POST | Disconnect a port |
| `/api/undo` | POST | Undo (full snapshot restore) |
| `/api/redo` | POST | Redo |

---

## Undo/Redo Implementation

Snapshot-based system:
```cpp
// GraphController.h
void push_undo() {
    undo_stack.push_back(capture_snapshot());   // serialize engine + positions
    redo_stack.clear();
    if (undo_stack.size() > 50) undo_stack.erase(undo_stack.begin());  // limit 50
}

void restore_snapshot(const Json::Value& snap) {
    auto new_engine = LogicEngine::deserialize(snap["circuit"]);
    new_engine->compile();
    node_positions = parse_positions(snap["positions"]);
    engine = exec->replace_logic(std::move(new_engine));  // atomically swaps engine
}
```

- Push undo before every mutation (add/remove node, add/remove wire)
- Full snapshots (not delta-based) — simple but memory-safe
- 50-entry circular buffer
- Snapshot includes: full serialized circuit + node positions

---

## DataType Enum Values

| Value | Type | Color (UI) |
|-------|------|------------|
| 0 | INT | #3b82f6 (blue) |
| 1 | BOOL | #ef4444 (red) |
| 2 | STRING | #10b981 (green) |
| 3 | TOKEN | #f59e0b (amber) |
| 4 | TOKEN_STREAM | #e879f9 (purple) |
| 5 | CONTEXT_BUFFER | #c084fc (violet) |
| 6 | (unused) | #fb923c (orange) |

---

## System Node IDs

System nodes appear in the UI with IDs beyond instances and registers:
```
sys_base = n_instances + n_registers
sys_base + 0 = CLK   (output INT, wire ID 0)
sys_base + 1 = RST   (output BOOL, wire ID 1)
sys_base + 2 = BREAK (input BOOL, wire ID 2)
```

System nodes CAN be dragged in the UI but their positions reset on reload unless saved.

---

## Known Issues

1. **No wire delete cascade from UI**: Deleting a node in the UI calls `remove_node` which cascades wire cleanup, but the frontend doesn't immediately reflect deleted wires from other nodes. Fixed by `fetchGraph()` after each mutation.

2. **Register ID mapping**: The UI uses sequential indices for registers (0, 1, 2...) but the engine uses a `map<int, RegisterEntry>` with auto-incremented keys. The `get_registers()` accessor returns a const reference to the map sorted by key, so index-based access works but relies on the `reg_base + r_idx` pattern being consistent.

3. **No multi-select**: Only one node can be selected at a time.

4. **No copy/paste**: Nodes cannot be duplicated.

5. **System node positions**: Not persisted in circuit.json — only in undo/redo snapshots.

6. **BREAK wire**: Only one connection supported (single `break_source_wire`). Multi-input OR logic would need a dedicated node.

7. **Token display in UI**: Long token streams are truncated at 20 tokens and 40 chars in config display.

---

## Lessons Learned

1. **Token granularity is fundamental**: The entire architecture hinges on "what is a token?". The API-based approach forced fake word-tokens, losing the ability to do real token-level operations. The project was designed for 1-real-token-per-tick, and migrating to llama.cpp will restore that original vision.

2. **Chat API ≠ Token-level API**: OpenAI-compatible chat endpoints hide all internal state (KV cache, token IDs, logits). They're designed for chat applications, not for token-stepping architectures. The mismatch caused the character→word split hack.

3. **Full snapshots for undo are simple and reliable**: No differential state tracking bugs. The 50-entry limit prevents memory growth.

4. **Position maps need consistent ID domains**: The `node_positions` map spans instances, registers, and system nodes — any ID shift must remap all affected entries.

5. **Config.json in build/ only**: Critical to remember — the API key lives in `build/config.json`, not in the repo root. Documented in build instructions.

6. **BREAK wire as engine primitive**: Adding a dedicated `break_source_wire` field to LogicEngine (rather than a separate node) kept the implementation clean. The routing pattern (wire A → BREAK system node → SYS_BREAK wire) maps naturally.

7. **Build infrastructure is fragile**: Drogon requires cloning the whole repo, libcurl-devel is a manual dependency, and the cmake was pre-configured. Moving to llama.cpp will add its own build complexity.

---

## P4 — llama.cpp Local Backend Migration

### Motivation

The API-based architecture forced several compromises:
- Word-level heuristic tokenization instead of real LLM token IDs
- Full context re-sent on every API call (no KV cache)
- Speculative batch TokenCache as a workaround for latency
- No logit/probability access

Local inference via llama.cpp resolves all of these, enabling the original "one real token per tick" vision.

### DataType System Overhaul

New `TOKEN_ID` type (value 6) representing a single integer token ID from the model's vocabulary. This is distinct from `TOKEN` (value 3), which stores the text representation for display and stop-condition checking.

- `TOKEN_STREAM` changed from `vector<string>` to `vector<int>` — stores token ID sequences
- `CONTEXT_BUFFER` changed from `vector<string>` to `vector<int>` — stores assembled context as token IDs
- New factory methods: `Value::token_id(int)`, `Value::token_stream(vector<int>)`, `Value::context_buffer(vector<int>)`

### New InferenceBackend Interface

The old `infer(context, max_tokens, temperature) -> vector<string>` was replaced with:

```cpp
virtual vector<int> tokenize(const string& text, bool add_special) = 0;
virtual string detokenize(int token_id) = 0;
virtual pair<int,string> infer_step(const vector<int>& ctx, float temp) = 0;
virtual void reset() = 0;
```

### LlamaBackend Implementation

- Uses llama.cpp C API (`llama_model_load_from_file`, `llama_decode`, `llama_sampler_sample`)
- **Incremental inference**: tracks `decoded_count_` to only decode new tokens each tick, leveraging KV cache
- BOS management: LLMInferNode prepends BOS internally; TokenizerNode uses `add_special=false`
- `reset()` frees and recreates the `llama_context` (clears KV cache)
- Mode: `llama_model` shared globally, `llama_context` per LLMInferNode instance
- Supports `n_gpu_layers` config for GPU offload (Vulkan/CUDA)
- Stub mode when no model path configured: token IDs return -1, text returns `[stub_N]`

### New Nodes

| Node | Kind | Role |
|------|------|------|
| `TokenizerNode` | `Tokenizer` | STRING → TOKEN_STREAM (real token IDs via llama tokenizer) |
| `DetokenizeNode` | `Detokenize` | TOKEN_ID → STRING (detokenize via llama) |

### Modified Nodes

| Node | Change |
|------|--------|
| `LLMInferNode` | Dual output: TOKEN(text) + TOKEN_ID(int); callback returns `pair<string,int>` |
| `TokenAccumNode` | Input changed from TOKEN to TOKEN_ID; appends `int` to `token_ids` |
| `ContextBuildNode` | Operates on `vector<int>` instead of `vector<string>` |
| `Str2Stream` | Added `tokenize_callback` (backward compat); now produces `vector<int>` |
| `Stream2Str` | Added `detokenize_callback`; detokenizes each ID then joins |
| `ShowStream` | Added `detokenize_callback` for display |

### Removed Components

- `APIBackend.h` — libcurl/SiliconFlow backend, replaced by `LlamaBackend.h`
- `TokenCache.h` — speculative batch cache, made obsolete by KV cache
- `libcurl` dependency — no longer needed

### Tokenizer Consistency Rule

TokenizerNode always tokenizes with `add_special=false`. LLMInferNode manages BOS internally. This prevents duplicate BOS tokens when multiple token streams are concatenated by ContextBuild.

### GPU Acceleration

- **Vulkan**: requires `shaderc` package (for `glslc`), build with `-DGGML_VULKAN=ON`, set `n_gpu_layers=99`
- **CUDA**: requires CUDA toolkit, build with `-DGGML_CUDA=ON`
- Fallback: CPU-only works out of the box, sufficient for small models

### Build Changes

- Added llama.cpp as a git submodule dependency via `add_subdirectory(libs/llama.cpp)`
- Removed `find_package(CURL)` and `CURL::libcurl` linkage
- Added include paths for `llama.cpp/include` and `llama.cpp/ggml/include`
- CMake targets: `llama`, `ggml`, `ggml-base`, `ggml-cpu`

### Test Infrastructure

Test files in `tests/`:
- `test_datatype.cpp` — DataType enum, Value constructors, to_json/from_json roundtrip
- `test_nodes_ir.cpp` — Full auto-regressive circuit compile + tick with stub backend
- `test_llama_backend.cpp` — Real model loading, tokenization, detokenization, inference

### Web UI Updates

- TOKEN_ID color: `#fb923c` (orange), type label: `TKID`
- Node palette: `Tokenizer` and `Detokenize` added to Convert group
- Register config: token_ids displayed as comma-separated integers
- LLMInfer node: renders 2 output ports automatically from schema

### Config Format (new)

```json
{
    "model_path": "/path/to/model.gguf",
    "n_gpu_layers": 99,
    "temperature": 0.7,
    "verbose": true
}
```

### Circuit Topology (demo)

```
StrConst → Tokenizer → TOKEN_STREAM[int] ──┐
                                            ├→ ContextBuild → CONTEXT_BUFFER[int]
UserInput → Tokenizer → TOKEN_STREAM[int]──┘         │
                           Register[TOKEN_STREAM] ────┘   │
                                                          v
                          LLMInfer → TOKEN[str] (display, stop)
                                   → TOKEN_ID[int] → TokenAccum → Reg[nxt]
                                                    → Detokenize → STRING
```

### Known Issues (P4)

1. **Qwen3.5 BOS token = 11 (comma)**: The model's `add_bos_token` is false, so BOS=11 is a placeholder. Inference works correctly without BOS prepending.
2. **CPU model loading is slow** (~30s for 3.9GB f16) due to DDR4-2133 memory bandwidth. Vulkan GPU loading is significantly faster.
3. **No multi-model support**: Single `LlamaBackend` per `ExecuteEngine`. Multiple LLMInfer nodes share the same model.
4. **No logit bias / custom sampling yet**: Currently uses temperature-only sampler chain. Token steering architecture is planned but not implemented.
5. **GraphController unchanged**: self-adapts by reading Component schemas dynamically. No special handling needed for new types.

### Lessons (P4)

1. **Real token IDs are transformative**: The circuit now operates on the model's actual vocabulary. Context buffers contain real token ID sequences that the model can understand natively.

2. **KV cache makes incremental inference trivial**: One `llama_decode` per tick, no context reconstruction needed. This is the architecture the project was designed for.

3. **CPU fallback is essential**: GPU toolchain issues (missing `glslc`, CUDA toolkit) are common. The architecture degrades gracefully to CPU-only with the same code path.

4. **Header-only LogicEngine was a good design choice**: All node changes (new DataType, new nodes, modified schemas) were self-contained in one file. No build system changes needed for the IR layer.
