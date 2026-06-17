## 微信 RPA 实现方案

本方案用于从 Windows 微信桌面客户端实时获取聊天消息，写入 `rpa_inbox_messages` 共享表，再由 Qt 端消费后显示在聚合接待界面。

**相关文档**：[微信RPA链路耗时分析与优化方向](./微信RPA链路耗时分析与优化方向.md) · [微信RPA优化方案设计](./微信RPA优化方案设计.md)（版本策略、4.1+ 混合增强、**最小 POC 步骤见该文档 §5**）· [微信RPA融合评估与落地建议](./微信RPA融合评估与落地建议.md)（聚焦 PyUIAutomation 与现有主链路如何融合）· [微信RPA改造方案设计与实现](./微信RPA改造方案设计与实现.md)（记录本轮 UIA 主路径改造、Reader MVP 与阶段计划）· [微信独立工作台方案设计与实现](./微信独立工作台方案设计与实现.md)（记录不并入聚合主链路的微信独立工作台方案与当前实现）· 与 pyweixin POC 的结论见**本文 §1「补充：pyweixin POC」**。

---

## 1. 方案演进

### v1 — Windows OCR 截图（初版，准确率差）

使用 PrintWindow 截图 + Windows OCR 识别消息区域文字。

问题：
- Windows OCR 中文识别准确率低，文字混乱
- 无法区分「对方消息」和「自己消息」
- 整段文字做 hash 去重，稍有抖动就重复入库
- 仅支持单会话

脚本：`wechat_reader_ocr.py`（保留作为参考）

### v2 — wxauto4 / UIAutomation（历史结论：作为主 Reader 不可行）

计划使用 wxauto4 库通过 Windows UIAutomation API 直接读取微信 UI 控件。

**不可行原因：**

1. **微信 4.0.5+ 架构大改**：微信从 Qt Widgets 升级为 Qt Quick/QML + GPU 渲染，UIAutomation 完全无法遍历控件，消息文本不可见。实测微信版本 4.1.7.59 确认不可用。
2. **wxauto4 仓库已清空**：GitHub 仓库（cluic/wxauto4）代码全部删除，仅剩"停止更新"。
3. **wxauto（3.9 版）Python 版本不兼容**：PyPI 上的 wxauto 要求 Python < 3.13，而项目使用 Python 3.13.2。
4. **分发性问题**：即使本机可用，打包给他人后也依赖特定微信版本，无法保证可用。

脚本：`wechat_reader_wxauto.py`（已废弃，不可用）

> 补充说明：上述结论针对当时评估的 **wxauto4 / 纯 UIAutomation 直接作为微信 Reader 主链路**。当前仓库新增的 `poc/pywechat-poc2.0` 已证明，在特定环境下，PyUIAutomation 仍可能准确完成**会话切换、输入框定位、发送按钮定位**等能力，因此更合适的定位是**增强 Writer/Session 的混合后端**，而不是直接替代本文的 PaddleOCR Reader 主路径。详见《[微信RPA融合评估与落地建议](./微信RPA融合评估与落地建议.md)》。

### v3 — PaddleOCR + 布局分析（当前方案）

使用 PaddleOCR 替换 Windows OCR，利用其返回的文字坐标进行布局分析，区分消息发送者。

**相对 v1 的核心改进：**

| 维度 | v1 Windows OCR | v3 PaddleOCR + 布局分析 |
|------|---------------|----------------------|
| 中文识别准确率 | 差 | 高（短文本 >98%，长文本 >95%） |
| 区分发送者 | 不支持 | 基于气泡 x 坐标位置（左=对方，右=自己） |
| 增量检测 | 整段文字 hash，抖动敏感 | 按文字块逐条去重，稳定 |
| 系统消息过滤 | 不支持 | 居中文字块自动过滤 |
| 依赖版本 | Windows 10/11 语言包 | 纯 Python，跨环境一致 |

### 补充：pyweixin POC（`poc/pywechat-poc`）验证结论

