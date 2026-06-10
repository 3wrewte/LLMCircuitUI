# LLM Circuit UI

A visual programming environment where LLM inference is modeled as a **clocked digital circuit** — each tick generates exactly one token.

Think of it as Verilog/RTL for LLM agents. You place nodes, wire ports, and watch tokens flow through the circuit clock cycle by clock cycle.

## Concept

Traditional LLM apps treat the model as a black-box function call. This project breaks that open: the LLM becomes a combinational logic element in a synchronous digital circuit. State lives only in Register nodes. Wires carry typed values between components. A global clock drives token-by-token execution.

```
StrConst → Tokenizer ──┐
                       ├→ ContextBuild → LLMInfer → TOKEN (display)
UserInput → Tokenizer ─┘         ↑                    ↓
                                  │               TOKEN_ID → TokenAccum → Register
                                  └─────────────────────────────────────┘
```

Each clock tick: registers propagate values, combinational nodes compute, then registers capture results. One token emerges per tick — the model's own auto-regressive loop becomes visible and controllable.

## Features

- **Token-by-token visualization**: Watch every token as it flows through the circuit
- **Canvas-based node editor**: Drag-and-drop nodes, port-to-port wiring with type validation
- **Real LLM token IDs**: Direct integration with llama.cpp, using the model's actual vocabulary
- **Incremental inference**: KV cache persists across ticks — only one `llama_decode` per new token
- **20+ node types**: LLMInfer, Tokenizer, Detokenize, ContextBuild, TokenAccum, logic gates, type converters, and more
- **Compile passes**: Type checking, combinational loop detection, dead code elimination, topological sort
- **Undo/redo**: Full snapshot-based, 50-entry history
- **Circuit serialization**: Save/load circuits as JSON
- **GPU acceleration**: Vulkan (tested) and CUDA backends via llama.cpp
- **CPU fallback**: Runs on CPU if no GPU toolchain is available

## Architecture

```
Layer 1: Visual Graph (web/blueprint.html)
        Canvas renderer, drag-to-create, port wiring, config panel
        ↓ REST API
Layer 2: IR (src/LogicEngine.h)
        DataType system, Component graph, compile passes, tick executor
        ↓ callback
Layer 3: Backend Runtime (src/ExecuteEngine/)
        LlamaBackend (llama.cpp), ExecuteEngine orchestrator, CLI
```

## Quick Start

### Prerequisites

- **Compiler**: GCC 13+ or Clang 17+, CMake 3.10+
- **Dependencies**: jsoncpp (bundled with Drogon), OpenMP
- **GPU (optional)**: Vulkan SDK (shaderc package) or CUDA toolkit
- **Model**: Any GGUF-format LLM (Qwen3.5-2B tested)

### Build

```bash
# Clone with submodules
cd LLMCircuitUI

# CPU-only build (works out of the box)
cmake -B build
make -j$(nproc) LLM_Circuit_UI -C build

# Vulkan GPU build
cmake -B build -DGGML_VULKAN=ON
make -j$(nproc) LLM_Circuit_UI -C build
```

### Configuration

Edit `build/config.json`:

```json
{
    "model_path": "/absolute/path/to/model.gguf",
    "n_gpu_layers": 99,
    "temperature": 0.7,
    "verbose": true
}
```

Set `model_path` to `""` to run in stub mode (no inference, useful for UI testing).

### Run

```bash
cd build
./LLM_Circuit_UI
```

- **Web UI**: `http://localhost:8080/blueprint.html`
- **CLI**: active in the terminal (type `help` for commands)
- **REST API**: 15 endpoints at `/api/*`

The default circuit (`circuit.json`) implements a simple chatbot: system prompt + user input → auto-regressive generation with stop-condition detection.

## CLI Commands

```
tick [N]    — Advance N ticks (default 1)
run [N]     — Run loop up to N ticks, stop on BREAK
wires       — Print all wire values
nodes       — List nodes with configs
regs        — Show register state
list        — Show available node types
reset       — Reset registers and clear KV cache
input <txt> — Set UserInput node text
load <path> — Load circuit JSON
save <path> — Save circuit JSON
verbose off — Disable per-tick output
quit        — Shutdown
```

## REST API

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/graph` | GET | Full circuit state (nodes, wires, positions, tickCount) |
| `/api/node_types` | GET | Available node kinds with port schemas |
| `/api/tick` | POST | Advance one tick |
| `/api/run` | POST | Start auto-run loop |
| `/api/stop` | POST | Stop auto-run loop |
| `/api/reset` | POST | Reset registers and clear KV cache |
| `/api/sync` | POST | Update node positions and configs |
| `/api/save` | POST | Save to circuit.json |
| `/api/input` | POST | Set UserInput node text |
| `/api/add_node` | POST | Create a new node |
| `/api/add_wire` | POST | Connect output port to input port |
| `/api/remove_node` | POST | Delete node (cascades wire cleanup) |
| `/api/remove_wire` | POST | Disconnect a port |
| `/api/undo` | POST | Undo last mutation |
| `/api/redo` | POST | Redo |

## Data Types

| Type | Color | Description |
|------|-------|-------------|
| INT | Blue | Integer |
| BOOL | Red | Boolean |
| STRING | Green | UTF-8 string |
| TOKEN | Amber | Token text (for display) |
| TOKEN_ID | Orange | Token vocabulary index |
| TOKEN_STREAM | Purple | Sequence of token IDs |
| CONTEXT_BUFFER | Violet | Assembled context for inference |

## Node Categories

**LLM**: LLMInfer, TokenAccum, ContextBuild, StopCond, Tokenizer, Detokenize

**Convert**: Str2Stream, Stream2Str, Str2Token, Token2Str, TokenMatch

**Logic**: Mux, And, Not, Threshold, Adder

**Input**: StrConst, Constant, BoolConst, UserInput

**Display**: ShowStr, ShowStream

## Circuit Format

Circuits are stored as JSON:

```json
{
    "version": 1,
    "wires": [{"id": 100, "type": "STRING"}],
    "registers": [{"id": 108, "cur_wire": 109, "nxt_wire": 110, "init": {...}}],
    "nodes": [
        {"kind": "Tokenizer", "inputs": [100], "outputs": [102]},
        {"kind": "LLMInfer", "inputs": [104], "outputs": [105, 111],
         "config": {"model": "Qwen3.5-2B", "temperature": 0.7}}
    ]
}
```

## Project Status

The project is in active development. The core engine is functional with real LLM token IDs via llama.cpp. Future directions include:

- Logit bias / token steering nodes
- Multi-model support (multiple backend instances)
- Streaming token output in web UI
- Circuit templates and copy/paste
- Conditional branching circuits (Mux-driven control flow)


