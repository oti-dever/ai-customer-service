# 微信 RPA 改造方案设计与实现

本文用于记录当前微信 RPA 从 **OCR 主路径** 向 **UIA 主路径 + OCR 兜底** 演进的设计与实现进展，面向：

- 当前主链路为什么慢、慢在哪里
- 为什么在当前机器与当前微信版本上，UIA 已具备主路径价值
- 已经完成了哪些代码改造
- 下一阶段该如何继续推进

本文与下列文档配套阅读：

- [微信RPA实现方案](./微信RPA实现方案.md)
- [微信RPA优化方案设计](./微信RPA优化方案设计.md)
- [微信RPA融合评估与落地建议](./微信RPA融合评估与落地建议.md)
- [微信独立工作台方案设计与实现](./微信独立工作台方案设计与实现.md)

---

## 1. 背景：为什么要改造

现有微信 RPA 主链路以 **截图 + PaddleOCR + 布局分析** 为核心。该路线在“UIA 树不可用”的历史阶段是合理的，但在当前环境下暴露出两个现实问题：

### 1.1 切会话慢且不稳

当前微信 Writer / Session 为了切到目标会话，主要依赖：

- 搜索框输入
- 搜索框 OCR 校验
- 标题 OCR 校验
- 方向键导航
- 搜索结果区 OCR 兜底

这条链路的优点是兜底多，缺点是：

- 耗时长
- 群聊命中率不稳定
- 焦点和输入风险高
- 业务决策强依赖 OCR

### 1.2 发送链路过度依赖 ROI 与焦点猜测

当前微信 Writer 在输入与发送阶段主要依赖：

- 输入框 ROI
- 鼠标点击焦点
- `Ctrl+A` / `Delete`
- Unicode 逐字输入或粘贴
- Enter 发送

这在窗口布局变化、DPI 变化、焦点漂移时容易变慢或出错。

### 1.3 Reader 的 OCR 成本高

现有 Reader 的慢，不只是“PaddleOCR 本身慢”，而是整个业务决策高度绑定 OCR：

- 会话列表 OCR
- 标题 OCR
- 聊天区 OCR
- 搜索框 / 结果区 OCR
- 输入框 OCR（安全校验 / 回执）

因此，若微信 UIA 树在当前环境下可以稳定暴露，最优方向就不再是“继续在 OCR 主路径上打补丁”，而是把 UIA 拉到主链路中承担会话切换、输入定位、发送，甚至进一步承担消息读取。

---

## 2. 新判断：当前环境下 UIA 已具备主路径价值

### 2.1 环境前提

在当前开发机上，实践结论已确认：

- 只要按 **“先开讲述人，再启动微信”** 的顺序运行微信
- 使用 **Inspect** 可以稳定看到微信内部主要控件
- 使用 `uiautomation` 也可以稳定枚举并操作这些控件

当前阶段**暂不把“如何对外分发给他人”作为首要问题**；优先目标是先解决“现有微信 RPA 太慢、没人愿意用”的体验问题。

### 2.2 已确认的关键控件契约

在当前微信最新版与当前机器上，已确认的关键 UIA 控件如下：

| 用途 | AutomationId | ClassName | ControlType | 说明 |
|------|--------------|-----------|-------------|------|
| 主窗口 | — | `mmui::MainWindow` | `Window` | 微信主窗 |
| 主导航栏 | `MainView.main_tabbar` | `mmui::MainTabBar` | `ToolBar` | 左侧顶级导航 |
| 会话列表 | `session_list` | `mmui::XTableView` | `List` | 会话容器 |
| 单条会话项 | `session_item_<会话名>` | `mmui::ChatSessionCell` | `ListItem` | 单聊/群聊均可见 |
| 聊天区列表 | `chat_message_list` | `mmui::RecyclerListView` | `List` | 当前会话消息列表 |
| 文本消息 | `chat_message_list.qt_scrollarea_viewport.chat_bubble_item_view` | `mmui::ChatTextItemView` | `ListItem` | `Name` 即消息正文 |
| 图片/表情消息 | `chat_message_list.qt_scrollarea_viewport.chat_bubble_item_view` | `mmui::ChatBubbleReferItemView` | `ListItem` | `Name` 为“图片”“动画表情”等占位 |
| 输入框 | `chat_input_field` | `mmui::ChatInputField` | `Edit` | 当前会话输入框 |
| 发送按钮 | — | `mmui::XTextView` | `Text` | `Name=发送` |

