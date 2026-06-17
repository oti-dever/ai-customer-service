# 微信 RPA 重构与聚合接入方案

## 1. 背景

当前工程中微信 RPA 已经存在两套形态：

- `python/rpa/platforms/wechat/`：面向聚合接待链路的 Reader / Writer，但实现依赖大量 OCR、区域校准、窗口锁、轮询和兼容包装，链路较重。
- `python/rpa/tools/wechat_workbench_service.py` + `src/services/wechat/` + `src/ui/wechatworkbenchdialog.*`：独立微信工作台，用于会话列表、消息读取、AI 建议和人工发送。

后续方向应与 [`桌面客服工作台系统_架构分析文档.md`](../direction/桌面客服工作台系统_架构分析文档.md) 对齐：C++ 聚合界面是唯一主工作台，Python 作为平台适配 sidecar 输出统一事件和执行受控命令。微信工作台不再作为长期产品入口保留。

`D:/wechat` 中的实现已经验证了更轻量的微信自动化路线：以 UIA 控件树为主，Win32 / 截图 / 颜色方差作为补充，不再依赖大面积 OCR 轮询。新微信 RPA 应以这套实现方式为参考，重写现有微信链路。

## 2. 目标形态

最终形态：

- 微信 RPA 是一个平台适配器，不是独立工作台。
- 聚合接待界面是微信消息展示、AI 辅助、人工确认发送的唯一主界面。
- Python 负责微信窗口发现、会话检测、消息读取、草稿回填、受控发送、健康检查和调试证据。
- C++ 负责统一会话模型、消息落盘、UI 刷新、发送任务创建、状态流转和审计。
- 数据库用于持久化、补偿和审计，不再作为 Python 与 C++ 的实时消息总线。
- `rpa_inbox_messages` 可以短期保留为兼容和补偿表，但不应继续作为主链路轮询入口。

不再保留的长期形态：

- 不再保留独立微信 RPA 工作台作为客服使用入口。
- 不再维护一套独立的微信工作台消息读取和发送协议。
- 不再让 Python 直接污染 UI 层或绕过 C++ 统一模型写业务消息。

## 3. 总体链路

推荐链路：

```text
微信 PC 客户端
    -> Python 微信适配器（UIA / Win32 / 截图辅助）
    -> IPC 事件推送
    -> C++ RpaService / IpcService
    -> MessageRouter / ConversationManager
    -> SQLite 统一模型落盘
    -> AggregateChatForm 聚合界面展示
```

出站链路：

```text
AggregateChatForm 输入 / AI 建议
    -> ConversationManager / MessageRouter 创建发送任务
    -> C++ 下发 RPA 命令
    -> Python 微信适配器回填草稿或受控发送
    -> Python 返回 draft_prepared / send_result_observed
    -> C++ 更新消息状态和审计记录
```

## 4. 参考实现对齐

`D:/wechat` 中可参考的模块边界：

| 参考模块 | 建议迁移含义 | 说明 |
|---|---|---|
| `detector.py` | 微信窗口、会话列表、未读会话检测 | 优先 UIA，按 `session_list` 等控件识别，不走红点 OCR 主链路 |
| `reader.py` | 当前聊天消息读取和角色判断 | 读取 `chat_message_list`，文本来自 UIA，方向用控件 / 截图方差辅助判断 |
| `sender.py` | 输入框、发送按钮、草稿填充和发送 | 优先输入框和按钮控件，必要时降级到 Win32 / 剪贴板 |
| `plugin.py` | 单轮处理和监控循环编排 | 可拆成 adapter runtime，不直接调用 AI 生成最终回复 |
| `utils/click.py` | 点击降级策略 | Windows 消息、Invoke、Click 分层降级 |
| `utils/stability.py` | 失败保护和调试证据 | 连续失败冷却、调试截图、异常降级 |

注意：不建议把 `D:/wechat` 原样复制成新工作台。应提取其自动化内核和模块边界，接入当前项目的 IPC、统一模型、聚合界面和审计链路。

## 5. Python 微信适配器职责

建议新微信适配器内部拆分：

```text
python/rpa/platforms/wechat/
  adapter.py       # 生命周期、事件输出、命令入口
  detector.py      # 微信窗口、账号状态、会话列表、未读检测
  reader.py        # 消息读取、消息归一化、角色判断
  sender.py        # 草稿回填、受控发送、发送结果观测
  events.py        # 统一事件结构和序列化
  config.py        # 微信适配配置
  stability.py     # 失败保护、冷却、调试证据
```

Python 负责：

- 发现微信窗口和 UIA 控件。
- 监听或轮询微信可见会话和可见消息。
- 将平台观察结果归一化成统一事件。
- 维护适配器内部游标、去重缓存、失败计数和调试证据。
- 执行 C++ 下发的 `prepare_reply_draft`、`send_message` 等受控命令。
- 将结果通过 IPC 返回 C++。

