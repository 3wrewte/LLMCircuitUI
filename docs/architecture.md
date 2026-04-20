# Architecture

## Three-Layer Design

```
┌─────────────────────────────────────────────────┐
│  Layer 1: Visual Graph (UI)                      │
│  blueprint.html — Canvas renderer, JS sync logic │
├─────────────────────────────────────────────────┤
│  Layer 2: Intermediate Representation (IR)       │
│  LogicEngine — Wire/Reg maps, Component graph,   │
│  topological sort, tick executor                 │
├─────────────────────────────────────────────────┤
│  Layer 3: Backend Runtime                        │
│  ExecuteEngine — InferenceBackend, TokenCache,   │
│  CLI debug interface                             │
└─────────────────────────────────────────────────┘
```

## Synchronous Tick Execution

Each tick proceeds in 3 phases:

### Phase 1: Register Propagation
```
for each register R:
    wire[R.cur] = R.val
```
Register current-value wires are written. This is the "read" phase — all combinational nodes see the same snapshot of state.

### Phase 2: Combinational Evaluation
```
for each node in topological order:
    read input wires → compute → write output wires
```
Nodes execute in dependency order. No node runs until all its predecessors have produced outputs. Memoization: if a node's inputs are unchanged since last tick, it may return a cached output.

### Phase 3: Register Capture (Clock Edge)
```
for each register R:
    R.val = wire[R.nxt]
```
Register next-value wires are sampled. This is the "write" phase — state transitions are atomic.

## Auto-Regressive Token Loop

The fundamental LLM circuit pattern:

```
SystemPrompt → Str2Stream ──┐
                              ├→ ContextBuild → LLMInfer → Token2Str
      Register[TOKEN_STREAM]─┘       │
         ↑                            ↓
         └──────── TokenAccum ←──────┘
```

Each tick:
1. ContextBuild assembles `[system_prompt, ...history_tokens...]`
2. LLMInfer produces 1 token from that context
3. TokenAccum appends the new token to history
4. Register captures the updated history for next tick

## Batch Emission (TokenCache)

When context grows only by appending (FIFO), we can predict future contexts:

1. Cache miss → call `infer_batch(K)` to generate K tokens at once
2. Store tokens keyed by predicted future context hashes
3. Cache hit → return pre-generated token without API call

This reduces API calls by up to K×, at the cost of wasted tokens when the prediction is wrong (cache invalidation).

## Wire & Register IDs

- IDs `0–9` are reserved for system wires (`SYS_CLK`, `SYS_RST`, `SYS_BREAK`)
- IDs `10+` are user wires, assigned sequentially by `add_wire()` / `add_register()`
- Each register owns 2 wires: `cur` (output, read each tick) and `nxt` (input, captured at clock edge)

## Compile Passes

Before execution, `compile()` runs a pipeline of passes:

### 1. Type Check
- Verifies every node's input/output wires match its schema
- Reports port count mismatches and type mismatches

### 2. Combinational Loop Detection
- Builds dependency graph ignoring register cur-wires (which break loops)
- Detects cycles using DFS with 3-color marking
- Reports the cycle path if found

### 3. Dead Code Elimination
- Traces from "driven" wires (register nxt, node inputs) backward
- Marks reachable nodes
- Reports count of unreachable nodes as a warning

### 4. Topological Sort
- Respects data dependencies (wire → consuming node)
- Register outputs and system wires are treated as sources
- Produces `execution_order` for the tick loop

Compile errors are accessible via `get_compile_errors()`. Severity is `"error"` (blocks execution) or `"warning"` (informational).

## IR Serialization

Circuits can be saved to / loaded from JSON:

```json
{
  "version": 1,
  "wires": [{"id": 100, "type": "STRING"}],
  "registers": [{"id": 108, "cur_wire": 109, "nxt_wire": 110, "init": {...}}],
  "nodes": [{"kind": "LLMInfer", "inputs": [105], "outputs": [106], "config": {...}}]
}
```

- `LogicEngine::serialize()` exports the current graph
- `LogicEngine::deserialize(json)` rebuilds a new engine from JSON
- `NodeFactory` maps `kind` strings to `Component` constructors
- All registered node kinds: `list` CLI command
