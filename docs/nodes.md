# Node Reference

## Primitive Nodes

| Node | Kind | Inputs | Output | Description |
|------|------|--------|--------|-------------|
| `ConstantNode` | `Constant` | — | INT | Outputs a configurable integer constant |
| `BoolConstantNode` | `BoolConst` | — | BOOL | Outputs a configurable boolean constant |
| `StringConstantNode` | `StrConst` | — | STRING | Outputs a configurable string constant |
| `AdderNode` | `Adder` | INT, INT | INT | Arithmetic addition |
| `ThresholdNode` | `Threshold` | INT | BOOL | `input >= threshold` comparison |
| `AndNode` | `And` | BOOL, BOOL | BOOL | Logical AND |
| `NotNode` | `Not` | BOOL | BOOL | Logical NOT |

## Type Conversion Nodes

| Node | Kind | Input | Output | Description |
|------|------|-------|--------|-------------|
| `Token2Str` | `Token2Str` | TOKEN | STRING | Decode token to string |
| `Str2Token` | `Str2Token` | STRING | TOKEN | Encode string as single token |
| `Str2Stream` | `Str2Stream` | STRING | TOKEN_STREAM | Wrap string as 1-element stream |
| `Stream2Str` | `Stream2Str` | TOKEN_STREAM | STRING | Join all tokens in stream to a string |
| `TokenMatch` | `TokenMatch` | TOKEN | BOOL | Test if token equals a configured value |

## Control Flow Nodes

| Node | Kind | Inputs | Output | Description |
|------|------|--------|--------|-------------|
| `MuxNode` | `Mux` | BOOL, STRING, STRING | STRING | If sel=true output a, else b |
| `UserInputNode` | `UserInput` | — | STRING | Configurable text input from CLI/API |
| `StopConditionNode` | `StopCond` | TOKEN | BOOL | Detects stop tokens, triggers SYS_BREAK |

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

### StopConditionNode

- **Input**: `TOKEN` — a single token to check
- **Output**: `BOOL` — true if the token matches any stop token
- **Config**: `stop_tokens` (array of strings) — default: `["\n\n", "<|im_end|>", "</s>"]`
- Should be connected to `SYS_BREAK` wire via an `And` node or directly
- When output is true and wired to break, `run` command will stop

### UserInputNode

- **Input**: none
- **Output**: `STRING` — the text set via CLI `input` command or API
- **Config**: `value` (string) — default value, can be set before running
- CLI: `input <text>` sets this value on all UserInput nodes in the circuit

### MuxNode

- **Inputs**: `BOOL sel`, `STRING a`, `STRING b`
- **Output**: `STRING` — `a` if sel is true, `b` otherwise
- Used for conditional routing, e.g., reset vs run paths

### Stream2StrNode

- **Input**: `TOKEN_STREAM`
- **Output**: `STRING` — concatenation of all tokens
- Useful for displaying generated text

## Node Factory

All nodes are registered with a `kind` string. This enables:
- **Deserialization**: circuits loaded from JSON use `kind` to instantiate nodes
- **Dynamic listing**: `list` CLI command shows all available kinds
- **No recompilation**: new `.json` circuits can use any registered node

## Design Principles

1. All LLM nodes are **combinational** (stateless) — state lives only in `Register` nodes
2. `LLMInferNode` emits exactly 1 token per tick — the clock-driven token machine invariant
3. Token accumulation uses Register feedback: `TokenAccum(reg_cur, new_token) → reg_nxt`
