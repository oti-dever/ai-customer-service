# C++客户端与Python服务端解耦重构规划

## 1. 背景

目标架构已经在 [C++客户端与Python服务端解耦架构方案.md](./C++客户端与Python服务端解耦架构方案.md) 中定义：`C++` 是客户端，`Python` 是服务端，数据库是唯一真相源，双方通过本机或局域网 WebSocket + JSON 通信。

当前架构仍然是：

- `C++` 启动桌面程序时拉起 Python sidecar。
- `C++` 负责 Python sidecar 生命周期，退出时尝试停止 sidecar。
- `Python` 更像附着在客户端上的自动化进程，运行边界依赖客户端。
- 通信路径里同时存在命令、事件、补偿拉取和进程生命周期控制，排查问题时容易混在一起。

这次重构的目标不是一次性大改，而是把“生命周期”“通信协议”“数据真相源”“实时事件”逐步拆开，保证每一步都可验证、可回滚。

## 2. 重构目标

- `Python` 可以独立启动、独立停止、独立调试。
- `C++` 不再默认负责 Python 服务端生命周期，只负责连接指定服务端地址。
- `Python` 采集到的会话和消息先写数据库，再按连接状态推送事件。
- `C++` 从数据库恢复会话和消息缓存，实时 WebSocket 事件只负责增量刷新。
- 命令和事件统一走双向 WebSocket JSON 协议。
- 正式模式下 Python 服务端只读微信，不允许发送消息；调试模式才允许发送。
- 保留低频补偿同步，作为防漏兜底，不再作为主路径。

## 3. 重构原则

1. 先拆边界，再替换实现。
   - 先把协议、状态、数据库同步边界定清楚，再逐步迁移具体代码。

2. 先兼容，再切换默认路径。
   - 迁移早期可以保留 sidecar 启动方式作为兼容入口，但新路径要以“连接独立 Python 服务端”为准。

3. 数据库优先。
   - Python 写库是事实来源；C++ 内存里的会话和消息只是 UI 缓存。

4. 所有跨进程行为必须可追踪。
   - WebSocket 连接、命令、响应、事件、数据库写入、补偿同步都要有 `request_id` / `event_id` / `cursor` 日志。

5. 每个阶段都要能单独验收。
   - 不把“服务端化”“协议改造”“UI 连接配置”“发送链路改造”混成一次提交。

## 4. 分阶段方案

### 阶段 0：冻结目标协议和数据边界

阶段 0 已完成，当前先把“以后怎么通信、谁写什么、谁负责恢复”冻结下来，不改主业务链路。

#### 4.1 协议外壳

统一外壳固定为 `event` / `command` / `response`，不再继续发散成多种临时结构。

```json
{
  "type": "event",
  "name": "message_observed",
  "id": "evt_xxx",
  "seq": 12,
  "cursor": "12",
  "payload": {}
}
```

字段口径：

- `type`：包类型，只能是 `event`、`command`、`response`。
- `id`：包级唯一标识。
- `name`：业务名，例如 `message_observed`、`send_message`、`connect`。
- `seq`：服务端递增序号，用于事件顺序和补偿恢复。
- `cursor`：客户端恢复游标。
- `request_id`：命令和响应的关联 ID。
- `payload`：业务内容。
- `status`：响应状态，只用于 `response`。
- `error`：失败原因，只用于 `response`。
- `result`：返回结果，只用于 `response`。

#### 4.2 标识字段边界

这一步的重点是把“谁用什么标识”固定住，避免 C++ 和 Python 各自发明自己的恢复规则。

- `request_id`：只用于命令往返，不用于事件去重。
- `event_id`：只用于事件去重和日志追踪。
- `seq`：服务端递增序号，作为事件主游标。
- `cursor`：客户端恢复位置，优先跟随 `seq`。
- `conversation_key`：跨进程会话主键，必须稳定且可回放。
- `display_name`：展示名，只能作为辅助字段，不应单独承担主键职责。

#### 4.3 数据边界

- Python 写数据库是事实来源。
- C++ 只把数据库当恢复源和缓存源。
- C++ 内存里的会话和消息只负责 UI 展示，不是长期真相。
- Python 负责采集、写库、发事件、执行命令。
- C++ 负责连接、展示、恢复、发起命令。

