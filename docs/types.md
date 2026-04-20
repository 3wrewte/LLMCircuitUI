# Data Types

## DataType Enum

| ID | Type | Description |
|----|------|-------------|
| 0 | `INT` | Integer value |
| 1 | `BOOL` | Boolean value |
| 2 | `STRING` | Arbitrary UTF-8 string |
| 3 | `TOKEN` | Single LLM token (stored as string) |
| 4 | `TOKEN_STREAM` | Ordered sequence of tokens |
| 5 | `CONTEXT_BUFFER` | Assembled prompt/context for LLM inference |

## Value Representation

```cpp
struct Value {
    DataType type;
    int i;                          // INT
    bool b;                         // BOOL
    std::string s;                  // STRING / TOKEN
    std::vector<std::string> tokens; // TOKEN_STREAM / CONTEXT_BUFFER
};
```

- `TOKEN` uses `.s` — the decoded text of one token
- `TOKEN_STREAM` and `CONTEXT_BUFFER` share the same `.tokens` storage — an ordered `vector<string>`
- `CONTEXT_BUFFER` is semantically distinct: it represents a ready-to-send prompt, while `TOKEN_STREAM` is an evolving sequence

## Type Flow Rules

- Wire types are assigned at creation and never change
- Node port types are declared in `get_input_schema()` / `get_output_schema()`
- A wire may only connect an output port to an input port of the **same type**, except:
  - `STRING → TOKEN` (via `Str2Token`)
  - `STRING → TOKEN_STREAM` (via `Str2Stream`)
  - `TOKEN → STRING` (via `Token2Str`)
  - No implicit coercion — all conversions must pass through explicit nodes

## System Wires

| ID | Name | Type | Description |
|----|------|------|-------------|
| 0 | `SYS_CLK` | INT | Clock tick counter |
| 1 | `SYS_RST` | BOOL | Reset signal |
| 2 | `SYS_BREAK` | BOOL | Break/run-halt signal |
