# 聚合会话「AI 自动回复」方案设计与实现

本文档描述在**聚合接待界面**中，在选定模式下由 **AI 自动生成回复并经由既有 RPA 出站链路发送给客户** 的能力的设计定案与实现要点。与 **「AI 辅助」**（仅起草、不自动发送，见《AI辅助回复方案设计与实现.md》）在 **产品责任、触发方式、风控** 上明确区分；**首版范围仅讨论千牛平台**，其他平台可后续按同一模式扩展。

**文件名**：与标题一致，`AI自动回复方案设计与实现.md`。

**关联文档**：

| 文档 | 关系 |
|------|------|
| 《AI辅助回复方案设计与实现.md》 | **起草模式**：手动点「生成本条回复」、写入输入框；**网络层、模型预设、多模态入参** 与本文 **生成阶段** 宜 **复用或抽公共层**，避免两套协议。 |
| 《千牛RPA优化方案设计与实现.md》 | 入站 **文本 / 截图路径**、Reader 行为；自动回复的 **待响应对象** 与辅助模式共用 **消息模型**（如 `latestInboundSnapshot`）。 |
| 《AI助手集成方案设计与实现.md》 | `OpenAiCompatClient`、豆包多模态；**内置助手不参与客户会话**。 |
| 《RPA-DB-协议.md》 / 各平台消息链路 | `messages` 表、`sendMessage` / Writer 出站约定。 |

---

## 1. 背景与目标

### 1.1 背景

- **AI 辅助** 降低「打字成本」，但 **发送仍由人确认**，适合强合规、高客单价或需人工把关的场景。
- **AI 自动回复** 面向希望 **减少人工盯屏、对延迟敏感** 的接待形态：在 **明确规则与风险提示** 下，由系统在满足条件时 **自动生成并发送**。
- 千牛侧 RPA 与聚合、SQLite 链路已在工程内贯通；**首版仅对千牛（`platform = qianniu`）** 开放自动回复逻辑，避免未接入或未验证的平台误发。

### 1.2 第一版目标（千牛）

1. 聚合 **模式下拉框** 选择 **「AI自动回复」**（实现上对应索引与 **《AI辅助回复方案设计与实现》§5.4** 第三项一致；文案以界面为准）。
2. **首次从其他模式切入「AI自动回复」** 时 **弹窗确认**（已实现）：说明将接管千牛会话的自动回复及 **误发、合规、费用** 风险；**取消则保持原模式**。
3. 在 **自动回复模式** 下，对 **当前选中且平台为千牛** 的会话，在满足 **§3** 条件时 **自动生成回复并调用既有发送接口**，无需客服点击「发送」。
4. **非目标（首版可不做或简化）**：全店铺无人值守策略引擎、复杂工单分流、跨会话批量机器人——首版以 **单会话、可解释规则** 为主。

**实现进度（概要）**：§3 判定、`MessageDao::lastMessageForConversation`、与 **AI 辅助** 共用的请求组装、独立 **`OpenAiCompatClient` 自动回复实例**、**T1/T2** 挂钩、`sendMessage` 出站、**§4.1** 与 **`abortAggregateAiRequest`** 联动 **已在工程内落地**；**冷却、失败重试、发送前二次校验、独立 `AggregateAutoReplyController` 进程级抽象** 等仍属后续（见 **§10**）。

### 1.3 与「AI 辅助」的差异（必读）

| 维度 | AI 辅助 | AI 自动回复（本文） |
|------|---------|---------------------|
| 触发 | 用户点击「生成本条回复」 | **切换会话**、**当前会话新入站** 等（见 §4） |
| 输出落点 | 聚合 **输入框** | **直接出站**（经 `MessageRouter::sendMessage` 等） |
| 责任 | 人可改字再发 | **系统自动发**；需 **日志、开关、失败策略**（见 §8） |
| 模式互斥 | 与人工接待、自动回复 **三选一**（同一下拉框） | 同左 |

---

## 2. 范围与非目标

### 2.1 范围内（首版 · 千牛）