#### 4.4 阶段 0 验收口径

- 事件和命令的字段名已经统一。
- 事件去重和恢复游标已经统一。
- 会话主键和展示名的职责已经分开。
- 数据库主写方已经明确为 Python。
- C++ 侧只保留缓存语义，不再作为真相源。

#### 4.5 当前不做的事

- 不在阶段 0 里改主业务流程。
- 不在阶段 0 里把旧 sidecar 立刻拆掉。
- 不在阶段 0 里把全部 HTTP 改成 WebSocket。
- 不在阶段 0 里改发送链路和未读观察链路的具体实现。

阶段 0 的作用是先把后续迁移的判定标准固定下来，后面的每一步都按这个基线往前推。

### 阶段 1：Python 服务端独立运行

阶段 1 已完成一半：服务端入口已经从“纯 sidecar 语义”收口成“可独立启动的 Python 服务端”，并且模式边界开始明确。

#### 4.1 当前完成内容

- `python/service/server.py` 支持 `--mode debug|formal`。
- `python/service/rpa_bridge.py` 已经能按模式拒绝发送类命令。
- `python/service/README.md` 已改成独立服务端说明。
- `python/service/run_service.bat` 默认按正式模式启动。
- 服务端启动日志能明确区分 `managed` 和 `standalone`。
- `rpa_bridge` 不再在 import 时自动实例化全局 bridge，避免旧副作用。

#### 4.2 这一步的目标

- `Python` 能独立启动，不依赖 `C++` 进程存在。
- `Python` 继续保留采集、健康检查、事件存储和数据库写入能力。
- 正式模式和调试模式的权限边界先固定下来。
- 当前还不重构通信主通道，不把阶段 2 的 WebSocket 连接也提前做完。

#### 4.3 当前完成的验收口径

- 不启动 `C++`，`Python` 仍能启动服务、采集、写库、产生日志。
- `formal` 模式下 `send_message` 和 `prepare_reply_draft` 被拒绝。
- `debug` 模式下命令入口仍可用于开发验证。
- 服务端可以单独说明自己是 `standalone` 还是被父进程管理。
- `send_disabled_in_formal_mode` 已成为正式模式下的明确拒绝信号。

#### 4.4 阶段 1 仍待做的部分

- 把独立运行时的命令和事件边界再收紧一点。
- 之后再进入阶段 2，把 C++ 从“拉起服务端”逐步改成“连接服务端”。

### 阶段 2：C++ 增加“连接服务端”能力

阶段 2 已开始推进，当前先完成“客户端可配置并连接外部 Python 服务端”，不一次性替换全部通信。

#### 4.1 当前完成内容

- `IpcService` 增加服务端连接配置持久化：
  - `rpa/serviceEndpoint`
  - `rpa/manageServiceLifecycle`
- `IpcService` 支持两种模式：
  - `manageServiceLifecycle=true`：沿用当前兼容路径，由 C++ 管理 Python sidecar。
  - `manageServiceLifecycle=false`：不再启动/停止 sidecar，只连接配置的 Python 服务端。
- RPA 管理窗口增加服务端配置行：
  - 服务端地址输入框。
  - “C++ 管理 Python sidecar”开关。
  - 保存服务端。
  - 测试连接。
- `restartManagedService()` 和 `startManagedService()` 已按配置走外部服务端连接分支。

#### 4.2 当前验收结果

- C++ 已经可以保存 Python 服务端地址。
- C++ 已经可以在不管理 sidecar 的模式下测试连接外部服务端。
- 默认仍保留旧 sidecar 兼容路径，避免一次性打断现有链路。
- 已通过 Debug 构建。

#### 4.3 阶段 2 未完成内容

- 还没有把主通信通道改成 C++ WebSocket 客户端。
- 还没有实现服务端断线后的心跳和自动重连状态机。
- 还没有在聚合界面展示完整连接状态。
- 还没有连接成功后触发数据库全量缓存重建；这属于阶段 3。

当前阶段 2 的结论是：C++ 已经具备“连接独立 Python 服务端”的入口和配置能力，但实时主通道仍保留当前兼容实现。

### 阶段 3：客户端缓存改为从数据库重建

把 C++ 的会话列表和消息列表恢复逻辑改成以数据库为准。