### 2.3 当前结论

基于这些控件契约，当前判断已经从“UIA 可作为增强手段”升级为：

- **Writer / Session**：UIA 可以成为主路径
- **Reader**：UIA 已具备做 MVP 的条件
- **OCR**：应逐步降级为兜底路径，而不是继续承担主链路全部职责

---

## 3. 改造目标

### 3.1 目标一：Writer / Session 改为 UIA 主路径

优先把以下动作从 OCR / ROI 路径迁到 UIA：

- 会话切换
- 输入框定位
- 发送按钮定位

### 3.2 目标二：Reader 先做 UIA MVP

先实现一个**独立、可运行、不接正式 inbox** 的 UIA Reader 原型，验证：

- 当前可见消息能否稳定读出
- 文本消息能否直接拿到正文
- 图片 / 表情等消息类型能否拿到结构化占位

### 3.3 目标三：不推翻现有主链路

当前改造策略不是“删除 OCR Reader 并一次性切换”，而是：

- UIA 先进入主链路中最容易见效的部分
- OCR 继续保留
- Reader 改造先走实验并行路线

---

## 4. 目标架构

```mermaid
flowchart LR
msgDb[(messages)] --> writer[WechatWriter]
writer --> sendRoute[SendRouteSelector]
sendRoute --> uiaSend[UiaSendRoute]
sendRoute --> fgSend[ForegroundRoiRoute]
sendRoute --> bgSend[BackgroundSearchOcrRoute]

uiaSend --> uiaSession[UiaSessionOps]
uiaSend --> uiaInput[UiaInputOps]
bgSend --> legacySession[LegacyWechatSession]

readerMain[WechatReader(OCR)] --> inbox[(rpa_inbox_messages)]
readerProbe[WechatReaderUIAProbe] --> uiaReader[WechatReaderUIA]
uiaReader -. 并行验证 .-> inbox
```

设计原则：

- **写链路先切 UIA**
- **读链路先做 MVP**
- **OCR 始终保留兜底**

---

## 5. 已完成的实现

本轮改造已经落地的内容如下。

### 5.1 抽取 UIA 公共层

已新增：

- `python/rpa/common/wechat_uia.py`

该模块承载的能力包括：

- 微信主窗查找
- 会话列表查找
- 单条会话候选查找
- 聊天输入框查找
- 发送按钮查找
- UIA 环境探测
- 当前聊天名称读取
- 当前可见消息样本采集

当前 `wechat_uia.py` 已编码的关键选择器包括：

- `session_list`
- `mmui::ChatSessionCell`
- `session_item_<会话名>`
- `chat_input_field`
- `chat_message_list`
- `mmui::ChatTextItemView`
- `mmui::ChatBubbleReferItemView`

### 5.2 Session 已支持 UIA 切会话

已在：

- `python/rpa/common/wechat_session.py`

增加：

- `switch_to_contact_uia(...)`
- `switch_to_contact_with_strategy(...)`

当前策略已支持：

- 优先按 `ChatSessionCell + session_item_*` 查找会话项
- 失败后回退到名称匹配
- 仍保留原有 foreground/background 搜索路径

### 5.3 Writer 已支持 UIA 主发送

已在：

- `python/rpa/writers/wechat_writer.py`

增加：

- `_send_text_uia(...)`
- `_ensure_in_target_chat_uia(...)`

当前 UIA 发送流程为：

1. 找微信主窗
2. UIA 切到目标会话
3. 找 `chat_input_field`
4. 聚焦输入框
5. 清空
6. 剪贴板粘贴
7. 优先点击 `Name=发送` 的 `XTextView`
8. 若未找到发送按钮，则回退 Enter

原有 foreground / background 路径仍保留。

### 5.4 已新增 UIA 自检脚本

已新增：

- `python/rpa/tools/wechat_uia_probe.py`