对开源仓库 [Hello-Mr-Crab/pywechat](https://github.com/Hello-Mr-Crab/pywechat) 中的 **`pyweixin`**（`pip` / 源码安装）做了本地 POC：`import pyweixin_compat_wechat41` 兼容层 + `smoke_poc.py` 等。结论用于**评估「能否复用为自有微信 RPA 主链路」**，**不**改变本文 **v3 主路径**（截图 + PaddleOCR + 布局）的定位。

| 结论项 | 说明 |
|--------|------|
| **强依赖微信 UIA 细节** | 上游大量写死 `mmui::…`、`Lists.SearchResult`、`Edit`/`ToolBar` 等；**小版本一变**（如 4.1.8）即易出现找不到控件、独立窗 `ChatSingleWindow` 不存在等，需要**持续打补丁**，与「微信版本不可控」的现实冲突。 |
| **POC 未复刻上游完整会话策略** | 上游 `open_dialog_window` 含「**会话列表滚动多页** → 再搜索」；POC 侧主要为**有限条数列表匹配 + 搜索/坐标兜底 + 主窗内嵌 surface**，**不等价**于完整移植 pyweixin。 |
| **他人「克隆即跑通」的常见原因** | 微信版本/界面与作者测试环境更接近、走的发送/会话路径不同、或实际使用 3.9 旧栈等；**不**代表在 4.1.8 与本项目约束下同样稳。 |
| **与主工程的关系** | **优先投入打磨自有微信 RPA**（ROI、红点/列表 OCR、搜索回退、配置化 ROI）：**不绑定**内部 UIA 类名，版本升级时多为**调参/模板**而非整条链重写。 |
| **POC 的保留价值** | 可作开发机上**辅助验证**（例如对比键鼠时序），**不宜**作为与主链路**同级 SLA** 的生产依赖。 |

---

## 2. 技术选型总结

| 方案 | 准确性 | 区分发送者 | 版本依赖 | 可分发 | 结论 |
|------|--------|-----------|---------|--------|------|
| Windows OCR 截图 | 差 | 不支持 | 低 | 可以 | v1 已废弃 |
| wxauto / UIAutomation | 100% | 支持 | **绑定微信 ≤4.0.3** | 不可靠 | **不可行** |
| PyWxDump（数据库解密） | 100% | 支持 | — | — | **法律风险，已关停** |
| **PaddleOCR + 布局分析** | **高** | **支持** | **无** | **可以** | **采用** |

---

## 3. 总体架构

**Python 只写 `rpa_inbox_messages`，Qt 单点维护 `conversations/messages`。**

```
微信窗口 (PrintWindow 截图)
  → 裁剪消息区域
  → PaddleOCR 识别（返回文字 + 坐标）
  → 布局分析（左/右/居中 → 对方/自己/系统）
  → 增量检测（逐条去重）
  → INSERT rpa_inbox_messages (consume_status=0)
  → WechatRPAAdapter (Qt，动态轮询消费：有流量缩短间隔 / 空闲拉长，见 `wechatrp_adapter.cpp`)
  → MessageRouter → conversations/messages
  → AggregateChatForm (聚合接待 UI)
```

Qt/C++ 端**无需任何改动**，数据协议与 v1 完全兼容。

**版本基线（推荐）**：方案验证与迭代以 **微信 PC 4.1 及以上**为主，与当前用户主流版本一致；**4.0.5+ 起主界面 UIA 难以遍历** 的结论仍以 Inspect/实测为准，读消息依赖 **PaddleOCR + 布局** 而非纯 UIA。会话切换、搜索等可逐步引入 **坐标 / 快捷键 / 多后端** 等混合手段，详见《[微信RPA优化方案设计](./微信RPA优化方案设计.md)》。

---

## 4. PaddleOCR 核心能力

### 4.1 安装

```bash
pip install paddlepaddle paddleocr
```

### 4.2 返回格式

```python
from paddleocr import PaddleOCR

ocr = PaddleOCR(use_angle_cls=True, lang='ch')
result = ocr.ocr('chat_screenshot.png', cls=True)

# 每个元素: [四角坐标, (文字, 置信度)]
# [[[x1,y1], [x2,y2], [x3,y3], [x4,y4]], ('你好', 0.98)]
```

四角坐标是文字块的包围框，左上/右上/右下/左下顺序。

### 4.3 首次运行

PaddleOCR 首次运行时会自动下载中文识别模型（约 10-15MB），之后使用本地缓存。

---

## 5. 布局分析：区分消息发送者

### 5.1 微信聊天界面布局特征

```
|        消息区域宽度 (region_width)          |
|                                             |
|  [头像] [对方气泡文字]                       |   ← 左侧 (x < 40%)
|                                             |
|              2025-03-18 15:30               |   ← 居中 (40%-60%)
|                                             |
|                       [我方气泡文字] [头像]  |   ← 右侧 (x > 60%)
```

### 5.2 分类规则

对每个 OCR 文字块，计算其水平中心点 `center_x` 相对于消息区域宽度的位置：

```python
ratio = center_x / region_width

if ratio < 0.4:
    # 左侧 → 对方消息 (direction='in', sender='customer')
elif ratio > 0.6:
    # 右侧 → 自己消息 (direction='out', sender='agent')
else:
    # 居中 → 系统消息（时间戳、提示），跳过不写入
```

> 阈值 0.4 / 0.6 为初始值，可在配置文件中微调。

### 5.3 消息聚合

多个相邻文字块可能属于同一条消息（长消息换行）。按以下规则合并：

1. 按 y 坐标从上到下排序
2. 连续的同侧文字块（y 间距 < 阈值）合并为一条消息
3. 换行处用空格或换行符连接

---

## 6. 增量检测

### 6.1 v1 的问题

v1 对整段 OCR 文字做 SHA1 hash，只要任何一个字符抖动就判定为"新内容"，导致大量重复入库。

### 6.2 v3 改进：逐条消息去重

1. 每轮 OCR 识别后，经布局分析得到若干条消息（内容 + 侧别）
2. 维护"已知消息"滑动窗口（最近 N 条，如 30 条）
3. 对每条消息计算 `content_hash = sha1(side + content)`
4. 仅将不在已知窗口中的消息写入 inbox
5. `platform_msg_id` = `wechat:{conv_id}:{content_hash}` 作为数据库唯一约束兜底

### 6.3 状态持久化

将已知消息窗口保存到 `python/rpa/_state/wechat_last_messages.json`，防止脚本重启后重复入库。

---

## 7. 配置文件

`python/rpa/wechat_config.json`：

```json
{
  "poll_interval_sec": 3,
  "window_match": {
    "process_name": "Weixin.exe",
    "title_contains": "微信"
  },
  "hwnd_hex": "0x12345",
  "chat_region": {
    "mode": "relative_to_window",
    "x": 260,
    "y": 90,
    "w": 720,
    "h": 760
  },
  "conversation": {
    "platform_conversation_id": "demo_wechat_conv_1",
    "customer_name": "演示微信联系人"
  },
  "layout": {
    "left_threshold": 0.4,
    "right_threshold": 0.6,
    "merge_y_gap": 15
  },
  "ocr": {
    "engine": "paddleocr",
    "lang": "ch",
    "min_confidence": 0.6
  }
}
```

字段说明：
- `chat_region` — 消息气泡区域坐标（通过 Qt 端校准工具设置）
- `layout.left_threshold` / `right_threshold` — 左右侧判定阈值
- `layout.merge_y_gap` — 同一条消息的 y 间距合并阈值（像素）
- `ocr.min_confidence` — 低于此置信度的文字块丢弃

---

## 8. 数据库协议（不变）

写入 `rpa_inbox_messages` 表：

| 字段 | 值 | 说明 |
|------|----|------|
| `platform` | `wechat` | 固定 |
| `platform_conversation_id` | 配置中的 conv_id | 当前单会话 |
| `customer_name` | 配置中的 customer_name | 对方名称 |
| `content` | OCR 识别的消息文本 | 已合并同条消息 |
| `platform_msg_id` | `wechat:{conv_id}:{content_hash}` | 唯一去重 |
| `consume_status` | `0` | 新消息 |

> 仅 direction='in'（对方消息）写入 inbox。自己发的消息通过 Qt 端发送流程管理。

---

## 9. 截图方式

复用 v1 的 PrintWindow 截图能力（已验证可用）：

1. **PrintWindow + PW_RENDERFULLCONTENT**（首选）：离屏截图，窗口被遮挡也可用
2. **BitBlt 屏幕截图**（降级）：窗口需可见

微信窗口通过 FloatFollow 模式保持可渲染状态（即使被主窗口覆盖），确保 PrintWindow 可用。

---

## 10. 未来演进

### 10.1 多会话支持

当前为单会话 MVP。后续可扩展：
- 识别左侧会话列表区域，提取各会话的未读标记
- 通过模拟点击切换会话，分别截图 OCR
- 每个会话独立维护 `platform_conversation_id` 和增量状态

### 10.2 更精准的气泡检测

如果简单的 x 坐标阈值不够准确，可引入：
- **YOLOv8 气泡检测**：训练模型识别"对方气泡"/"我方气泡"两个类别，召回率可达 99.5%
- 需准备 ~1000 张标注数据，深色/浅色模式各训练一个模型

### 10.3 写通路（发送消息）

通过坐标定位输入框 + 模拟键盘输入实现发送：
1. Qt 端用户发送消息 → `messages` 表 sync_status=10
2. Python Writer 读取 → 定位微信输入框 → 模拟输入 → 模拟点击发送
3. 更新 sync_status 为 11/12

---

## 11. 依赖清单

```
paddlepaddle
paddleocr
pillow
```

> `winsdk` 不再需要（v1 Windows OCR 的依赖）。如需保留 v1 备用可不删除。

---

## 12. 文件清单

| 文件 | 说明 | 状态 |
|------|------|------|
| `python/rpa/wechat_reader_ocr.py` | v1 Windows OCR 读取器 | 保留参考 |
| `python/rpa/wechat_reader_wxauto.py` | v2 wxauto4 读取器 | 已废弃（不可用） |
| `python/rpa/wechat_config.example.json` | 配置文件示例 | 待更新为 v3 格式 |
| `python/rpa/requirements.txt` | Python 依赖 | 待更新 |
| `src/services/platforms/wechatrp_adapter.cpp` | Qt 端 inbox 消费适配器 | 无需修改 |

---

## 13. 实施步骤

1. **安装 PaddleOCR 依赖** — `pip install paddlepaddle paddleocr`
2. **新建 `wechat_reader_paddleocr.py`** — 基于 v1 截图能力 + PaddleOCR + 布局分析
3. **更新配置文件格式** — 增加 `layout` 和 `ocr` 配置项
4. **更新 `requirements.txt`** — 替换 winsdk 为 paddlepaddle + paddleocr
5. **测试调优** — 校准阈值，验证增量检测，调整 poll_interval
6. **清理废弃文件** — 删除 `wechat_reader_wxauto.py`

---

## 14. 阶段小结（截至 2026-03-24）

本节记录**当前已落地能力**、**已知不足**与**后续可优化项**，便于与《各平台消息链路》及千牛 RPA 阶段对齐；不要求本阶段做到完美。

### 14.1 当前已做到哪里

| 能力 | 说明 |
|------|------|
| **读通路（v3）** | `python/rpa/readers/wechat_reader.py`：接待窗口截图 → PaddleOCR → 布局分析（左/右/中）→ 增量去重（含模糊相似度）→ 写入 `rpa_inbox_messages`（`platform=wechat`）。 |
| **会话与过滤** | 红点驱动自动切换会话；联系人名「连续两次 OCR 一致」再采信；`conversation_list_region.exclude` 过滤公众号/支付等非联系人会话。 |
| **写通路** | `python/rpa/writers/wechat_writer.py`：轮询 `messages`（`sync_status=10`）→ 默认 **后台 PostMessage**（避免组合键误输入）→ 失败时回退前台发送并 **恢复原先前台窗口**；含前台句柄校验，降低误发到其它应用的风险。 |
| **Qt 消费** | `WechatRPAAdapter` 轮询 `rpa_inbox_messages` 注入 `MessageRouter`；发送仍走统一 DB 写 `sync_status=10` 由 Python Writer 消费（与《RPA-DB-协议》一致）。 |
| **配置** | `python/rpa/config/wechat_config.json`：区域、OCR、布局、`send_mode`、`writer_poll_sec` 与 Reader 轮询等可调。 |
| **阶段目标** | **单会话场景下**「微信 ↔ 聚合界面」消息链路已跑通；可日常试用，非生产级完备。 |

> 实际配置文件路径为 `python/rpa/config/wechat_config.json`（文中历史示例路径 `python/rpa/wechat_config.json` 以仓库为准）。

### 14.2 已知不足（本阶段接受、未解决）

- **多会话与身份**：虽能自动切会话采集，但 `platform_conversation_id` / 客户名与真实业务 id 的**稳定映射、合并策略**仍可加强（OCR 抖动、重名、群聊等）。
- **消息形态**：以**文本气泡**为主；图片、语音、引用、链接等**结构化差或无法还原**，可能漏识或噪声。
- **环境与嵌入**：窗口被聚合嵌入、最小化、DPI/缩放、深色主题时，截图与 OCR **偶发不稳定**，需现场校准。
- **写结果校验**：发送后**未做二次 OCR/协议回执**确认，仅以模拟输入成功 + DB 状态为准。
- **性能与体验**：已做轮询与 sleep 调优，但「点击发送到千牛/微信侧可见」仍有**可感知延迟**；再压缩需权衡 OCR/轮询 CPU。
- **合规与长期**：无官方客服 API；RPA 受客户端改版影响，需持续维护。

### 14.3 后续建议优化方向（按需排期）

1. **多会话产品化**：统一 `platform_conversation_id` 规则（如店铺+买家标识）、会话列表与当前会话一致性校验、与聚合侧会话合并策略文档化。  
2. **读质量**：针对噪声/时间戳/系统提示的规则库、可选 `merge_y_gap`/阈值按主题皮肤预设；必要时小模型气泡检测（文档 §10.2）。  
3. **写通路**：发送后轻量校验（例如聊天区尾部 OCR 或短暂等待再比对）；与微信已做的 **PostMessage + 安全回退** 策略在千牛侧复用/适配（千牛多为 CEF，需单独验证）。  
4. **工程化**：配置向导/校准工具与文档同步、日志分级、失败重试与运维面板。  
5. **官方能力**：若后续能申请微信/千牛等**开放平台消息能力**，可并行实现 API 适配器，与 RPA 切换（见《各平台消息链路》）。

---

**与下一阶段的衔接**：千牛 RPA 与微信统一走 **Python RPA + 共享 SQLite**；微信侧已沉淀的 `common`（截图、OCR、布局、增量、DB、输入模拟）可直接复用到千牛 Reader/Writer，减少重复踩坑。