Python 不负责：

- 不直接渲染 UI。
- 不作为客服工作台。
- 不直接决定 AI 是否自动发送。
- 不绕过 C++ 统一模型写入业务消息。
- 不把平台私有字段扩散到主 UI。

## 6. C++ 职责

C++ 负责：

- 启动、停止和保活 Python sidecar。
- 接收 Python 事件并转换为 `PlatformMessage` 或 `Models::ConversationEvent`。
- 通过 `MessageRouter` / `ConversationManager` 做幂等、落盘和 UI 通知。
- 在聚合界面展示微信会话和消息。
- 创建发送任务、下发命令、接收发送结果。
- 记录审计和失败状态。

短期可以复用：

- `src/ipc/*`：现有 HTTP sidecar 能力可扩展 RPA 事件和命令接口。
- `src/core/messagerouter.*`：继续作为入站消息归一入口。
- `src/models/unifiedmodels.*`：承接统一模型。
- `src/ui/aggregatechatform.*`：作为唯一用户主界面。

逐步替换：

- `src/services/platforms/wechatrp_adapter.*`：由轮询 `rpa_inbox_messages` 改为接收 IPC 事件。
- `src/services/wechat/wechatworkbenchservice.*`：不再作为长期服务，能力收敛到通用 RPA/IPC 服务。
- `src/ui/wechatworkbenchdialog.*`：后续下线或仅保留临时调试入口。

## 7. IPC 协议草案

### 7.1 设计原则

- Python 到 C++ 使用事件推送，不再依赖 C++ 高频轮询数据库。
- 每个事件必须有 `event_id`，C++ 侧按幂等键去重。
- 每个命令必须有 `request_id` / `task_id`，方便审计和结果回填。
- 平台私有字段放入 `metadata`，不要污染统一模型字段。
- 事件先由 C++ 落统一消息库，再通知 UI。

### 7.2 Python -> C++ 事件

#### `conversation_observed`

用于发现或更新微信会话。

```json
{
  "event_id": "evt_20260528_000001",
  "event_type": "conversation_observed",
  "platform": "wechat",
  "account_id": "local_wechat",
  "conversation_key": "wechat:local_wechat:张三",
  "occurred_at": "2026-05-28T10:30:00+08:00",
  "payload": {
    "display_name": "张三",
    "unread_count": 2,
    "last_message_preview": "你好，在吗",
    "last_message_at": null,
    "source_type": "ui_observed",
    "confidence": 80,
    "verification_status": "unverified",
    "metadata": {
      "observation_method": "uia",
      "window_hwnd": "0x00123456"
    }
  }
}
```

#### `message_observed`

用于上报微信可见消息。

```json
{
  "event_id": "evt_20260528_000002",
  "event_type": "message_observed",
  "platform": "wechat",
  "account_id": "local_wechat",
  "conversation_key": "wechat:local_wechat:张三",
  "occurred_at": "2026-05-28T10:30:01+08:00",
  "payload": {
    "platform_msg_id": "wechat_local_张三_8f31c2",
    "direction": "inbound",
    "sender_role": "customer",
    "sender_name": "张三",
    "content_type": "text",
    "content": "你好，在吗",
    "platform_displayed_at": null,
    "source_type": "ui_observed",
    "confidence": 80,
    "verification_status": "unverified",
    "evidence_ref": "",
    "metadata": {
      "observation_method": "uia",
      "role_method": "bubble_variance",
      "raw_control_name_hash": "sha256:..."
    }
  }
}
```

#### `account_health_changed`

用于上报微信窗口、登录态、控件可用性和异常状态。

```json
{
  "event_id": "evt_20260528_000003",
  "event_type": "account_health_changed",
  "platform": "wechat",
  "account_id": "local_wechat",
  "occurred_at": "2026-05-28T10:30:02+08:00",
  "payload": {
    "status": "online",
    "healthy": true,
    "error_code": "",
    "message": "",
    "capabilities": {
      "text_message_read": true,
      "send_text": true,
      "fill_draft": true,
      "background_send": false,
      "requires_foreground_window": true,
      "requires_ocr": false
    },
    "metadata": {
      "main_window_found": true,
      "session_list_found": true,
      "message_list_found": true,
      "chat_input_found": true,
      "send_button_found": true
    }
  }
}
```

#### `draft_prepared`

用于回填草稿后的结果回调。