用于独立验证：

- 主窗是否可见
- 会话列表是否可见
- 输入框是否可见
- 消息列表是否可见
- 发送按钮是否可见

当前已在本机执行通过，返回结果为：

- `available=true`
- `main_window_found=true`
- `session_list_found=true`
- `chat_input_found=true`
- `message_list_found=true`
- `send_button_found=true`

### 5.5 已新增 UIA Reader 探针

已新增：

- `python/rpa/readers/wechat_reader_uia_probe.py`

当前能力：

- 遍历当前可见消息
- 输出消息样本
- 对消息做类型初分：
  - `text`
  - `bubble_ref`
  - `image`
  - `emoji`

### 5.6 已新增 UIA Reader MVP

已新增：

- `python/rpa/readers/wechat_reader_uia.py`

当前能力：

- 获取当前聊天名称
- 获取当前可见消息
- 输出结构化 JSON
- 为每条消息生成：
  - `seq`
  - `kind`
  - `content`
  - `class_name`
  - `automation_id`
  - `control_type`
  - `bbox`
  - `content_hash`
  - `platform_msg_id`

当前该脚本已在本机成功运行。

---

## 6. Reader MVP 的当前边界

### 6.1 当前已确认可读

在当前环境下，UIA Reader MVP 已确认能够直接读取：

- 文本消息：`mmui::ChatTextItemView`
- 图片/表情等占位消息：`mmui::ChatBubbleReferItemView`

文本消息可直接从 `Name` 获取正文，例如：

- `太难了`
- `太难聊了`
- `死亡三连问`

### 6.2 当前暂不做的事

本阶段明确暂不做：

- 左右方向判定
- 图片 / 表情的精细类型拆分
- 正式写入 `rpa_inbox_messages`
- 替换现有 `wechat_reader.py`

### 6.3 当前默认策略

当前 Reader MVP 只作为并行验证工具：

- 只读当前可见消息
- 方向先统一视为 `unknown`
- 重点验证“文本消息是否稳定可取”

---

## 7. 下一阶段设计

后续改造建议分为 4 个阶段推进。

## 阶段 A：稳定 Writer / Session 的 UIA 主路径

目标：

- 让 Writer 在实际发送时优先走 UIA
- 让会话切换尽量不再依赖 OCR 搜索链

具体步骤：

1. 继续观察 `switch_to_contact_uia()` 在单聊 / 群聊上的命中率
2. 调整会话匹配优先级：
   - 优先 `AutomationId`
   - 其次 `Name`
3. 若有必要，增加会话列表刷新 / 重试策略
4. 在发送阶段进一步利用 `InvokePattern` 或更稳定的点击策略

验收标准：

- 单聊切换明显快于当前 foreground/background 搜索链
- 群聊成功率明显提升
- 发送动作不再依赖 ROI

## 阶段 B：Reader MVP 工程化

目标：

- 把当前 `wechat_reader_uia.py` 从“打印 JSON”升级为“可与正式 Reader 对照运行”的原型

具体步骤：

1. 只保留文本消息作为主验证对象
2. 维持 `side=unknown`
3. 先不写正式 inbox，可考虑：
   - 写日志文件
   - 或写实验性输出文件
4. 与 OCR Reader 做耗时与稳定性对比

验收标准：

- 当前可见文本消息可稳定读出
- 读取速度显著快于聊天区 OCR
- 连续多次运行结果稳定

## 阶段 C：补左右方向判定

目标：

- 让 UIA Reader 能区分“对方消息”和“自己消息”

当前判断：

- 从 Inspect 看，当前左右消息在 `ClassName` 上可能一致
- 差异可能主要体现在 `BoundingRectangle`

因此后续优先方向是：

- 先按 `BoundingRectangle.left/right` 做实验性位置分类
- 若后续在其它机器或其它消息类型上发现更强的父子结构差异，再升级规则

验收标准：

- 可较稳定地区分 `in` / `out`
- 不因单机坐标偏差而大规模误判

## 阶段 D：决定是否接管正式 Reader

目标：

- 决定 UIA Reader 是否有资格替换 OCR Reader 的“聊天区文本读取”部分

前提条件：

