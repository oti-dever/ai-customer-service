根据详细分析，以下是完整的重构方案。

---

## 总体结论

当前 `python/` 目录已实现微信、千牛 RPA 的完整读写链路，具备 M5 单平台 PoC 所需的核心能力。但存在以下主要问题：

| 问题类型 | 具体表现 |
|---------|---------|
| **平台代码混入 common** | `common/` 下有 8 个 `qianniu_*` 和 7 个 `wechat_*` 专用模块 |
| **通用能力未抽象** | `NameStabilizer`、窗口锁、增量去重等在微信/千牛各自实现 |
| **布局解析耦合** | `layout_parser.py` 硬编码了平台特定的正则和阈值 |
| **目录职责模糊** | `service/` 与 `rpa/` 的边界不清，AI 服务与 RPA 分离但共享 DB |

本重构方案应以 [`桌面客服工作台系统_架构分析文档.md`](../direction/桌面客服工作台系统_架构分析文档.md) 为上位约束：

- `python/service/` 定位为 **Python 策略与 AI sidecar**，负责 AI 建议、规则判断、文本清洗、低频任务和调试接口。
- `python/rpa/` 定位为 **平台适配层的一种 Python / RPA 实现**，负责平台窗口发现、消息采集、草稿回填、发送结果观测和健康检测。
- C++ 客户端仍是 UI、统一模型、状态流转、本地缓存和高频事件分发的主控方。
- Python RPA 不直接污染 UI 和核心模型，只通过 IPC / RPA 服务层输出统一事件或执行受控命令。
- MVP 阶段默认不做无人值守自动发送，优先实现“采集 + AI 建议 + 草稿回填 + 人工确认”闭环。

**可复用的通用能力**（M5 PoC 直接可用）：

| 能力 | 模块 | 复用度 |
|-----|------|-------|
| Win32 输入模拟 | `input_sim.py` | 100% 通用 |
| Win32 窗口截图 | `screenshot.py` | 100% 通用 |
| Win32 窗口操作 | `win32_window.py` | 100% 通用 |
| OCR 引擎抽象 | `ocr_engine.py` | 100% 通用 |
| SQLite 助手 | `db_helper.py` | 90% 通用（需拆分 RPA 表操作） |
| 窗口互斥锁 | `window_lock.py` | 100% 通用 |
| 增量去重 | `incremental.py` | 100% 通用 |
| 布局解析框架 | `layout_parser.py` | 70% 可抽象（需解耦平台特化） |

---

## 推荐目录结构

```text
python/
├── rpa/
│   ├── __init__.py
│   ├── main.py                    # 入口（不变）
│   │
│   ├── core/                      # 🆕 通用基础设施
│   │   ├── __init__.py
│   │   ├── input_sim.py           # Win32 输入模拟
│   │   ├── screenshot.py          # Win32 截图
│   │   ├── win32_window.py        # Win32 窗口操作
│   │   ├── ocr_engine.py          # OCR 引擎抽象
│   │   ├── window_lock.py         # 平台窗口锁
│   │   ├── incremental.py         # 增量去重
│   │   ├── layout_parser.py       # 布局解析（通用框架）
│   │   ├── name_stabilizer.py     # 🆕 名称稳定器（从各平台提取）
│   │   └── rpa_console_log.py     # 日志格式化
│   │
│   ├── db/                        # 🆕 数据库层（拆分）
│   │   ├── __init__.py
│   │   ├── connection.py          # 连接管理（通用）
│   │   └── inbox_dao.py           # RPA 收件箱表操作
│   │
│   ├── platforms/                 # 🆕 平台适配器（清晰边界）
│   │   ├── __init__.py
│   │   ├── base.py                # 🆕 平台适配器基类
│   │   │
│   │   ├── qianniu/               # 千牛平台
│   │   │   ├── __init__.py
│   │   │   ├── window.py          # 窗口发现
│   │   │   ├── session.py         # 会话操作
│   │   │   ├── coords.py          # 坐标计算
│   │   │   ├── header.py          # 标题区解析
│   │   │   ├── chat_parser.py     # 聊天解析
│   │   │   ├── bubble_parser.py   # 气泡解析
│   │   │   ├── clicknium.py       # Clicknium 集成
│   │   │   ├── reader.py          # 消息读取器
│   │   │   └── writer.py          # 消息发送器
│   │   │
│   │   ├── wechat/                # 微信平台
│   │   │   ├── __init__.py
│   │   │   ├── window.py
│   │   │   ├── session.py
│   │   │   ├── capture.py
│   │   │   ├── ocr_ops.py
│   │   │   ├── reader.py
│   │   │   ├── reader_discover.py
│   │   │   └── writer.py
│   │   │
│   │   └── pdd/                   # 拼多多平台（M5 候选）
│   │       ├── __init__.py
│   │       ├── reader.py
│   │       └── writer.py
│   │
│   ├── config/                    # 配置文件（不变）
│   │   ├── qianniu_config.json
│   │   ├── wechat_config.json
│   │   └── pdd_config.json
│   │
│   └── _state/                    # 运行时状态（不变）
│
├── service/                       # AI 辅助服务（不变）
│   ├── __init__.py
│   ├── server.py
│   └── ai_suggestion.py
│
└── requirements.txt               # 🆕 统一依赖（从 rpa/requirements.txt 提升）
```