| 项 | 说明 |
|----|------|
| 平台 | **仅 `ConversationInfo.platform == "qianniu"`**（字符串以工程约定为准）。 |
| 会话选择 | 以 **聚合当前选中会话** 为主触发对象；是否扩展「列表内非选中会话排队」见 **§7 演进**。 |
| 消息判定 | **仅依据「最后一条消息」**（见 §3），**不**在首版实现「向前查找上一条客户消息」 unless 产品变更。 |
| 生成内容 | 与 **AI 辅助** 共用 **同一条客户入站上下文**（`latestInboundSnapshot` 或等价）及 **system 知识块**；**模型线路**（豆包/DeepSeek 等）可与辅助 **共用 `ai/presets/*` 与配置键**，或单独 **`aggregateAutoReply/*`**（实现期定）。 |
| 发送 | 复用 **`MessageRouter::sendMessage(conversationId, text)`**（或当前聚合发送统一入口），保证与 **手动发送** 同一 RPA/回执链路。 |

### 2.2 非目标（首版不做或延后）

- **非千牛** 平台自动发送（微信、拼多多等）：默认 **忽略** 或仅打日志。
- **全自动客服大脑**（意图识别、订单改价、退款）：不承诺；首版为 **单轮/单条上下文回复**。
- **法律/合规承诺**：文档与弹窗仅作 **风险提示**；企业合规责任由使用方自负。
- **与「AI 辅助」同时生效**：模式下拉 **互斥**，不会并行。

---

## 3. 核心业务规则：何时尝试自动回复

以下规则 **首版写死**，后续若需「最后一条为 out 仍补回客户」需 **新字段或消息状态机**（单独立项）。

### 3.1 查询对象

- 对给定 `conversation_id`，取 **`messages` 表中该会话 `id` 最大的一条记录**（最后一条消息）。

**工程实现**：**`MessageDao::lastMessageForConversation(int conversationId) const`** → **`std::optional<MessageRecord>`**，SQL 为 **`ORDER BY id DESC LIMIT 1`**，字段映射与 **`listByConversation`** 一致。

### 3.2 跳过（不生成、不发送）

满足 **任一** 即 **跳过** 本会话本次检测：

1. **平台不是千牛**。
2. **最后一条消息的 `direction ≠ 'in'`**（即为 `out` 或其他）：表示 **最近一条已是己方发出**，首版 **不再向上查找** 更早的 `in`。
3. **会话无有效待回复载荷**：与辅助模式一致——若仅有截图路径但 **文件不存在**，或 **文本与图皆不可用**，则不调用模型（或仅告警，实现期定）。
4. **`aggregateAutoReply/emergencyUserStop == true`**（用户通过 **§4.1「停止自动回复」** 或后续等价入口触发）。
5. **其他全局开关关闭**（若另行实现 `aggregateAutoReply/enabled` 等）。
6. **冷却/防抖**（见 §6）：距离上一次 **自动发送成功** 不足最短间隔、或同一会话 **生成失败重试次数** 超限。

### 3.3 进入生成

当 **最后一条为 `direction = 'in'`** 且 **有效载荷满足**（文本和/或可读的 `content_image_path`），且 **§3.2** 未命中跳过条件，则 **允许进入生成 → 发送** 流水线。

### 3.4 与千牛截图入站（多模态）的关系

- 若入站为 **截图 + 占位文本**，生成侧与 **《AI辅助回复方案设计与实现》§3.4、§5.6** 一致：**须走支持视觉的线路**（如豆包）；**纯文本模型** 不得自动发送（应跳过并 **记日志/状态栏**）。
- **配置错误**（无 Key、非方舟看图）时：**不发送**，避免空转或乱答。

---

## 4. 触发时机

| # | 场景 | 行为概要 |
|---|------|----------|
| T1 | 用户 **切换到某会话**（列表点击、键盘导航等导致 `m_currentConvId` 变化） | 若当前模式为 **AI自动回复** 且该会话为 **千牛**，对 **该会话** 执行一次 **§5 检测流水线**。 |
| T2 | **当前已停留在某千牛会话**，收到 **新入站消息**（`newMessageReceived`，且 `conversationId == m_currentConvId`） | 若模式为 **AI自动回复**，对该会话再执行 **§5**（通常新入站后最后一条变为 `in`，满足 §3.3）。 |

