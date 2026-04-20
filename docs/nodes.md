# Node Reference

## Primitive Nodes

| Node | Inputs | Output | Description |
|------|--------|--------|-------------|
| `ConstantNode` | — | INT | Outputs a configurable integer constant |
| `StringConstantNode` | — | STRING | Outputs a configurable string constant |
| `AdderNode` | INT, INT | INT | Arithmetic addition |
| `ThresholdNode` | INT | BOOL | `input >= threshold` comparison |

## Type Conversion Nodes

| Node | Input | Output | Description |
|------|-------|--------|-------------|
| `Token2Str` | TOKEN | STRING | Decode token to string |
| `Str2Token` | STRING | TOKEN | Encode string as single token |
| `Str2Stream` | STRING | TOKEN_STREAM | Wrap string as 1-element stream |
| `TokenMatch` | TOKEN | BOOL | Test if token equals a configured value |

## LLM Nodes

### LLMInferNode

- **Input**: `CONTEXT_BUFFER` — assembled prompt/context
- **Output**: `TOKEN` — exactly 1 token per tick
- **Config**: `model` (string), `temperature` (float)
- **Callback**: `infer_callback(context_tokens) → token_string`
  - If no callback is set, outputs stub tokens `[stub_0]`, `[stub_1]`, ...
  - The ExecuteEngine sets this callback to route through `TokenCache → InferenceBackend`
- This is the only node that triggers LLM API calls

### TokenAccumNode

- **Inputs**: `TOKEN_STREAM`, `TOKEN`
- **Output**: `TOKEN_STREAM`
- Appends the input token to the end of the input stream
- Primary use: accumulate generated tokens into history (via Register feedback)

### ContextBuildNode

- **Inputs**: N × `TOKEN_STREAM` (N is configurable via `num_inputs`)
- **Output**: `CONTEXT_BUFFER`
- Concatenates all input streams in port order into a flat context buffer
- Typical layout: port 0 = system prompt, port 1 = conversation history

## Design Principles

1. All LLM nodes are **combinational** (stateless) — state lives only in `Register` nodes
2. `LLMInferNode` emits exactly 1 token per tick — the clock-driven token machine invariant
3. Token accumulation uses Register feedback: `TokenAccum(reg_cur, new_token) → reg_nxt`