---

## 与主架构的职责边界

根据 [`桌面客服工作台系统_架构分析文档.md`](../direction/桌面客服工作台系统_架构分析文档.md)，Python 目录重构后应保持三条边界清晰：

| 模块 | 架构层级 | 主要职责 | 不做什么 |
|------|----------|----------|----------|
| `python/service/` | Python 策略与 AI 服务 | AI 建议、规则命中、文本清洗、风险标记、低频任务 | 不直接控制桌面 UI，不直接发送平台消息 |
| `python/rpa/core/` | RPA 通用基础设施 | Win32 输入、截图、OCR、窗口锁、增量去重、通用解析框架 | 不写入平台业务规则，不包含千牛/微信/拼多多特化逻辑 |
| `python/rpa/platforms/*` | 平台适配层 | 平台窗口发现、消息读取、草稿回填、发送状态观测、健康检查 | 不暴露平台字段给 UI，不绕过 C++ 统一模型 |
| `src/ipc/` / `src/services/rpa/` | C++ 主控边界 | 进程管理、IPC 协议、命令下发、事件接收、状态转发 | 不内嵌平台自动化细节 |

推荐链路：

```text
平台窗口 / Web 工作台
    ↓
python/rpa/platforms/{platform}/
    ↓ 统一事件 / 命令结果
src/services/rpa/ + src/ipc/
    ↓
MessageRouter / ConversationManager / Models::*
    ↓
AggregateChatForm
```

关键原则：

- Python RPA 可以作为本机 sidecar 或后续 RPA Worker，但 C++ 仍负责会话状态、消息状态和 UI 可见行为。
- 平台特有字段只允许保留在 `metadata` 或平台适配层内部，不进入 UI 专用判断。
- RPA 侧持久化仅用于游标、去重、运行状态和临时 inbox，不作为主消息库。
- 真正进入工作台的消息必须由 C++ 侧统一模型落盘和分发。

---

## 平台适配器协议对齐

架构文档中的平台插件协议：

```text
connect()
disconnect()
sync()
send_message()
fetch_history()
get_health()
```

Python RPA PoC 可采用更贴近自动化实现的接口，但需要明确映射关系，避免后续 IPC 协议漂移：

| 架构插件协议 | Python RPA 建议接口 | 说明 |
|-------------|--------------------|------|
| `connect()` | `start()` | 启动平台 reader / writer，检查窗口或浏览器上下文 |
| `disconnect()` | `stop()` | 停止采集和自动化任务，释放窗口锁 |
| `sync()` | `fetch_visible_conversations()` + `fetch_visible_messages()` | 对当前可见会话做低频同步，不承担全量历史同步 |
| `send_message()` | MVP 阶段优先 `prepare_reply_draft()` | 默认只回填草稿，人工确认后由受控命令触发真实发送 |
| `fetch_history()` | 后续扩展 `fetch_history(cursor)` | M5 可暂不实现，避免拖慢 PoC |
| `get_health()` | `health_check()` | 返回窗口、登录态、OCR、自动化工具、最近错误等健康信息 |

推荐基础接口：

