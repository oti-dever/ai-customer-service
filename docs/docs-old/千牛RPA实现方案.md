# 千牛 RPA 实现方案

> 文档目标：聚焦当前阶段（单会话读写链路打通）的可落地方案与现状，不展开过多历史推演与远期设计。
> 更新时间：2026-04-08（千牛校准增加输入框/发送按钮区域；后台发送为先 Enter 再条件点击发送坐标）

---

## 1. 阶段目标与范围

### 1.1 当前阶段目标（已达成）

- 在千牛 PC 端实现 **单会话消息读取**（Reader）与 **单会话消息发送**（Writer）闭环。
- 聚合系统可持续接收千牛消息并回写至千牛会话。

### 1.2 当前范围边界

- 覆盖：千牛接待中心窗口的 OCR 读消息、数据库入库、数据库出库发送、会话校验与发送重试。
- 暂不覆盖：多会话高并发调度、复杂反作弊策略、完善运营后台监控、自动化回放平台。

---

## 2. 技术路线（精简）

千牛聊天区域在 UIA 层不可用，采用 RPA 路线：

- 读：窗口截图 -> OCR -> 版面解析 -> 去重/清洗 -> 写入 `rpa_inbox_messages`
- 写：读取 `messages(sync_status=10)` -> 会话校验/切换 -> 模拟输入与发送 -> 回写发送状态

关键原则：

- 坐标与截图统一到 `PrintWindow` 整窗像素坐标系。
- 配置优先支持手动校准得到的绝对 `x,y,w,h`，比例坐标作为兜底。

---

## 3. 当前实现（Reader）

Reader 入口：`python/rpa/readers/qianniu_reader.py`

### 3.1 已实现能力

- 千牛窗口定位（优先 `hwnd_hex`，其次 `window_match`）。
- 聊天区与标题区截图（支持调试图落盘）。
- OCR 识别与布局解析（左/右/中列分类 + 合并）。
- **结构化消息提取**：识别左侧消息的发送者名称、原始时间戳、消息内容。
- 单会话增量去重（hash + fuzzy）；聚合界面「清空聊天记录 / 删除会话」时 Qt 会写入 `python/rpa/_state/reader_incremental_purge_qianniu.txt`，Reader 每轮消费并 `purge_conversation`，避免仅删 DB 后 OCR 仍被判重复。
- **多会话 Reader 第一阶段**：
  - 支持会话列表区域 OCR 解析当前**可见页**联系人；
  - 支持按当前可见页**串行轮询切会话**后复用现有单会话读逻辑；
  - 支持 `visible_round_robin` 与 `unread_first` 两种扫描模式（后者依赖红点检测效果）。
- 文本清洗与拆分：
  - 处理"多条消息被合并为一段"的拆分；
  - 避免误删"售后无忧：""限时赠送："这类正文前缀。
- **公共会话能力沉淀**：标题 OCR、列表点击切会话、`Ctrl+F` 搜索切会话已抽到 `python/rpa/common/qianniu_session.py`，供 Reader / Writer 共用。

### 3.2 关键配置

配置文件：`python/rpa/config/qianniu_config.json`

- `chat_region`：支持 `x,y,w,h` 绝对坐标（优先）与 ratio（兜底）。
- `contact_header_region`：用于标题 OCR（会话识别）。
- `conversation_list_region`：会话列表区域，供多会话 Reader 扫描与点击切换。
- `input_region`：消息输入框区域（Writer 点击焦点、后台发送取中心；未校准时用 `left/right/top/bottom_ratio`）。
- `send_button`：发送按钮区域（后台 Enter 未生效时的坐标补救；未校准时用 `center_x_ratio` / `center_y_ratio`）。
- `conversation_scan.*`：列表扫描模式、估计行高、每轮最大切换数、切换稳定等待时间等；`use_background_list_click`（默认 `true`）时会话列表切换使用 **PostMessage 后台点击**（与微信 RPA 思路一致，不移动鼠标、不抢前台），若目标控件不响应可改为 `false` 回退为模拟鼠标前台点击。`list_first_chat_ocr`：无列表红点则跳过标题/聊天截图与 OCR；`fallback_full_chat_ocr_interval_sec` 为无红点时的兜底全读间隔（秒）。列表点击未确认会话时可回退 **Ctrl+F** 搜索联系人。
- `unread_detection.*`：会话列表**左带**内高饱和红点像素行检测（`scan_x_start_ratio`～`scan_x_end_ratio`）；配合 `conversation_scan.scan_mode=unread_first` 时未读行优先切换。右侧红字计时为后续阶段。
- `window_lock.*`：Reader / Writer 串行占用千牛窗口的锁参数。`reader_skip_multi_when_pending_send`（默认 `true`）：DB 中存在千牛待发 outbound（`sync_status=10`）时，Reader **仍读当前会话**，但**本轮不跑多会话切换**，缩短持锁时间，让 Writer 优先发出聚合回复。`reader_reduce_activity_when_pending_send`（默认 `true`）+ `reader_poll_interval_sec_when_pending_send` 或 `pending_send_poll_factor`：有待发时拉长 Reader 轮询间隔，减少与 Writer 抢锁。首轮截图仅持锁采集位图，标题/聊天/列表 OCR 在锁外执行，多会话切换仍单独持锁。
- `layout.*`：左右阈值、行合并间距、模糊去重阈值。
- `production_mode`（默认 `true`）：与微信 Reader 一致，为 `true` 时强制关闭 `debug.save_screenshots` 与 `log_parsed_messages`，适合生产；排障改为 `false` 后再开截图。
- `debug.*`：调试截图开关与目录；`log_parsed_messages` 为 `true` 且非 production 时前若干次扫描可打印 layout 逐块日志。