1. 当前可见文本消息读取稳定
2. 左右方向判定稳定
3. 增量去重稳定
4. 在多轮测试中，性能与稳定性优于 OCR

若满足条件，可考虑：

- UIA Reader 读取文本消息为主
- OCR Reader 退为 fallback

若不满足条件，则保留：

- OCR Reader 为正式主路径
- UIA Reader 为辅助 / 对照 / 特定环境增强路径

---

## 8. 配置与运行建议

当前已在 `python/rpa/config/wechat_config.json` 中加入 `uia` 配置段，例如：

- `uia.enabled`
- `uia.require_probe_ok`
- `uia.session_match_mode`
- `uia.max_visits`
- `uia.max_depth`
- `uia.prefer_send_button_click`

建议运行顺序：

1. 先运行：

```bash
python python/rpa/tools/wechat_uia_probe.py
```

确认 UIA 环境可用。

2. 再运行：

```bash
python python/rpa/readers/wechat_reader_uia_probe.py
```

观察当前会话消息类型样本。

3. 最后运行：

```bash
python python/rpa/readers/wechat_reader_uia.py
```

查看当前可见消息的结构化结果。

---

## 9. 风险与处理

### 9.1 风险：讲述人前置

当前 UIA 路径依赖：

- 先开讲述人
- 再启动微信

处理方式：

- 当前阶段接受该前提，用于内部验证
- 后续若要面向更广用户，再评估交付说明与环境管理

### 9.2 风险：大版本更新后控件名变动

虽然当前判断这些控件短期内较稳定，但仍需保守处理。

处理方式：

- 每次微信大版本升级后重复执行：
  - `wechat_uia_probe.py`
  - `wechat_reader_uia_probe.py`
- 用 Inspect 再做一次对照

### 9.3 风险：Reader 读取到的仍只是“当前可见消息”

当前 UIA Reader MVP 还没有处理：

- 滚动
- 历史消息加载
- 更完整的增量状态

处理方式：

- 当前阶段只做可见消息验证
- 后续再评估滚动与断点策略

### 9.4 风险：当前输出在 Windows 控制台中出现中文乱码

当前脚本在 Windows 控制台输出 JSON 时，部分中文可能因终端编码显示异常。

处理方式：

- 现阶段优先以结构与计数为准
- 后续如需，可改为写 UTF-8 文件或显式设置控制台编码

---

## 10. 当前结论

截至本文编写时，可以明确给出以下结论：

1. **Writer / Session 的 UIA 主路径已经具备可用性**  
   当前主窗、会话列表、单条会话项、输入框、发送按钮都已被稳定识别并接入代码。

2. **Reader 的 UIA 原型已经具备 MVP 条件**  
   当前可见文本消息和部分非文本消息类型已可直接从 UIA 读取，不再依赖聊天区 OCR。

3. **OCR 不应再被视为唯一主路径**  
   在当前开发机与当前微信版本上，UIA 已经有足够证据进入主链路；OCR 更适合逐步退为兜底策略。

4. **下一阶段优先级非常明确**  
   先稳定 Writer / Session UIA 主路径，再推进 Reader MVP 工程化，最后补左右方向判定并评估是否替换正式 Reader。

---

## 11. 相关代码索引

| 内容 | 位置 |
|------|------|
| 微信 UIA 公共层 | `python/rpa/common/wechat_uia.py` |
| 微信 Session 路由 | `python/rpa/common/wechat_session.py` |
| 微信 Writer | `python/rpa/writers/wechat_writer.py` |
| UIA 自检脚本 | `python/rpa/tools/wechat_uia_probe.py` |
| UIA Reader 探针 | `python/rpa/readers/wechat_reader_uia_probe.py` |
| UIA Reader MVP | `python/rpa/readers/wechat_reader_uia.py` |
| OCR Reader 主链路 | `python/rpa/readers/wechat_reader.py` |
| 微信配置 | `python/rpa/config/wechat_config.json` |

---

*文档目的：作为当前微信 RPA UIA 改造阶段的设计与实现快照；后续若继续补齐左右方向、写库接入、正式 Reader 并联/替换，应在本文增量更新。*