```python
# python/rpa/platforms/base.py
from abc import ABC, abstractmethod

class PlatformAdapter(ABC):
    @abstractmethod
    def start(self) -> None: ...

    @abstractmethod
    def stop(self) -> None: ...

    @abstractmethod
    def health_check(self) -> dict: ...

    @abstractmethod
    def fetch_visible_conversations(self) -> list[dict]: ...

    @abstractmethod
    def fetch_visible_messages(self, conv_id: str) -> list[dict]: ...

    @abstractmethod
    def prepare_reply_draft(self, conv_id: str, text: str) -> dict: ...

    # 非 MVP 默认能力，只有在人工确认和风控边界完整后再启用。
    def send_message(self, conv_id: str, text: str, *, confirm_token: str) -> dict:
        raise NotImplementedError
```

---

## 统一事件输出

平台 reader / writer 不应直接操作 UI，也不应直接决定 C++ 会话状态。建议统一输出以下事件，由 `src/services/rpa/` 和 `MessageRouter` 归一化到 `Models::*`：

| 事件类型 | 触发来源 | 必要字段 |
|----------|----------|----------|
| `message_observed` | reader 发现新消息 | `platform`、`account_id`、`conversation_key`、`platform_msg_id`、`direction`、`content`、`timestamp`、`source_type`、`metadata` |
| `conversation_observed` | reader 发现或更新会话 | `platform`、`account_id`、`conversation_key`、`display_name`、`unread_count`、`last_message_at`、`metadata` |
| `account_health_changed` | health check / 登录态检测 | `platform`、`account_id`、`status`、`error_code`、`message` |
| `draft_prepared` | writer 草稿回填完成 | `platform`、`conversation_key`、`task_id`、`status`、`evidence` |
| `send_result_observed` | writer 观测发送结果 | `platform`、`conversation_key`、`task_id`、`status`、`platform_msg_id`、`error_message` |

幂等键建议：

```text
platform + account_id + conversation_key + platform_msg_id
```

如果平台无法提供稳定消息 id，则退化为：

```text
platform + account_id + conversation_key + timestamp_bucket + content_hash
```

`source_type` 建议与统一模型保持一致，例如：

- `rpa_ui_observed`：通过桌面 UI / OCR / UIA 观测到的消息。
- `rpa_dom_observed`：通过 Web DOM / CDP 观测到的消息。
- `manual_outbound`：客服在工作台人工确认后发出的消息。

---

## 审计与人工确认边界

架构文档要求自动化能力必须可审计、可回放、可关停。M5 / MVP 阶段建议遵守以下约束：

1. **默认只做草稿回填**
   - AI 建议只生成候选文本。
   - RPA writer 默认执行 `prepare_reply_draft()`，把内容放入目标平台输入框。
   - 最终发送由客服在 C++ 工作台确认，或由受控命令在明确授权后执行。

2. **真实发送必须有确认凭证**
   - 如果后续启用 `send_message()`，必须要求 `confirm_token`、`task_id`、目标会话校验信息和待发送文本。
   - 发送前校验窗口标题、账号、会话对象、输入框内容，避免焦点劫持和误发。

3. **所有写动作必须可审计**
   - 记录 `task_id`、`request_id`、平台、账号、会话、动作、文本摘要、风险标签、执行结果和错误原因。
   - 高风险文本只记录摘要或脱敏内容，不落明文敏感字段。

4. **平台级开关必须可用**
   - 支持禁用 reader、writer、草稿回填、真实发送。
   - 平台异常时只标记 degraded，不阻塞其它平台和人工接待主链路。

5. **失败时回退人工**
   - OCR、窗口定位、草稿回填或发送观测失败时，不重试高风险动作。
   - UI 提示人工处理，保留失败原因用于排查。

---

## 分阶段优化方案

### 第一阶段：最小可运行（1-2 天）

**目标**：不破坏现有功能，仅做最小结构调整，让 M5 PoC 可以基于此开发。

**步骤**：

1. **创建 `core/` 目录，移动纯通用模块**
   ```bash
   # 只移动 100% 通用的模块
   mkdir -p python/rpa/core
   # 移动：input_sim, screenshot, win32_window, ocr_engine, 
   #       window_lock, incremental, rpa_console_log
   ```

