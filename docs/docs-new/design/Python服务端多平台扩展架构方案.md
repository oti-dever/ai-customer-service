# Python 服务端多平台扩展架构方案

## 1. 目标

本文是《C++客户端与Python服务端解耦架构方案》的配套设计，重点说明 Python 服务端如何从当前微信链路扩展为多平台自动化服务。

目标不是把每个平台写成完全独立的一套服务，而是在统一的 Python 服务端内保留清晰的平台边界：

- `python/service/` 负责服务入口、协议、命令分发、事件推送、运行模式和平台注册。
- `python/rpa/core/` 负责跨平台基础能力，例如窗口、UIA、截图、输入模拟、日志和通用稳定性控制。
- `python/rpa/platforms/<platform>/` 负责平台专属自动化能力，例如微信、千牛、拼多多、抖店等。
- C++ 客户端只面对统一的服务协议，不直接依赖任何平台 UIA、OCR 或点击细节。

## 2. 当前状态

当前 Python 侧已经形成了两层结构：

```text
python/
  service/
    server.py
    rpa_bridge.py
    ai_suggestion.py
  rpa/
    core/
    db/
    platforms/
      wechat/
      qianniu/
```

其中：

- `python/service/server.py` 是 HTTP 服务入口。
- `python/service/rpa_bridge.py` 负责 RPA 命令、事件、WebSocket 和 bridge 生命周期。
- `python/rpa/platforms/wechat/adapter.py` 已经承担了微信平台 sidecar adapter 角色。
- `python/rpa/platforms/qianniu/` 已经具备千牛窗口发现、会话扫描、消息读取、消息发送等底层模块，但仍需要正式 adapter 化后接入服务分发。

当前主要问题是：`rpa_bridge.py` 仍偏向单平台硬编码，服务层和平台 adapter 的边界还没有抽象成稳定扩展点。

## 3. 总体分层

建议 Python 服务端按以下四层组织。

```text
C++ Client
  |
  | HTTP / WebSocket JSON
  v
python/service/
  - server.py
  - rpa_bridge.py
  - platform registry
  |
  v
python/rpa/platforms/<platform>/
  - adapter.py
  - detector.py
  - reader.py
  - sender.py
  - config.py
  |
  v
python/rpa/core/
  - UIA / Win32 / screenshot / input / logging
```

职责边界：

| 层级 | 职责 | 不应该做的事 |
|---|---|---|
| C++ 客户端 | UI、聚合会话、用户操作入口、历史同步展示 | 不直接操作平台窗口，不直接读写 Python 内部数据库 |
| `python/service` | 协议、服务生命周期、平台路由、事件推送、运行模式控制 | 不写平台 UIA 细节 |
| `python/rpa/platforms/<platform>` | 平台窗口发现、会话扫描、消息读取、切换、发送、平台诊断 | 不关心 HTTP/WebSocket 细节 |
| `python/rpa/core` | 跨平台工具和稳定性基础设施 | 不依赖具体平台业务语义 |

## 4. 平台目录规范

每个平台建议至少具备以下文件：

```text
python/rpa/platforms/<platform>/
  __init__.py
  adapter.py
  config.py
  detector.py
  reader.py
  sender.py
```

可选文件：

```text
sessions.py       # 会话列表、未读检测、会话切换
uia.py            # 平台 UIA 封装和控件扫描
screenshot.py     # 平台截图、失败现场、OCR 截图区域
role_judgement.py # 消息方向判断
stability.py      # 平台级失败冷却、调试产物
```
建议约束：

- 平台内部使用相对导入，例如 `from .reader import ...`。
- 平台模块可以依赖 `rpa.core`，但 `rpa.core` 不反向依赖平台。
- 平台 adapter 是唯一被 `python/service` 直接调用的平台入口。
- 平台目录内可以存在不同技术路线，例如 UIA、OCR、Clicknium、坐标点击，但必须由 adapter 向外收敛成统一命令。

## 5. 统一 Adapter 接口

建议每个平台 adapter 暴露相同的最小接口：

```python
class PlatformSidecarAdapter:
    platform: str

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        ...

    def health(self) -> dict[str, Any]:
        ...
```

服务层只关心：

- 这个平台叫什么。
- 是否支持某条命令。
- 命令返回是否成功。
- 是否产生统一事件。

服务层不关心：
- 这个平台内部到底走 UIA、OCR、Clicknium 还是网页 DOM。
- 平台内部是一个模块还是拆成 detector / reader / sender。
- 某个平台为了稳定性做了多少兜底分支。

也就是说，服务层看到的是“统一入口”，平台内部自己消化复杂性。

## 6. 服务侧扩展点

现在 `rpa_bridge.py` 已经能承接命令、健康检查和事件存储，但还偏微信单平台。下一步建议先做成“平台注册 + 平台分发”，不用一开始就抽得很重。

建议最小形态：

```python
class PlatformRegistry:
    def __init__(self) -> None:
        self._adapters: dict[str, PlatformSidecarAdapter] = {}

    def register(self, adapter: PlatformSidecarAdapter) -> None:
        self._adapters[adapter.platform] = adapter

    def get(self, platform: str) -> PlatformSidecarAdapter:
        ...
```

`RpaBridge` 只做几件事：

