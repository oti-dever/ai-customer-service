# 拼多多 RPA 实现方案

> 文档目标：定义拼多多平台在当前阶段的可落地实现方案，先完成单会话读写闭环，不在本阶段引入主程序 WebEngine 依赖。
> 更新时间：2026-03-25

---

## 1. 阶段目标与范围

### 1.1 当前阶段目标（MVP）

- 在拼多多商家聊天页面实现 **单会话消息读取（Reader）+ 单会话消息发送（Writer）** 闭环。
- 读链路统一落入 `rpa_inbox_messages`，写链路统一消费 `messages(sync_status=10)`。
- 与微信、千牛保持同构：复用 `python/rpa/common`，Qt 端继续只做 inbox 消费与聚合展示。

### 1.2 当前范围边界

- 覆盖：浏览器窗口发现、聊天区截图 OCR、消息解析去重、单会话发送、状态回写、基础重试与日志。
- 暂不覆盖：多会话并发调度、复杂反自动化规避、主程序内嵌 QWebEngine、全链路监控看板。

---

## 2. 技术路线与选型结论

### 2.1 当前主路线（采用）

- **方案 A：浏览器窗口截图 + OCR + 模拟输入**
- 读：窗口截图 -> OCR -> 布局解析 -> 去重 -> 写入 `rpa_inbox_messages`
- 写：轮询 `messages(sync_status=10)` -> 会话校验 -> 聚焦输入框 -> 粘贴发送 -> 回写 `sync_status`

采用原因：

- 与微信、千牛 RPA 架构完全同构，复用率最高。
- 不引入 Qt WebEngine，主程序依赖与包体更轻。
- 能最快打通业务闭环，风险与维护方式可控。

### 2.2 备选路线（并行验证，不进主线）

- **方案 B：QWebEngineView + JS 注入 DOM**
- 仅在 `test/pdd-webengine` 做阶段 0 可行性验证（选择器稳定性、登录态、发送可控性）。
- 在未证明 DOM 长期稳定前，不作为主线实施。

---

## 3. 总体架构（与现有链路对齐）

```
拼多多商家聊天页（Chrome/Edge）
  -> pdd_reader.py（截图/OCR/解析/去重）
  -> rpa_inbox_messages (consume_status=0)
  -> PDD 适配器（Qt 端轮询消费）
  -> MessageRouter -> conversations/messages -> 聚合界面

聚合界面回复
  -> messages (sync_status=10)
  -> pdd_writer.py（轮询/会话校验/模拟输入发送）
  -> 回写 sync_status=11/12
```

约束原则：

- Python 只负责 RPA 读写与共享库写入，不直接改 Qt 业务库结构。
- 读写都以“当前选中会话”作为第一阶段前提，避免过早处理多会话调度复杂度。

---

## 4. Reader 方案设计（`pdd_reader.py`）

Reader 入口建议：`python/rpa/readers/pdd_reader.py`

### 4.1 核心流程

1. 窗口发现：按配置匹配浏览器进程与标题关键字，或直接使用 `hwnd_hex`。
2. 区域截图：使用 `chat_region` 从整窗截图中裁剪消息区。
3. OCR 识别：调用 `common/ocr_engine.py`，输出文字块与坐标。
4. 布局解析：按 x 坐标划分左/右/中，合并同条消息文本。
5. 过滤与清洗：过滤系统提示、空白、低置信度噪声。
6. 增量去重：`content_hash + 滑动窗口` 去重。
7. 入库：写 `rpa_inbox_messages`，`consume_status=0`。

### 4.2 消息判定建议

- 左侧文本视为客户消息（`direction=in`）优先入库。
- 右侧文本视为我方消息，默认不写 inbox（避免重复）。
- 居中文本（时间轴/系统提示）默认过滤。

### 4.3 关键配置

配置文件建议：`python/rpa/config/pdd_config.json`

- `window_match`：浏览器进程名、标题关键字（如“拼多多商家后台”）。
- `hwnd_hex`：固定窗口句柄（可选，优先级高）。
- `chat_region`：消息区绝对坐标（首选）或比例坐标（兜底）。
- `layout.left_threshold/right_threshold/merge_y_gap`
- `ocr.min_confidence`
- `reader.poll_interval_sec`
- `debug.save_screenshot/debug_dir`

---

## 5. Writer 方案设计（`pdd_writer.py`）

Writer 入口建议：`python/rpa/writers/pdd_writer.py`

### 5.1 核心流程

1. 轮询发送队列：读取 `messages` 中 `platform=pdd_web` 且 `sync_status=10` 的记录。
2. 目标识别：从 `platform_conversation_id` 解析目标会话标识（当前阶段可先弱校验）。
3. 会话校验：OCR 标题区或输入区旁标识，确认当前会话未偏离。
4. 执行动作：激活窗口 -> 点击输入框 -> 粘贴文本 -> Enter/点击发送。
5. 状态回写：
   - 成功：`sync_status=11`
   - 失败：`sync_status=12` + `error_reason`
6. 失败重试：按 `max_retries` 与退避策略重试。

### 5.2 建议错误码