2. **保留 `common/` 目录暂存平台相关代码**
   - 不立即删除 `common/`，保持兼容
   - 在 `common/__init__.py` 添加重导出，保证旧 import 路径可用

3. **提取 `NameStabilizer` 为通用类**
   - 微信和千牛 reader 中各有一份，合并到 `core/name_stabilizer.py`

4. **验证千牛 reader/writer 仍可运行**

### 第二阶段：平台模块归整（2-3 天）

**目标**：将平台特化代码移入 `platforms/` 子目录。

**步骤**：

1. **创建 `platforms/qianniu/` 目录**
   ```python
   # python/rpa/platforms/qianniu/__init__.py
   from .reader import run_reader
   from .writer import run_writer
   ```

2. **移动千牛专用模块**
   - `common/qianniu_*.py` → `platforms/qianniu/*.py`
   - 更新内部 import

3. **创建 `platforms/wechat/` 目录**
   - 同理移动微信专用模块

4. **更新 `main.py` 入口**
   ```python
   if args.platform == "qianniu":
       from rpa.platforms.qianniu import run_reader, run_writer
   elif args.platform == "wechat":
       from rpa.platforms.wechat import run_reader, run_writer
   ```

5. **验证两个平台 reader/writer 可运行**

### 第三阶段：布局解析抽象（可选，1 天）

**目标**：`layout_parser.py` 解耦平台特化逻辑。

**步骤**：

1. **定义平台解析配置协议**
   ```python
   @dataclass
   class LayoutParserConfig:
       system_text_patterns: list[re.Pattern]
       header_pattern: re.Pattern | None
       left_threshold: float
       right_threshold: float
       merge_y_gap: float
   ```

2. **各平台提供配置实例**
   - `platforms/qianniu/layout_config.py`
   - `platforms/wechat/layout_config.py`

3. **`layout_parser.py` 改为接收配置参数**

### 第四阶段：M5 PoC 平台适配器（与 PoC 开发同步）

**目标**：为 M5 PoC 目标平台创建规范适配器。

根据 MVP 范围说明，推荐 PoC 平台优先级：
1. **拼多多网页版**（已有 `pdd_reader.py`）
2. **千牛 PC**（已有完整实现）

**适配器实现要求**：

- 以“平台适配器协议对齐”小节中的 `PlatformAdapter` 为准，不再在各平台重复定义接口。
- reader 输出 `message_observed`、`conversation_observed`、`account_health_changed`。
- writer 默认实现 `prepare_reply_draft()`，真实发送能力放到非 MVP 扩展。
- 所有命令结果必须带 `task_id`，便于 C++ `RpaService` 关联请求、回填状态和记录审计。
- PDD 网页版优先走 DOM / CDP / 指纹浏览器观测，`source_type` 使用 `rpa_dom_observed`。
- 千牛 / 微信 PC 优先走 UIA / OCR / 窗口自动化观测，`source_type` 使用 `rpa_ui_observed`。

---

## 可执行重构步骤（优先保证 PoC 可落地）

### 立即可做（第一阶段核心）

```bash
# 1. 创建 core 目录
mkdir -p python/rpa/core

# 2. 复制（非移动）纯通用模块到 core/
cp python/rpa/common/input_sim.py python/rpa/core/
cp python/rpa/common/screenshot.py python/rpa/core/
cp python/rpa/common/win32_window.py python/rpa/core/
cp python/rpa/common/ocr_engine.py python/rpa/core/
cp python/rpa/common/window_lock.py python/rpa/core/
cp python/rpa/common/incremental.py python/rpa/core/
cp python/rpa/common/rpa_console_log.py python/rpa/core/

# 3. 创建 core/__init__.py 导出
```

```python
# python/rpa/core/__init__.py
from .input_sim import (
    simulate_click, simulate_key, simulate_key_combo,
    set_clipboard_text, get_clipboard_text, ClipboardGuard,
    post_click, post_type_text, post_clear_text,
)
from .screenshot import capture_region, save_bgra_png, get_window_rect
from .win32_window import bring_to_foreground, get_foreground_window
from .ocr_engine import BaseOCREngine, PaddleOCREngine, RapidOCREngine, build_ocr_engine
from .window_lock import hold_platform_window_lock
from .incremental import IncrementalDetector, content_hash, make_platform_msg_id
from .rpa_console_log import rpa_log, rpa_phase, rpa_heartbeat
```