**说明**：

- **T1** 覆盖「点开一个已有未回会话」的场景。
- **T2** 覆盖「正在看该会话时客户又发一条」的场景。
- **不在首版要求**：后台轮询 **所有** 千牛会话的未读队列（避免与 Writer 锁、多会话切换冲突）；若产品需要，在 **§7** 扩展 **任务队列**。

### 4.1 应急「一键关」（总开关 · 先只做关）

原聚合左侧 **「模拟消息」** 按钮已 **不再用于模拟入站**（测试可改用内置模拟适配器等其他入口），按钮位改为 **「停止自动回复」**，作为 **紧急停止** 与 **总开关语义** 的入口（**首版仅实现「关」**，**「一键开」** 后续再议，避免与 **模式下拉 + 确认窗** 两套入口冲突）。

| 动作 | 行为 |
|------|------|
| 用户点击 **「停止自动回复」** | ① 将 **`QSettings` 键 `aggregateAutoReply/emergencyUserStop`** 置为 **`true`**（供后续 **`AggregateAutoReplyController`** 或运行时轮询 **硬性禁止** 自动发送，即使其它状态异常）。② **模式下拉框无条件设为「人工接待」**（索引 `0`），并更新内部记录的 **上一稳定模式**（避免确认弹窗逻辑错乱）。③ **中止** 当前聚合内 **AI 辅助** 正在进行的流式生成（`abortAggregateAiRequest`，避免草稿继续写入）。④ **状态栏短提示**（如「已停止自动回复并切换为人工接待」）。 |
| 用户希望 **再次** 使用 AI 自动回复 | 在模式下拉中 **重新选择「AI自动回复」**，走 **与首次切入相同的确认告警**；用户点 **「是」** 后，将 **`aggregateAutoReply/emergencyUserStop`** 置回 **`false`**，表示用户已再次知悉风险。点「否」则保持原模式不变。 |
| 与 **仅手动改模式下拉** 的区别 | 用户 **不点**「停止自动回复」、而是自己选「人工接待」时，**不强制** 写 `emergencyUserStop`（仍为人工接待，自动逻辑本就不触发）。**一键关** 强调 **显式熔断 + 持久标志位**，便于日志审计与后续控制器统一判断。 |

**实现状态**：聚合界面 **按钮文案、点击逻辑、`QSettings`、模式下拉回退、进入 AI 自动回复确认后清除标志** 已与工程对齐；**自动发信流水线** 已在 `AggregateChatForm::tryAggregateAutoReply` 中读取 **`aggregateAutoReply/emergencyUserStop`** 并执行生成与 **`ConversationManager::sendMessage`**（详见 **§10**）。独立命名的 **`AggregateAutoReplyController` 类** 尚未抽取，逻辑暂挂在 **`AggregateChatForm`**。

---

## 5. 流水线（建议实现顺序）

### 5.1 总览

```
触发(T1/T2) → 前置检查(模式/平台) → 读最后一条消息(§3) →
  若跳过 → 结束
  否则 → 组装请求(复用辅助的 snapshot + system，见 §5.2) →
  调用模型(流式或非流式) → 得到最终文本 →
  sendMessage(convId, text) → 处理失败/重试(§6)
```

### 5.2 生成层复用

- **强烈建议** 从现有 **`AggregateChatForm::onGenerateAiDraftClicked`** 抽取 **无 UI 依赖** 的函数，例如：  
  `bool AggregateAiClient::generateDraftText(int conversationId, QString* outText, QString* error)`  
  内部复用 **`readAiSettingsForAggregatePreset`**、`latestInboundSnapshot`、多模态 `imageFileToDataUrl`、`OpenAiCompatClient` 请求与 **流式拼接**。
