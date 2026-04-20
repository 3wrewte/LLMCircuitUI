# LLM Circuit CLI 使用指南

程序启动后会进入交互式命令行，同时启动 HTTP 服务（`localhost:8080`）。

## 基本用法

### 启动程序

```bash
./build/LLM_Circuit_UI
```

程序会自动寻找当前目录下的 `circuit.json`，如果找到则加载，否则构建内置 demo 电路。

### 运行一次推理

```
> input 你好，请介绍一下你自己
> run 100
```

`run` 命令会持续 tick 直到遇到停止条件或达到上限（默认 1000 ticks）。

### 查看结果

```
> regs          # 查看所有寄存器的当前值
> wires         # 查看所有 wire 的值
> nodes         # 查看所有节点和配置
```

### 复位

```
> reset         # 清空寄存器+缓存，回到初始状态
```

之后可以重新 `input` + `run` 开始新一轮推理。

## 完整命令列表

| 命令 | 缩写 | 说明 |
|------|------|------|
| `tick [N]` | `t` | 执行 N 个 tick（默认 1） |
| `run [N]` | `r` | 持续执行，遇到 BREAK 或达到 N（默认 1000）停止 |
| `input <文本>` | `i` | 设置 UserInput 节点的值 |
| `wires` | `w` | 显示所有 wire 的值 |
| `nodes` | `n` | 显示所有节点和配置 |
| `regs` | | 显示所有寄存器的值 |
| `cache` | `c` | 显示 TokenCache 统计（命中率等） |
| `load <文件>` | | 从 JSON 文件加载电路 |
| `save [文件]` | | 保存电路到 JSON（不指定路径则保存到上次 load 的位置） |
| `reset` | | 复位所有寄存器 + 清除缓存 |
| `compile` | | 重新运行编译 pass（类型检查、环路检测等） |
| `list` | `ls` | 列出所有可用的节点类型 |
| `verbose on\|off` | `v` | 开关详细输出 |
| `help` | `h` | 显示帮助 |
| `quit` | `q` | 退出 |

## 典型工作流

### 场景 1：快速对话

```
> input What is machine learning?
> run 200
  ╔══ TICK 1 ══╗
    [CACHE MISS] calling API batch=8
    [BATCH] stored 30 tokens in cache
  ╚════════════╝
    Reg[108] = stream[1]{"\n"}
  ...
  ╔══ TICK 28 ══╗
    [CACHE MISS] calling API batch=8
  ...
> regs
  Reg[108] = stream[200]{...}
> cache
  [TokenCache] hits=190 misses=2 entries=...
```

### 场景 2：加载自定义电路

```
> load my_circuit.json
  Loaded: my_circuit.json (5 nodes, 1 regs)
> input Hello world
> run 50
> save my_circuit_v2.json
```

### 场景 3：调试

```
> compile
  warning: 2 unreachable node(s) detected
  Compile OK
> verbose off
> run 500
> verbose on
> regs
> cache
  [TokenCache] hits=480 misses=3 entries=...
```

### 场景 4：多轮对话

```
> input Tell me about AI
> run 100
> reset           # 清空历史
> input Tell me about physics
> run 100
```

## 电路文件格式 (circuit.json)

```json
{
  "version": 1,
  "wires": [
    { "id": 100, "type": "STRING" },
    { "id": 101, "type": "TOKEN_STREAM" }
  ],
  "registers": [
    {
      "id": 108,
      "cur_wire": 109,
      "nxt_wire": 110,
      "init": { "type": "TOKEN_STREAM", "value": [] }
    }
  ],
  "nodes": [
    {
      "kind": "StrConst",
      "inputs": [],
      "outputs": [100],
      "config": { "text": "You are a helpful AI." }
    },
    {
      "kind": "LLMInfer",
      "inputs": [105],
      "outputs": [106],
      "config": { "model": "Qwen/Qwen3-8B", "temperature": 0.7 }
    }
  ]
}
```

修改 `circuit.json` 后重启程序即可加载新电路，无需重编译。

## 配置文件 (config.json)

```json
{
  "api_key": "sk-xxx",
  "base_url": "https://api.siliconflow.com/v1",
  "model": "Qwen/Qwen3-8B",
  "batch_size": 8,
  "temperature": 0.7,
  "verbose": true
}
```

| 字段 | 说明 |
|------|------|
| `api_key` | SiliconFlow API Key |
| `base_url` | API 地址（国内版 `.cn`，国际版 `.com`） |
| `model` | 模型名 |
| `batch_size` | 打包发射的预生成 token 数 |
| `temperature` | 推理温度 |
| `verbose` | 是否输出详细 tick 信息 |