```python
# 4. 提取 NameStabilizer 到 core/name_stabilizer.py
# （从 wechat_reader.py 和 qianniu_reader.py 合并）
```

```python
# python/rpa/core/name_stabilizer.py
from typing import Optional

class NameStabilizer:
    """OCR 名称稳定器：连续 N 次一致才接受改名，减少会话抖动。"""
    
    def __init__(self, required_consistent: int = 2):
        self.current: Optional[str] = None
        self._required = required_consistent
        self._candidate: Optional[str] = None
        self._candidate_count: int = 0

    def update(self, name: Optional[str]) -> Optional[str]:
        if not name:
            return self.current
        if self.current is None:
            self.current = name
            return self.current
        if name == self.current:
            self._candidate = None
            self._candidate_count = 0
            return self.current
        if name == self._candidate:
            self._candidate_count += 1
            if self._candidate_count >= self._required:
                self.current = name
                self._candidate = None
                self._candidate_count = 0
        else:
            self._candidate = name
            self._candidate_count = 1
        return self.current

    def force_set(self, name: str) -> None:
        if name:
            self.current = name
            self._candidate = None
            self._candidate_count = 0
```

### 兼容性保护

```python
# python/rpa/common/__init__.py - 添加重导出保持兼容
# 暂时保留，让旧 import 路径仍可用
try:
    from ..core.input_sim import *
    from ..core.screenshot import *
    from ..core.ocr_engine import *
    from ..core.window_lock import *
    from ..core.incremental import *
    from ..core.name_stabilizer import NameStabilizer
except ImportError:
    pass  # 降级：core/ 尚未创建时使用本地模块
```

---

## 重构检查清单

| 阶段 | 检查项 | 验收标准 |
|-----|-------|---------|
| 第一阶段 | `core/` 模块可导入 | `from rpa.core import simulate_click` 成功 |
| 第一阶段 | 千牛 reader 可运行 | `python -m rpa.main --platform qianniu --reader-only` 正常 |
| 第一阶段 | 微信 reader 可运行 | `python -m rpa.main --platform wechat --reader-only` 正常 |
| 第二阶段 | `platforms/qianniu/` 建立 | 所有 `qianniu_*.py` 移入 |
| 第二阶段 | 旧 import 仍兼容 | `from rpa.common.qianniu_session import ...` 仍可用 |
| 第三阶段 | `layout_parser` 可配置化 | 平台特定正则从核心函数移出 |
| 第四阶段 | M5 PoC 适配器实现 | `PlatformAdapter` 接口完整实现 |
| 第四阶段 | 统一事件可输出 | reader / writer 输出 `message_observed`、`conversation_observed`、`draft_prepared` 等事件 |
| 第四阶段 | 人工确认边界清晰 | 默认只做草稿回填，真实发送需要确认凭证和审计记录 |

---

## 风险与缓解

| 风险 | 影响 | 缓解措施 |
|-----|-----|---------|
| 重构导致现有 RPA 不可用 | 阻断 M5 PoC | 先复制后移动，保留兼容层 |
| import 路径变更遗漏 | 运行时报错 | 在 `common/__init__.py` 添加重导出 |
| 过度工程化 | 拖慢 PoC 进度 | 第一阶段仅做最小必要调整 |
| Python RPA 绕过 C++ 主控 | 状态不一致、审计缺失 | 只通过 IPC / RPA 服务层输出统一事件和执行受控命令 |
| 自动化误发 | 产生业务风险 | MVP 默认草稿回填，真实发送必须带确认凭证和二次校验 |

---

**建议**：先完成第一阶段（最小可运行），确认千牛/微信 RPA 不受影响后，再根据 M5 PoC 选定的目标平台，逐步完成后续阶段。如果 M5 选择千牛作为 PoC 平台，可直接复用现有代码，重构优先级可降低；如果选择拼多多网页版，则优先建立 `platforms/pdd/` 并实现适配器接口。无论选择哪个平台，都应先打通“观测消息 → 统一事件 → C++ 落盘展示 → AI 建议 → 草稿回填 → 人工确认”的最小闭环，再考虑真实发送和更复杂的自动化策略。