阶段 3 当前已先完成轻量改造：

- `ConversationManager` 增加显式 `reloadFromDatabase()`，用于丢弃“内存状态就是事实”的假设，重新以 DAO 查询结果刷新会话。
- 聚合界面启动时仍按原有路径从数据库读取会话列表和最后选中会话。
- `IpcService` 报告服务端连接可用后，聚合界面会重新读取数据库中的会话列表和当前会话消息。
- 当前选中的会话如果还存在，会保留选中并重载消息；如果数据库里已经不存在，则清空当前选中。

当前先采用“全量重建当前缓存”的策略：

- C++ 启动或连接成功后，从数据库读取会话列表。
- 读取每个会话的最近消息。
- 重建 UI 缓存和当前选中状态。
- 再订阅 WebSocket 增量事件。

后续数据量变大后，再考虑按游标增量恢复。

当前仍保留的边界：

- 这一步只做客户端缓存重建，不改变 Python 写库方式。
- 这一步仍使用现有 HTTP/事件兼容链路，不提前切 WebSocket 主通道。
- 当前是全量刷新 UI 缓存，不做按游标增量恢复；游标恢复留到实时事件和补偿同步阶段一起收敛。

验收标准：

- C++ 关闭后重新打开，能从数据库恢复聚合界面。
- Python 在 C++ 关闭期间采集到的新消息，C++ 重连后能同步出来。
- 重启客户端不会重复显示历史消息。

### 阶段 4：实时消息主路径切到 WebSocket 事件

把“Python 主动观察 -> 写库 -> 推送事件 -> C++ 刷新 UI”作为主路径。

阶段 4 当前已开始推进：

- Python 侧已经把 `conversation_observed`、`message_observed`、`message_sent`、`send_failed`、`account_health_changed` 推到事件通道。
- C++ 侧 `WechatRPAAdapter` 已接收 WebSocket 事件，并把 `conversation_observed` 走进会话更新链，把 `message_observed` 走进消息更新链。
- 原来的 inbox 轮询还保留，但已经降成低频补偿，不再作为实时主路径。
- C++ 侧会感知事件桥接连接状态：事件桥接在线时优先等推送，断线或低频兜底时才补拉 `/api/rpa/events` 和 inbox。
- C++ 侧已经能记录事件类型、游标和补偿扫描日志，便于区分事件驱动与补偿驱动。

事件建议包括：

- `conversation_observed`
- `message_observed`
- `message_sent`
- `send_failed`
- `account_health_changed`

低频补偿同步保留，但只做防漏：

- 定时按游标或数据库更新时间补拉。
- 补拉结果必须走同一套去重逻辑。
- 补偿同步不能触发高频 UI 抖动。

验收标准：

- 微信收到新消息后，Python 主动推送，C++ 不依赖高频轮询也能显示。
- 断线重连后不会漏消息。
- 补偿同步不会造成重复气泡。

### 阶段 5：发送链路切到 WebSocket 命令

聚合界面发送消息时，C++ 通过 WebSocket 发 `send_message` 命令。

阶段 5 当前已进入最小闭环，后续先补验证和测试收口：

- Python 服务端新增 RPA 命令 WebSocket，默认监听 `ws://127.0.0.1:8767`。
- C++ 微信发送链路优先通过命令 WebSocket 发送 `send_message`。
- 如果命令 WebSocket 尚未可用，C++ 暂时回退到旧 HTTP `/api/rpa/command`，避免一次切换打断现有发送能力。
- 其它 RPA 命令暂时仍走 HTTP，后续再逐步迁移。
- C++ 命令请求和 Python 结果都携带 `client_message_id`，用于把本地 pending 消息和服务端回包对齐。
- `MessageRouter` 已优先按 `client_message_id` 回填发送状态，文本匹配只作为兜底。

命令中必须带：

- `platform`
- `conversation_key`
- `display_name`
- `client_message_id`
- `text`
- `request_id`

Python 侧处理顺序：

1. 校验当前运行模式。
2. 校验目标会话。
3. 必要时扫描会话列表并切换到目标会话。
4. `verify_session_switch(target)` 成功后才写输入框。
5. 发送后写数据库。
6. 返回 `response`。
7. 推送 `message_sent` 或 `send_failed` 事件。