```json
{
  "event_id": "evt_20260528_000004",
  "event_type": "draft_prepared",
  "platform": "wechat",
  "account_id": "local_wechat",
  "conversation_key": "wechat:local_wechat:张三",
  "task_id": "task_20260528_000001",
  "occurred_at": "2026-05-28T10:30:03+08:00",
  "payload": {
    "status": "success",
    "error_message": "",
    "evidence_ref": "",
    "metadata": {
      "target_verified": true,
      "input_text_hash": "sha256:..."
    }
  }
}
```

#### `send_result_observed`

用于真实发送动作后的结果回调。MVP 阶段默认不主动启用无人值守自动发送。

```json
{
  "event_id": "evt_20260528_000005",
  "event_type": "send_result_observed",
  "platform": "wechat",
  "account_id": "local_wechat",
  "conversation_key": "wechat:local_wechat:张三",
  "task_id": "task_20260528_000002",
  "occurred_at": "2026-05-28T10:30:04+08:00",
  "payload": {
    "status": "sent",
    "platform_msg_id": "",
    "error_message": "",
    "verification_status": "auto_verified",
    "metadata": {
      "send_method": "uia_send_button",
      "target_verified": true
    }
  }
}
```

### 7.3 C++ -> Python 命令

#### `connect`

连接微信适配器。

```json
{
  "request_id": "req_20260528_000001",
  "command": "connect",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {
    "mode": "listen",
    "emit_initial_snapshot": true
  }
}
```

#### `disconnect`

停止微信适配器。

```json
{
  "request_id": "req_20260528_000002",
  "command": "disconnect",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {}
}
```

#### `health_check`

查询微信适配器状态。

```json
{
  "request_id": "req_20260528_000003",
  "command": "health_check",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {}
}
```

#### `fetch_visible_conversations`

低频补拉当前可见会话。

```json
{
  "request_id": "req_20260528_000004",
  "command": "fetch_visible_conversations",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {
    "limit": 80
  }
}
```

#### `fetch_visible_messages`

低频补拉目标会话当前可见消息。

```json
{
  "request_id": "req_20260528_000005",
  "command": "fetch_visible_messages",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {
    "conversation_key": "wechat:local_wechat:张三",
    "limit": 30
  }
}
```

#### `prepare_reply_draft`

将内容回填到微信目标会话输入框，等待人工确认。

```json
{
  "request_id": "req_20260528_000006",
  "task_id": "task_20260528_000001",
  "command": "prepare_reply_draft",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {
    "conversation_key": "wechat:local_wechat:张三",
    "display_name": "张三",
    "text": "您好，请问有什么可以帮您？",
    "require_target_verification": true
  }
}
```

#### `send_message`

真实发送。该命令必须由人工确认或明确授权流程触发。

```json
{
  "request_id": "req_20260528_000007",
  "task_id": "task_20260528_000002",
  "command": "send_message",
  "platform": "wechat",
  "account_id": "local_wechat",
  "parameters": {
    "conversation_key": "wechat:local_wechat:张三",
    "display_name": "张三",
    "text": "您好，请问有什么可以帮您？",
    "confirm_token": "manual_confirmed_by_agent",
    "require_target_verification": true
  }
}
```

## 8. 数据落盘策略

### 8.1 主链路

主链路应由 C++ 统一落盘：

1. Python 通过 IPC 发 `message_observed`。
2. C++ 将事件映射为 `PlatformMessage` 或 `Models::ConversationEvent`。
3. `MessageRouter` 做幂等校验、创建会话、创建消息。
4. `ConversationManager` 发出 UI 更新信号。
5. `AggregateChatForm` 展示最新消息。

### 8.2 补偿链路

数据库补偿可以保留，但不再作为实时总线：

- Python 可以写入轻量事件日志或 `rpa_inbox_messages` 兼容表，用于崩溃恢复。
- C++ 启动或重连时可按游标补拉未确认事件。
- 一旦 IPC 主链路稳定，`rpa_inbox_messages` 的轮询消费应降级或下线。

### 8.3 幂等键

优先使用：

```text
platform + account_id + conversation_key + platform_msg_id
```

微信无法提供稳定消息 ID 时，退化为：

```text
platform + account_id + conversation_key + direction + content_hash + time_bucket
```

建议把退化策略写入 `metadata`：

```json
{
  "idempotency_method": "content_hash_time_bucket",
  "time_bucket_seconds": 60
}
```

## 9. 微信工作台下线策略

短期：

- 保留入口用于调试或比对，不再新增业务能力。
- 新微信适配器优先服务聚合界面。
- 工作台相关 Python 服务不再作为新协议承载点。

中期：

- 将工作台内有价值能力迁移到通用适配器：
  - UIA 探测
  - 会话列表读取
  - 当前消息读取
  - 草稿回填
  - 发送验证
- 主界面隐藏或移除“微信 RPA 工作台”入口。

长期：