### 3.3 结构化消息提取

千牛对方消息的 OCR 结构：

```
┌─────────────────────────────────────────┐
│ [头像]  店铺名:昵称  2026-3-24 15:33:39 │  ← 头部行
├─────────────────────────────────────────┤
│ 消息正文内容...                          │  ← 内容行
└─────────────────────────────────────────┘
```

布局解析器从头部行提取：

| 字段 | 示例 | 说明 |
|-----|------|------|
| `sender_name` | `oppo平实专卖店:哗哗` | 发送者名称 |
| `original_timestamp` | `2026-3-24 15:33:39` | 原始时间字符串 |
| `content` | `您好哦，亲亲` | 消息正文 |

数据流：

```
OCR blocks → layout_parser.parse_chat_layout() → ParsedMessage(sender_name, original_timestamp, content)
           → qianniu_reader → rpa_inbox_messages(sender_name, original_timestamp)
           → QianniuRPAAdapter → PlatformMessage → MessageRouter → MessageRecord
           → AggregateChatForm.createBubble() 展示（见 3.4）
```

**OCR 分段与解析规则（已落地）**

- **同一 block 含头部**：首块文本同时含「名称 + 时间戳」时，由 `_extract_header_info` 正则解析（名称分隔支持 `:`、`：`、`·`、`•` 等）。
- **名称与时间戳分属相邻 block**：PaddleOCR 常把头部拆成多块，例如 `block[0]='被习惯遗忘的我：小朵'`、`block[1]='2026-3-619:12:41'`、`block[2]=正文`。`_merge_blocks` 在方式 1 未命中时启用**分离模式**：`_is_pure_name` + `_is_pure_timestamp` 匹配前两块，再自第三块起合并为正文。
- **日期与时间之间无空格**：时间戳正则允许日期与时间之间为 **0 个或多个** 空白（含全角空格），以兼容如 `2026-3-619:12:41` 的 OCR 结果。
- **版式切段（千牛 `platform="qianniu"`）**：左侧买家区若仅靠 `merge_y_gap` 合并，多条气泡易被合成一条。`parse_chat_layout` 在买家侧增加版式规则：再次出现**独立一行 `tb` 账号**（`tb`+数字）时，在合并前结束上一条；在已累积至少 3 个块的一条消息后，若下一块为**纯时间戳行**，也先切段（常见于下一气泡的时间行先于账号行出现）。Reader 调用时传入 `platform="qianniu"`。`_merge_blocks` 同时支持「`tb` + 时间」与「时间 + `tb`」两种块序。

实现位置：`python/rpa/common/layout_parser.py`（`ParsedMessage`、`_merge_blocks`、`_TIMESTAMP_PATTERN` 等）。

### 3.4 聚合界面消息展示（千牛式布局）

目标：与对方在千牛客户端中的观感一致——**名称与原始时间在气泡上方一行**，**正文在白色圆角气泡内**。

| 项目 | 说明 |
|------|------|
| 对方消息 | 上方一行灰色小字（样式 `bubbleMetaIn`）：`sender_name` + **两个空格** + `original_timestamp`（无 OCR 时间时回退为消息 `createdAt` 的 `HH:mm`）；下方气泡内仅正文。 |
| 己方消息 | 仍为「正文在上、时间在气泡内底部」，使用 `createdAt` 的 `HH:mm`。 |
| 名称行排版 | **不换行**（`setWordWrap(false)`），且不限制名称行最大宽度，避免「名称与时间被拆成多行」影响观感；极长昵称时整行可能变宽，聊天区或出现横向滚动。 |