- `ERR_WINDOW_NOT_FOUND`
- `ERR_FOREGROUND_FAILED`
- `ERR_SESSION_MISMATCH`
- `ERR_INPUT_NOT_FOUND`
- `ERR_SEND_FAILED`

---

## 6. 数据协议与字段约定

### 6.1 Reader 入库（`rpa_inbox_messages`）

- `platform`: `pdd_web`
- `platform_conversation_id`: 当前会话标识（先使用配置或 OCR 标题映射）
- `customer_name`: 客户展示名（可为空，后续完善）
- `content`: OCR 解析后的文本
- `platform_msg_id`: `pdd_web:{conv_id}:{content_hash}`
- `consume_status`: `0`

### 6.2 Writer 出库（`messages`）

- 读取：`sync_status=10`
- 成功回写：`sync_status=11`
- 失败回写：`sync_status=12`，并附带 `error_reason`

---

## 7. 工程实施计划（先文档后实现）

### 阶段 0：可运行基线确认（1-2 天）

- 固定浏览器类型（建议 Chrome）与页面缩放（100%）。
- 确认窗口匹配、聊天区与输入区坐标可稳定标定。
- 产出：`pdd_config.json` 初版模板与坐标校准记录。

### 阶段 1：读通路打通（2-4 天）

- 完成 `pdd_reader.py`，实现截图/OCR/解析/去重/入库。
- 验收：连续运行 30 分钟，能稳定采集新消息且重复率可控。

### 阶段 2：写通路打通（2-4 天）

- 完成 `pdd_writer.py`，实现轮询发送、状态回写、基础重试。
- 验收：人工发起 50 条消息，成功率达到预设阈值（如 >=95%）。

### 阶段 3：稳定性增强（2-3 天）

- 增加会话校验、失败分类统计、窗口丢失恢复。
- 验收：异常场景（窗口失焦、轻微页面变化）下具备可恢复能力。

---

## 8. 风险与应对

- 页面结构与样式变动导致坐标漂移  
  - 应对：固定窗口与缩放；优先绝对坐标；提供快速重标定流程。
- OCR 准确率波动导致漏读或误读  
  - 应对：置信度阈值、文本清洗、增量去重、调试截图回溯。
- 发送误投到错误会话  
  - 应对：发送前会话校验失败即中断，返回 `ERR_SESSION_MISMATCH`。
- 浏览器焦点争用影响输入  
  - 应对：前后台发送策略分层，动作后检测输入框状态。

---

## 9. 验收标准（本阶段）

判定“拼多多单会话读写链路打通”需同时满足：

- Reader 可持续写入有效客户消息到 `rpa_inbox_messages`。
- Qt 聚合界面可消费并展示拼多多消息。
- Writer 可消费 `messages(sync_status=10)` 并回发至拼多多页面。
- 发送成功/失败状态可正确回写 `11/12`，失败原因可定位。

---

## 10. 后续演进（不在当前阶段）

- 多会话自动切换与并发调度。
- DOM 注入路线再评估（若 `test/pdd-webengine` 证明稳定）。
- 发送后回执校验（末条消息比对/输入框清空检测）。
- 可观测性建设（Reader/Writer 心跳、成功率、错误分布）。

---

## 11. 结论

拼多多当前阶段采用 **OCR RPA 主路线**，以最小改造成本与现有微信/千牛架构保持一致。  
先完成单会话读写闭环并达到可运维状态，再决定是否引入 WebEngine/DOM 注入路线作为二阶段优化。

---

## 12. 当前进度（已做 / 待完成）

### 12.1 已完成

- Qt/C++：新增了 `PddRPAAdapter`，会轮询消费 `rpa_inbox_messages` 中 `platform='pdd_web'` 的入站消息并交给 `MessageRouter` 落库到聚合界面。
- Python：新增了拼多多 `pdd_reader.py` / `pdd_writer.py` 框架，并扩展 `python/rpa/main.py` 支持 `--platform pdd` / `--platform all`。
- Python：新增了 `python/rpa/config/pdd_config.json` 配置模板（用于窗口匹配、截图 OCR 区域、输入框区域、轮询间隔）。
- UI：在主程序右键菜单中新增了“拼多多OCR区域校准”，可手动框选：
  - 聊天消息区域（写入 `chat_region` 的绝对坐标）
  - 输入框区域（写入 `input_region` 的绝对坐标）

### 12.2 待完成

- 拼多多 Reader 的会话标识/客户名：当前实现主要依赖配置中的 `platform_conversation_id` / `customer_name`，后续需要通过 OCR 标题区或页面元素映射到更稳定的会话 id。
- Reader 的结构化解析细节调优：包括噪声过滤规则、`left/right_threshold`、`merge_y_gap` 以及长消息合并策略，确保对话文本更稳。
- Writer 的“会话正确性”兜底：当前 Writer 主要依赖你已在正确聊天页并按配置点击 `input_region`；后续应增加会话校验（如标题 OCR）或发送后最小回执校验，降低误发风险。
- Writer 的发送触发稳定性：后续若 Enter 不能稳定触发发送，可补充 `send_button` 坐标并优先点击发送按钮。
- 阶段验收：在你登录并打开拼多多商家后台后，需要用人工触发确认读写闭环稳定性（成功率、重复率、失败原因可定位）。

