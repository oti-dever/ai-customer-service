## 微信 RPA 实现方案

本方案用于从 Windows 微信桌面客户端实时获取聊天消息，写入 `rpa_inbox_messages` 共享表，再由 Qt 端消费后显示在聚合接待界面。

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

### v2 — wxauto4 / UIAutomation（验证不可行）

计划使用 wxauto4 库通过 Windows UIAutomation API 直接读取微信 UI 控件。

**不可行原因：**

1. **微信 4.0.5+ 架构大改**：微信从 Qt Widgets 升级为 Qt Quick/QML + GPU 渲染，UIAutomation 完全无法遍历控件，消息文本不可见。实测微信版本 4.1.7.59 确认不可用。
2. **wxauto4 仓库已清空**：GitHub 仓库（cluic/wxauto4）代码全部删除，仅剩"停止更新"。
3. **wxauto（3.9 版）Python 版本不兼容**：PyPI 上的 wxauto 要求 Python < 3.13，而项目使用 Python 3.13.2。
4. **分发性问题**：即使本机可用，打包给他人后也依赖特定微信版本，无法保证可用。

脚本：`wechat_reader_wxauto.py`（已废弃，不可用）

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
  → WechatRPAAdapter (Qt, 800ms 轮询消费)
  → MessageRouter → conversations/messages
  → AggregateChatForm (聚合接待 UI)
```

Qt/C++ 端**无需任何改动**，数据协议与 v1 完全兼容。

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
5. `platform_msg_id` = `wechat_pc:{conv_id}:{content_hash}` 作为数据库唯一约束兜底

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
| `platform` | `wechat_pc` | 固定 |
| `platform_conversation_id` | 配置中的 conv_id | 当前单会话 |
| `customer_name` | 配置中的 customer_name | 对方名称 |
| `content` | OCR 识别的消息文本 | 已合并同条消息 |
| `platform_msg_id` | `wechat_pc:{conv_id}:{content_hash}` | 唯一去重 |
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