- **差异**：自动模式 **不把内容写入 `m_inputEdit`**，仅在内存中拼出 **完整字符串** 再发送；可配置 **非流式** 以降低实现复杂度（需评估首字延迟）。

**工程现状**：已在 `aggregatechatform.cpp` 匿名命名空间中实现 **`buildAggregateAiCompletionRequest(...)`**，由 **`onGenerateAiDraftClicked`**（弹窗错误）与 **`tryAggregateAutoReply`**（静默失败并 **`qInfo`/`qDebug`**）共用；自动路径使用 **独立** 的 **`OpenAiCompatClient* m_autoReplyClient`**（与 **`m_aggregateAiClient`** 共用 **`QNetworkAccessManager`**），**流式** 拼接后发送；根参数与辅助一致含 **`max_tokens: 512`**。

### 5.3 发送层

- 调用 **`MessageRouter::sendMessage(conversationId, text)`**（与手动发送一致），确保 **SQLite 出站记录、RPA Writer、回执** 与现网一致。
- **发送前** 可选 **二次校验**：最后一条仍为 `in` 且 **未被人工抢先发送**（对比 `platform_msg_id` 或时间戳）——防竞态，实现期可选。

**工程现状**：自动回复在流式 **`completed`** 后对 **`m_autoReplyTargetConvId`** 调用 **`ConversationManager::instance().sendMessage(cid, text)`**，语义上与聚合 **`onSendClicked`** 一致（最终仍经 **`MessageRouter`**）。

### 5.4 与 UI 的耦合

- 自动回复 **不宜** 强依赖 `AggregateChatForm` 控件状态；建议 **`AggregateAutoReplyController`**（或等价）挂在 **`ConversationManager` / `MainWindow`** 层，订阅 **会话切换信号** 与 **新消息信号**，并查询 **`ConversationDao::findById`** 得到 `platform`。
- 模式标志：读取 **`QComboBox` 当前索引** 或与 **`QSettings`** 同步的枚举，避免仅依赖 UI 未创建场景。

**工程现状（首版折中）**：触发与判定写在 **`AggregateChatForm`** 内——**T1** 在 **`showConversation(int)`** 末尾调用 **`tryAggregateAutoReply(conversationId, "T1")`**；**T2** 在 **`onNewMessage`** 中当 **`conversationId == m_currentConvId`** 且 **`msg.direction == "in"`** 时调用 **`tryAggregateAutoReply(..., "T2")`**。模式通过 **`m_modeCombo` 当前索引** 是否为 **「AI 自动回复」**（与 **`kAggregateModeIndexAutoReply`** 一致）判断。后续若需单元测试或与 UI 解耦，可将 **`tryAggregateAutoReply`** 抽成独立 **`AggregateAutoReplyController`** 并迁入 **`ConversationManager`** 信号订阅。

---

## 6. 并发、重试与体验

| 主题 | 建议 |
|------|------|
| **同会话重入** | 同一 `conversationId` **仅允许一个** 生成/发送任务；新触发若上一任务未完成，**丢弃或排队**（首版可 **丢弃并打日志**）。 |
| **生成失败** | **不**自动无限重试；**指数退避** 或 **最多 N 次**，超过则 **状态栏/日志** 提示，需人工切模式或重试。 |
| **发送失败** | 沿用现有 **`messageSendFailed`** 与发件时间线；**不**自动连环重发同文案（除非产品明确要求）。 |
| **冷却时间** | 同一会话两次 **自动发送成功** 之间 **≥ T 秒**（配置项，如 3～10s），防止客户连发刷屏导致 API 爆量。 |
| **与 AI 辅助生成冲突** | 若用户在同一会话手动点「生成本条回复」，**abort** 自动任务或 **自动任务让路**（实现期定一种策略）。 |

---

## 7. 配置、开关与审计