实现位置：`src/ui/aggregatechatform.cpp` 中 `AggregateChatForm::createBubble()`。

关联能力：聚合列表切换/发消息后聊天区 **延迟布局再滚到底部**（`scheduleScrollChatToBottom`），减少气泡追加后未贴底的问题。

### 3.5 会话列表扫描与当前校准状态

多会话 Reader 当前采用的思路：

- 先 OCR 标题区与聊天区，处理当前会话；
- 再截图 `conversation_list_region`，解析当前可见页联系人；
- 依据 `conversation_scan.scan_mode` 生成本轮候选会话；
- 按列表点击串行切换，并再次 OCR 标题区确认切换结果。

**未读 / 待接待会话的常见视觉特征（接待中心列表，产品侧观察）**——可组合用于优先级与检测，代码侧目前主要落地了「红点」像素扫描（见 `unread_detection`），其余可作增强：

- 头像角标 **小红点**、右侧 **红色「N秒」** 计时（未回复时递增）；
- **整行浅黄 / 米色底**（新消息行常见；点击该会话或已读后恢复为白底/灰底与正常灰字时间），与已读/当前选中行的灰色底可形成对比。

实现黄色行检测时需考虑：主题升级、DPI、夜间模式、以及「选中行」与「未读黄底」同时存在时的层次关系，宜与红点/右栏红字做 **逻辑或** 而非单独硬判定。

离线自测：将接待中心左侧列表区域（或等价裁切）存为 PNG 后执行  
`python rpa/tools/offline_qianniu_list_test.py --image <路径>`（工作目录为仓库内 `python/`），可打印左/右红带未读行、浅黄/灰底启发式比例及列表 OCR 联系人，无需打开千牛窗口。

当前进展与限制：

- **离线验证已完成**：已用非商家账号的「**淘宝购物消息列表**」截图验证列表 OCR / 分组过滤 / 行分组逻辑，能从该类列表中抽取主要会话项。
- **实机 live 校准未完成**：当前账号的「正在接待」列表为空，尚未对**商家接待列表**做最终区域校准与实机切换验证。
- 因此，当前多会话 Reader 可视为**代码路径已打通、代理列表已验证、商家 live 场景待二次校准**。

---

## 4. 当前实现（Writer）

Writer 入口：`python/rpa/writers/qianniu_writer.py`

### 4.1 已实现能力

- 轮询发送队列：读取 `messages` 表中千牛待发消息。
- 会话目标识别：从 `platform_conversation_id` 解析目标联系人。
- 发送前会话校验：标题 OCR 与目标会话匹配检查。
- 会话切换兜底（顺序）：**OCR 可见会话列表 + 后台点行**（`try_switch_session_via_list_background`，目标须在列表可见）；失败且可前台时再 **`Ctrl+F` 搜索** 回车确认。
- `writer_lazy_ocr_init`（默认 `true`）：与微信 Writer 类似，**首条待发 dequeue 时再加载** Writer 侧 PaddleOCR，减轻与 Reader 同时冷启动双份模型的压力。
- 发送策略：
  - 默认优先后台 PostMessage 发送；
  - **后台路径**：填入文本后 **先按 Enter**（与千牛「按 Enter 键发送」一致）；若聊天区底部哈希未变化，且（在 Writer OCR 可用时）输入框 OCR 仍像含有待发正文片段，再 **点击「发送按钮」校准坐标** 补救；
  - 后台失败回退前台发送；
  - 前台失败可再走后台兜底（可配置）。
- **窗口串行占用**：发送前通过轻量窗口锁与 Reader 协调，避免 Reader 截图时 Writer 正在切会话/发送。
- 结果回写：
  - 成功：`sync_status=11`
  - 失败：`sync_status=12` + `error_reason`
- 错误码：`ERR_WINDOW_NOT_FOUND / ERR_FOREGROUND_FAILED / ERR_SESSION_MISMATCH / ERR_SWITCH_FAILED / ERR_SEND_FAILED`
- **发送后回执校验**：检测输入框清空 / 聊天区底部变化，提升发送确认可靠性。

### 4.2 关键配置（writer）

- `writer_lazy_ocr_init`（根级，默认 `true`）：待发时再初始化 Writer OCR；设为 `false` 则进程启动即加载（与旧行为一致）。
- `writer.max_retries`
- `writer.switch_timeout_sec`
- `writer.require_header_match`
- `writer.allow_background_fallback`
- `writer.prefer_background`
- `receipt_verify.enabled`：是否启用发送后回执校验（默认 true）
- `receipt_verify.timeout_sec`：回执校验超时时间（默认 2.0 秒）