- 删除 `src/ui/wechatworkbenchdialog.*`。
- 删除 `src/services/wechat/wechatworkbenchservice.*`。
- 删除或归档 `python/rpa/tools/wechat_workbench_service.py`。
- 微信能力全部通过聚合界面和统一 IPC/RPA 服务访问。

## 10. 迁移阶段

### 阶段 0：协议和文档固化

目标：

- 固化本方案中的事件和命令结构。
- 明确数据库只做持久化和补偿，不做实时总线。
- 明确微信工作台未来下线。

验收：

- 事件字段可映射到 `Models::Conversation` / `Models::Message`。
- 发送命令具备 `task_id`、目标校验、人工确认边界。

### 阶段 1：新微信适配器骨架

目标：

- 建立 `adapter / detector / reader / sender / stability` 结构。
- 对齐 `D:/wechat` 的 UIA 优先路线。
- 实现 `health_check`、`fetch_visible_conversations`、`fetch_visible_messages`。

验收：

- 可找到微信主窗口。
- 可读到会话列表。
- 可读到当前聊天可见消息。
- 不依赖微信 OCR 校准文件完成主链路。

### 阶段 2：IPC 事件推送

目标：

- Python 将 `conversation_observed` / `message_observed` 推送到 C++。
- C++ 通过统一模型落盘并刷新聚合界面。

验收：

- 微信新消息能进入聚合接待界面。
- C++ 不需要轮询 `rpa_inbox_messages` 才能展示新消息。
- 重复消息不会重复落盘。

### 阶段 3：草稿回填和受控发送

目标：

- 聚合界面中生成或输入回复后，C++ 下发 `prepare_reply_draft`。
- Python 将草稿回填到微信输入框。
- 后续在明确人工确认后支持 `send_message`。

验收：

- 能回填到正确微信会话。
- 目标会话校验失败时不写入错误窗口。
- 命令结果能回传并更新聚合界面状态。

### 阶段 4：工作台收敛和入口下线

目标：

- 聚合界面覆盖微信工作台原有用户价值。
- 微信工作台入口隐藏或移除。
- 工作台服务代码不再作为主链路依赖。

验收：

- 客服无需打开微信工作台即可完成微信会话查看和回复。
- 微信适配器异常只影响微信平台状态，不影响聚合主界面和其他平台。

### 阶段 5：旧链路清理

目标：

- 下线微信 OCR Reader / Writer 主链路。
- 清理兼容包装和不再使用的校准入口。
- 保留必要调试工具和证据采集能力。

验收：

- `python/rpa/platforms/wechat/reader.py`、`writer.py` 中旧 OCR 轮询逻辑不再是生产入口。
- `src/services/platforms/wechatrp_adapter.*` 不再依赖高频数据库轮询。
- 文档、帮助文案和 UI 入口与新形态一致。

## 11. 风险和约束

### 11.1 微信 UIA 结构变化

风险：

- 微信版本升级可能改变 `session_list`、`chat_message_list`、`chat_input_field` 等控件。

控制：

- 控件查找必须有候选评分和降级路径。
- 保留 UIA tree dump 调试工具。
- 健康检查必须明确标出哪个控件不可用。

### 11.2 消息方向判断错误

风险：

- UIA 文本可读，但消息方向不一定总是稳定。

控制：

- 优先使用控件结构判断。
- 其次使用截图颜色方差。
- 不确定时标记 `direction=unknown` 或降低 `confidence`，不伪装成高可信消息。

### 11.3 误发风险

风险：

- 自动化切换会话或发送时可能作用到错误窗口。

控制：

- MVP 默认优先 `prepare_reply_draft`，不默认无人值守发送。
- `send_message` 必须带 `confirm_token`。
- 发送前校验窗口、目标会话、输入框内容。
- 失败时回退人工，不盲目重试高风险动作。

### 11.4 IPC 断开或 Python 崩溃

风险：

- Python sidecar 异常时消息推送中断。

控制：

- C++ 显示微信平台 degraded。
- Python 保留本地补偿日志或兼容入站表。
- 重连后按游标补拉可见会话和消息。

## 12. 推荐结论

微信 RPA 应从“独立 OCR/RPA 工作台”重构为“UIA 优先的平台适配器”。`D:/wechat` 的实现方式可以作为新微信适配器的自动化内核参考，但最终能力应接入当前项目的统一 IPC、统一模型、聚合接待界面和审计链路。

这条路线的核心收益是：

- 消息进入聚合主界面，而不是散落在独立微信工作台。
- Python 与 C++ 通过事件和命令通信，而不是靠数据库轮询实时传递消息。
- 自动化能力被限制在平台适配层，UI 和核心模型保持稳定。
- 微信工作台可逐步下线，系统产品形态回到“一个聚合工作台”。