| 配置项（示例） | 说明 |
|----------------|------|
| 模式 + 首次确认 | 下拉选 **AI自动回复** + 弹窗 **Yes**；确认后将 **`aggregateAutoReply/emergencyUserStop`** 置 **`false`**（与 **§4.1** 联动）。 |
| **`aggregateAutoReply/emergencyUserStop`**（bool） | **`true`**：用户已按 **§4.1** 一键停止，或等价熔断；后续自动发送实现 **必须** 尊重该位（与当前模式下拉 **与关系**）。**`false`**：未熔断，或用户已再次确认进入 AI 自动回复。 |
| **「停止自动回复」按钮** | 见 **§4.1**；置 **`emergencyUserStop=true`** 并 **切人工接待**。 |
| **「一键开」** | **未做**；重新启用 AI 自动回复 **仅通过** 模式下拉 **+ 确认窗**（见 **§4.1**）。 |
| `aggregateAutoReply/minIntervalMs`（可选） | 同会话最小发送间隔。 |
| 模型线路 | 与辅助 **共用预设** 或 **独立 `aggregateAutoReply/modelPresetKey`**，避免改辅助影响自动（产品定）。 |

**审计**：至少 **日志** 记录 **会话 id、平台、触发原因 T1/T2、是否跳过及原因、模型请求耗时、发送结果**；后续若需 **合规导出**，再落库辅助表。

---

## 8. 风险与合规提示（产品文案方向）

- **误发**：模型错误、上下文截断、图片理解偏差均可能导致 **不当回复**；首版应 **显著提示** 并在文档中明确 **人工可随时切回人工接待**。
- **费用**：每次自动回复均可能产生 **API 调用费用**；建议在设置或首次确认中说明。
- **平台规则**：千牛侧 **禁止骚扰、违规营销** 等以平台最新规则为准；本软件 **不** 替客户做合规裁决。

---

## 9. 实现阶段建议

| 阶段 | 内容 |
|------|------|
| **P0** | `MessageDao`（或 DAO）提供 **`lastMessageForConversation(int)`**；`AggregateAutoReplyController` 骨架 + 模式/平台/§3 判断 + **日志**。 |
| **P1** | 接入 **生成复用** + **`sendMessage`**；打通 **T1 切换会话** 与 **T2 新消息**。 |
| **P2** | 冷却、失败重试上限；与辅助 **互斥/abort**（**初版已部分实现**，见 **§10**）；**§4.1** 应急一键关（**UI + `QSettings` + `tryAggregateAutoReply` 读 `emergencyUserStop`** 已贯通）。 |
| **P3** | 非千牛扩展、多会话队列、更细 **消息级「已回复」状态**（若 §3 规则升级）。 |

**与当前代码对齐**：**P0/P1 主体已完成**（见 **§10**）。**P2** 中：**§4.1** 与 **`emergencyUserStop`** 已被 **`tryAggregateAutoReply`** 消费；**与 AI 辅助互斥** 已实现为 **`m_aggregateAiGenerating || m_autoReplyBusy` 时跳过**；**`abortAggregateAiRequest`** 会 **中止双客户端**（辅助 + 自动）并清除自动回复占用。**冷却、重试策略、`aggregateAutoReply/enabled` 全局开关、发送前二次校验** 仍属待办。

---

## 10. 工程实现对照（当前代码）

以下与仓库实现 **逐项对应**，便于评审与后续重构；文件名以 `src/` 为根。

### 10.1 数据层

| 设计点 | 实现 |
|--------|------|
| §3.1 最后一条消息 | **`MessageDao::lastMessageForConversation`**（`messagedao.h` / `messagedao.cpp`），`ORDER BY id DESC LIMIT 1`。 |
| 入站上下文（与辅助一致） | **`buildAggregateAiCompletionRequest`** 内使用 **`MessageDao::latestInboundSnapshot`**，多模态与 **`aggregateConfigLikelySupportsVision`** 判定同 **AI 辅助**。 |

### 10.2 聚合 UI 与触发（`aggregatechatform.cpp` / `.h`）