---

## 5. 校准与坐标体系

主程序支持「千牛 OCR 校准」（与微信一致：先弹窗 **选择区域** → 再 **单次** 全屏框选）：

可选区域包括：**聊天消息区域**（`chat_region`）、**会话标题区域**（`contact_header_region`）、**会话列表区域**（`conversation_list_region`）、**消息输入框区域**（`input_region`）、**发送按钮区域**（`send_button`）。每次只校准一项，写入对应键的 `x,y,w,h`（`mode: relative_to_window`），并更新 `hwnd_hex`；其余配置从现有 `qianniu_config.json` 合并保留。`input_region` / `send_button` 供 Writer 取中心点（后台点击输入框、条件补救时点击发送）；未校准时仍可用配置中的比例字段兜底。

入口：**管理启动/停止 RPA** 中「千牛 PC」行「千牛OCR校准」；或侧栏托管千牛后的右键菜单（若已嵌入）。**无需先嵌入**：standalone 会枚举 `AliWorkbench.exe` 且标题含「接待中心」等的主窗。

注意：

- 校准结果与当前窗口尺寸相关；明显改变主窗口布局/缩放后建议重新校准。

辅助脚本：

- `python/rpa/calibrate_qianniu_conversation_list.py`
  - 默认模式：对当前千牛窗口的列表区截图、OCR 并打印识别到的会话；
  - `--image <path>`：对离线图片做 OCR 与列表解析，适合当前无商家接待列表时，用「淘宝购物消息列表」截图先调规则。

---

## 6. 客服侧理想流程与实现差距

本节描述联调中归纳的**目标体验**（客服/操作者视角）、**当前实现**与之差异，以及与之对应的**后续重点**。与「单会话链路已打通」不矛盾：理想流程面向多会话、少人工干预的长期形态。

### 6.1 理想流程（客服视角）

典型操作（与当前联调相近，但期望自动化补全缺口）：

1. 打开千牛接待中心，启动主程序中的千牛 RPA；
2. 在**聚合界面**等待对方消息出现在当前会话聊天区；
3. **若有其他联系人来消息**：仍只需在聚合界面等待或切换会话查看，**不必**为了收消息而手动在千牛里切会话——新消息检测、是否在千牛侧切换会话，应由 **Python Reader（及配套策略）** 完成；
4. **发送消息**：在聚合界面选中目标会话 → 输入框输入 → 点击发送；回写到千牛**正确会话**由 **Python Writer** 按库中会话标识完成，操作者无需关心 Ctrl+F、标题校验等细节。

概括：**读**——多来源进线也能自动进聚合；**写**——聚合选谁发给谁。

### 6.2 当前实现 vs 理想：主要出入

| 维度 | 理想 | 当前实现 |
|------|------|----------|
| **读链路范围** | 能覆盖「多个联系人」的来消息（或至少自动轮询未读并切会话再 OCR） | Reader 已支持**当前可见列表页**的会话扫描与串行切换读取；但商家接待列表的 **live 校准尚未完成**，现阶段主要以代码路径与「淘宝购物消息列表」离线验证为主 |
| **其他人发消息** | 仍在聚合等待即可收到 | 代码上已具备“列表扫描 -> 切会话 -> OCR”的第一阶段能力；但在当前非商家账号环境下，还无法证明真实接待列表中的其他来信能稳定进入流水线 |
| **聚合中选会话** | 与「当前要服务/要读写的对象」一致 | `ConversationManager::selectConversation` 仅更新本程序当前会话、未读等，**不会**通知 Python 去千牛切换联系人；**千牛当前会话**与**聚合当前选中会话**可能不一致 |
| **消息归属** | 与操作者认知一致 | Reader 以**千牛标题 OCR + 当前聊天区**为准入库；若聚合与千牛不同步，可能出现「聚合在看 B，入库内容仍属 A 窗口」的错位感 |
| **发链路** | 聚合发送即回写正确会话 | **已基本符合**：Writer 按 `platform_conversation_id` 解析目标，`Ctrl+F` 切换并标题 OCR 校验后发送；同时已加入 Reader / Writer 的窗口串行锁；仍受搜索命中率、昵称特殊字符、OCR 稳定性等工程因素影响 |

实现锚点（便于对照代码）：