- 启动时注册平台 adapter，例如 `wechat`、`qianniu`。
- 根据 `payload.platform` 找到对应 adapter。
- 在 bridge 层统一做运行模式判断，例如正式模式禁发。
- 统一返回 `success / error` 响应格式。
- 统一保存事件、分页查询事件、对外暴露 `/api/rpa/*` 接口。

这样改完后，bridge 不需要知道微信窗口细节，只需要知道“这个平台有没有注册、能不能执行这条命令”。

## 7. 建议统一的命令面

不要求所有平台一步到位支持全部能力，但命令名字最好先统一，便于 C++ 端保持稳定。

建议保留下面这组命令：

- `connect`
- `disconnect`
- `health_check`
- `fetch_visible_conversations`
- `fetch_visible_messages`
- `scan_unread_and_fetch`
- `prepare_reply_draft`
- `send_message`

补充约束：

- 平台不支持某条命令时，明确返回 `unsupported_command`，不要静默降级。
- 命令参数尽量放进 `parameters`，避免不同平台把顶层字段越堆越乱。
- `prepare_reply_draft` 和 `send_message` 继续区分开，MVP 默认优先草稿回填，不把真实发送做成默认路径。
- 正式模式是否禁发，统一在 service/bridge 层兜住，不散落到每个平台自己判断。

## 8. 建议统一的事件面

当前微信链路已经在往统一事件靠拢，多平台阶段建议继续收口成“少量稳定事件 + 平台扩展 metadata”。

建议事件主集合保持：

- `conversation_observed`
- `message_observed`
- `draft_prepared`
- `send_result_observed`
- `account_health_changed`

建议做法：

- 公共字段尽量统一，例如 `event_id`、`timestamp`、`platform`、`account_id`、`conversation_id`、`message_id`。
- 平台特有信息放 `metadata`，例如微信的方向判断方法、千牛的 OCR 头部识别信息、PDD 的 DOM 节点来源。
- C++ 默认只消费统一字段；确实要做平台特化展示时，再按 `metadata` 增量使用。

这样可以避免每接一个平台，就把 C++ 协议和数据库表再改一遍。

## 9. 平台内部推荐拆法

平台目录不需要机械一致，但建议大体遵守下面的职责：

- `adapter.py`：平台对外总入口，负责命令分发、结果收口、事件组装。
- `detector.py`：窗口发现、主窗口健康检查、前置环境判断。
- `reader.py`：读取当前可见消息、必要时做增量去重。
- `sender.py`：草稿回填、受控发送、发送结果确认。
- `sessions.py`：会话列表、未读扫描、会话切换。
- `stability.py`：失败冷却、互斥、调试产物、重试边界。

其中最关键的一点是：`adapter.py` 负责“对外统一”，`reader/sender/detector` 负责“对内实现”。不要让 `python/service` 直接 import 某个平台的 reader 或 sender。

## 10. 多平台接入顺序

建议不要同时把所有平台都 adapter 化，而是按下面顺序小步推进：

1. 先把 `wechat` 这条链路抽成可复用模板，验证注册表分发和统一返回格式。
2. 再把 `qianniu` 接成第二个平台，优先打通 `health_check / fetch_visible_conversations / fetch_visible_messages / prepare_reply_draft`。
3. `send_message` 放到更后面，等人工确认、失败回执、风控边界清楚后再打开。
4. Web 平台如 PDD、抖店可以复用同一套服务协议，但平台内部实现允许是 DOM/CDP，不必套桌面 UIA 思路。

这样做的好处是，先验证“服务层是否真的已经平台无关”，而不是一开始就把每个平台都半迁不迁地接进来。

## 11. 建议的落地改动

如果只做当前仓库下一步，建议控制在这几项：

1. `python/service/rpa_bridge.py`
   - 去掉只支持 `wechat` 的硬编码判断
   - 引入简单的平台注册表
   - 把正式模式禁发逻辑保留在 bridge

2. `python/rpa/platforms/qianniu/`
   - 增加正式 `adapter.py` 入口
   - 让现有 detector / reader / sender / sessions 能被 adapter 收口调用

3. `python/service/server.py`
   - `/api/rpa/health`
   - `/api/rpa/events`
   - `/api/rpa/command`
   - 这些接口继续保持不变，只把 `platform` 分发交给 bridge

也就是说，优先改内部组织，不急着改 C++ 侧协议。

## 12. 需要提前守住的边界

多平台一接进来，最容易乱的不是功能，而是边界。

建议提前守住几条：

- 不让平台实现反向侵入 `python/service`。
- 不让 C++ 直接感知平台内部模块名和技术路线。
- 不把“是否允许发送”散落到每个平台自己判断。
- 不把平台特有字段硬塞进统一主字段，优先放 `metadata`。
- 不要求所有平台都同时支持全部命令，允许先只读、后草稿、最后受控发送。

## 13. 结论

这份多平台方案的核心不是做一个很重的插件系统，而是先把当前微信 sidecar 继续收口成一个可复制的模板：

- `service` 负责协议、模式、注册和分发
- `platform adapter` 负责平台能力收口
- `platform internals` 负责各自的 UIA/OCR/DOM 细节

先把第二个平台顺利接进同一套 bridge，基本就能证明这条路是对的。后面再逐步补历史同步、平台诊断、失败证据和调试工具，会比现在继续在 bridge 里堆平台分支更稳。