| 设计点 | 实现 |
|--------|------|
| §4 T1 | **`showConversation(int conversationId)`** 在刷新界面后调用 **`tryAggregateAutoReply(conversationId, "T1")`**（切换会话会先 **`abortAggregateAiRequest`**，避免旧会话请求干扰）。 |
| §4 T2 | **`onNewMessage`** 在 **`conversationId == m_currentConvId`** 且 **`msg.direction == "in"`** 时调用 **`tryAggregateAutoReply(conversationId, "T2")`**。 |
| 模式 | 仅当 **`m_modeCombo->currentIndex() == kAggregateModeIndexAutoReply`** 时继续；非自动模式 **直接返回、不打日志**（避免刷屏）。 |
| §4.1 熔断 | 读取 **`QSettings` `aggregateAutoReply/emergencyUserStop`**；为真则 **`qInfo` 跳过**。首次进入 **AI 自动回复** 确认后将该键置 **`false`**（既有逻辑）。 |
| 平台 | **`ConversationDao::findById`**，**`platform == "qianniu"`**；否则 **`qDebug` 跳过**。 |
| §3.2 最后一条方向 | **`lastMessageForConversation`** 的 **`direction == "in"`**；否则 **`qDebug` 跳过**。 |
| 与辅助并发 | **`m_aggregateAiGenerating \|\| m_autoReplyBusy`** 时 **`qInfo` 跳过**（同会话单任务、与辅助互斥的初版策略）。 |
| 生成 | **`buildAggregateAiCompletionRequest(..., silent=true, &skipReason)`**；失败 **`qInfo` 含 skipReason**（如缺 Key、多模态线路不匹配等）。 |
| 网络 | **`m_autoReplyClient`**（**`OpenAiCompatClient`**）独立实例；**`requestChatCompletion(..., stream=true)`**，**`onAutoReplyStreamDelta/Completed/Failed`**；**`max_tokens`** 与辅助同为 **512**。 |
| 发送 | **`onAutoReplyCompleted`**：`trim` 后非空调用 **`ConversationManager::instance().sendMessage(cid, text)`**；空 completion **状态栏提示**，不打开发送。 |
| 中止 | **`abortAggregateAiRequest`**：**`m_aggregateAiClient`** 与 **`m_autoReplyClient`** 均 **`abortActive()`**；并清除 **`m_autoReplyBusy`** / **`m_autoReplyTargetConvId`** / 累积文本。 **「停止自动回复」**、**切换会话** 均会走该路径（前者另有模式回退）。 |

### 10.3 尚未实现或仅部分覆盖的设计项

| 文档章节 | 说明 |
|----------|------|
| §3.2-5、§6 | **`aggregateAutoReply/enabled`** 等 **额外全局开关**、**冷却/防抖**、**失败重试上限** 未接。 |
| §5.3 | **发送前二次校验**（防人工抢先）未做。 |
| §5.2 | **非流式** 可选路径未单独提供（当前与辅助一致为 **流式**）。 |
| §5.4 | **`AggregateAutoReplyController` 独立类** 未建；逻辑在 **`AggregateChatForm`**。 |
| §6 | 新触发在 busy 时 **丢弃并打日志** 已满足；**排队** 未做。 |

---

## 11. 修订记录

| 日期 | 修订 |
|------|------|
| 2026-04-08 | 初稿：目标与范围（千牛首版）、§3 最后一条规则、T1/T2 触发、流水线与辅助复用、并发与配置、阶段划分；与《AI辅助回复方案设计与实现》交叉引用。 |
| 2026-04-08 | 新增 **§4.1 应急一键关**：原「模拟消息」改为 **「停止自动回复」**；`aggregateAutoReply/emergencyUserStop`；切 **人工接待**、中止辅助流式；再次进入 **AI自动回复** 须确认并清标志；**一键开** 明确为后续。**§7** 同步。 |
| 2026-04-08 | 新增 **§10 工程实现对照**：落地 **`MessageDao::lastMessageForConversation`**、**`buildAggregateAiCompletionRequest` + `m_autoReplyClient`**、**T1/T2**、**`tryAggregateAutoReply`** 判定与日志、**`sendMessage`** 与 **`abort`** 联动；更新 **§1.2、§3.1、§4.1、§5.2～5.4、§9** 等与实现状态不一致的表述。 |