- Reader 主循环：`python/rpa/readers/qianniu_reader.py`（当前会话读取 + 当前可见列表页扫描）。
- 聚合选会话：`src/core/conversationmanager.cpp` 中 `selectConversation`（无千牛联动）。
- Writer 发送与切会话：`python/rpa/writers/qianniu_writer.py` 中 `send_qianniu_message`。
- 公共会话能力：`python/rpa/common/qianniu_session.py`。
- 串行窗口锁：`python/rpa/common/window_lock.py`。

### 6.3 后续重点（相对理想流程的缺口）

1. **千牛 Reader 多会话能力继续收敛**：当前已完成第一阶段代码实现，下一步重点是商家接待列表的 live 校准、切换成功率验证，以及未读优先/高亮行等策略细化。
2. **聚合 ↔ 千牛会话同步（可选但利于体验）**：在聚合中选中某千牛来源会话时，驱动千牛切换到对应联系人（需与 Reader/Writer 调度协调，避免并发冲突）。
3. **与 §8.3 通用项叠加**：多会话下调度与重试、OCR 规则持续打磨、可观测性等。

---

## 7. 验收口径（本阶段）

以下判定"读写链路打通"：

- 能持续读取千牛单会话消息并落库；
- 聚合界面可展示主要文本内容（不再出现大面积碎片化）；
- 待发消息可自动发送回千牛；
- 发送结果可回写状态，失败可定位原因。

---

## 8. 当前已实现 / 不足 / 后续计划

### 8.1 当前已实现

- 单会话收发闭环可用；
- OCR 区域手动校准可用；
- 读链路文本拆分与清洗已完成一轮稳定化；
- 写链路支持后台优先发送 + 前台回退 + 基础重试；
- 关键错误码与日志已可用于排障；
- **发送后回执校验**：检测输入框清空 / 聊天区底部变化，提升发送确认可靠性。
- **结构化字段入库与展示**：`sender_name`、`original_timestamp` 经 `rpa_inbox_messages` / `messages` 贯通至聚合界面。
- **OCR 头部拆分兼容**：名称与时间戳分块、日期与时间无空格等场景已在 `layout_parser` 中处理。
- **聚合界面千牛式气泡**：对方消息元数据在气泡上方一行（名称与时间两个空格分隔），名称行单行不换行；聊天区滚动到底部策略已加固。
- **多会话 Reader 第一阶段代码已落地**：支持当前可见列表页扫描、按列表串行切换读取、基础未读优先模式。
- **会话公共能力已收敛**：标题 OCR、列表点击切换、`Ctrl+F` 搜索切换统一到 `qianniu_session.py`。
- **Reader / Writer 串行锁已落地**：避免截图、切会话、发送互相打断。
- **列表校准脚本已补齐**：支持 live 窗口模式与离线图片模式；已用「淘宝购物消息列表」截图做离线验证。

### 8.2 当前不足

- 多会话并行/抢占发送策略尚未收敛；
- 商家账号下的**接待列表 live 校准**尚未完成；当前多会话验证主要依赖非商家账号的「淘宝购物消息列表」离线截图。
- 「淘宝购物消息列表」与真实商家接待列表在头像、分组、未读样式、可点击行为上可能存在差异，因此当前结果不能等同于最终商家场景验收通过。
- 极端 OCR 场景（复杂富文本、超长混排、含链接长消息）仍可能偶发误分段；
- 运行健康监控与告警能力较弱。
- 聚合界面名称行不设最大宽度时，**超长昵称**可能导致横向滚动；若需可后续改为省略号截断或 Tooltip 全文。

### 8.3 后续要做的事（迭代方向）

- **优先补齐商家 live 校准**：切回商家账号后，对 `conversation_list_region`、`row_height_guess`、点击偏移、未读检测参数做实机调整。
- **与 §6.3 对齐**：继续推进千牛多会话读（列表/未读驱动切换）、聚合选会话与千牛同步等。
- 增强失败重放策略（分级重试、死信处理）；
- 建立运行状态看板（Reader/Writer 心跳、成功率、错误分布）；
- 收敛多会话场景下的切会话与并发发送策略；
- 持续打磨 OCR 解析规则：
  - 动态 `merge_y_gap` 适配不同 DPI/窗口尺寸；
  - 宽文本框（含链接）特殊处理；
  - 富文本卡片识别与跳过。

---

## 9. 结论

本阶段目标（千牛单会话读写链路打通）已完成。  
在此基础上，**多会话 Reader 第一阶段代码与调试链路已补齐**，并已通过「淘宝购物消息列表」离线截图完成代理验证；下一步需在商家账号下完成接待列表 live 校准，才能真正进入面向业务场景的多会话稳定性迭代。