验收标准：

- 正式模式下发送命令被明确拒绝。
- 调试模式下可以发送到指定会话。
- 目标会话验证失败时，不写输入框，不误发。
- C++ 能用 `request_id` 把命令往返对齐，能用 `client_message_id` 把本地 pending 消息和 Python 发送结果对齐。

### 阶段 6：移除 C++ 对 sidecar 生命周期的强依赖

当独立服务端路径稳定后，再收敛旧逻辑。

建议处理方式：

- C++ 默认不再启动 Python 服务端。
- 旧 sidecar 启动入口只作为开发辅助或兼容开关，不能再作为正式默认路径。
- 程序退出时只断开 WebSocket，不负责杀 Python 服务端。
- Python 服务端由用户、脚本、服务管理器或部署工具启动。

验收标准：

- C++ 退出不会影响 Python 服务端运行。
- Python 服务端退出不会导致 C++ 异常退出。
- 进程生命周期问题和通信问题在日志中能分开定位。
- `manageServiceLifecycle=false` 时，C++ 只做连接，不做拉起、停止、重启。

## 5. 关键模块改造方向

### 5.1 C++ 侧

- `IpcService`：从“管理 Python 进程 + HTTP/WS 混合通信”逐步收敛为“WebSocket 客户端连接管理”。
- `RpaProcessManager`：降级为开发辅助或兼容模块，不再是正式路径核心。
- `WechatRPAAdapter`：只面向平台命令和事件协议，不关心 Python 进程如何启动。
- `MessageRouter`：继续负责 UI 消息发送状态流转，但状态来源改为 WebSocket response/event。
- DAO 层：提供客户端启动时的会话和消息恢复能力。
- UI 层：增加服务端连接状态展示和连接配置入口。

### 5.2 Python 侧

- `service/server.py`：成为独立服务端入口，负责 WebSocket、健康检查、模式控制。
- `service/rpa_bridge.py`：负责命令分发和事件发布，不直接绑死某个平台。
- `rpa/platforms/wechat`：继续封装微信自动化细节。
- DB 写入层：统一处理 conversation/message/event cursor 写入。
- 日志层：独立记录服务启动、连接、命令、事件、微信自动化、数据库写入。

## 6. 兼容和回滚策略

迁移期间建议保留两套入口：

- 新入口：C++ 连接独立 Python 服务端。
- 旧入口：C++ 启动 Python sidecar。

但功能开发优先走新入口。旧入口只用于：

- 对比问题。
- 临时回滚。
- 验证某个问题是否来自新通信链路。

每个阶段上线前都要保证：

- 旧路径还能启动。
- 新路径能单独验证。
- 日志能明确看出当前使用的是 `sidecar_mode` 还是 `server_mode`。

## 7. 风险点

- 数据库主写方切换不清晰，可能造成 C++ 和 Python 重复写同一条消息。
- WebSocket response 和 event 同时到达时，C++ 发送状态可能重复流转。
- 会话主键不稳定时，重启后可能把同一个微信会话识别成多个会话。
- Python 独立运行后，服务端配置、日志目录、数据库路径必须统一，否则排查会变难。
- 正式模式禁发必须落在 Python 命令入口，不能只靠 C++ UI 隐藏按钮。

## 8. 建议验收路径

1. 先用 Python 单独运行验证“无客户端采集写库”。
2. 再用 C++ 只连接服务端，验证连接状态和数据库恢复。
3. 再验证 Python 推送新消息，C++ 聚合界面实时更新。
4. 再验证 C++ 关闭期间 Python 继续采集，C++ 重启后能恢复。
5. 最后验证调试模式发送消息和正式模式拒绝发送。

## 9. 最终收敛状态

重构完成后，系统应该变成：

- `Python 服务端`：长期运行，负责平台自动化、采集、写库、事件推送、命令执行。
- `C++ 客户端`：按需启动，连接服务端，展示数据库历史和实时事件，发送用户命令。
- `数据库`：唯一真相源。
- `WebSocket`：实时双向通信通道。
- `低频补偿同步`：防漏机制，不是主链路。

这样后续接入其他平台时，只需要在 Python 服务端新增平台适配器，并复用同一套数据库、WebSocket 协议和 C++ 聚合界面。
