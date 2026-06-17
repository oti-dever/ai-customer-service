## 项目简介

`yy-ai-customer-service` 是一个基于 `Qt 6 + CMake + SQLite` 的 Windows 桌面客服工作台，目标是把多平台会话、RPA 自动化与 AI 辅助回复整合到同一个应用里。

当前仓库同时包含：

- `src/`：主程序源码
- `tests/`：纳入主工程 `ctest` 的单元测试
- `test/`：与主工程解耦的 POC / 独立验证工程
- `python/rpa/`：Python 侧 RPA 读写器、校准脚本与辅助工具
- `docs/`：需求、设计方案、链路说明与落地记录

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `src/core/` | 会话管理、消息路由、认证等核心流程 |
| `src/data/` | SQLite 数据库与 DAO |
| `src/services/ai/` | OpenAI 兼容接口、方舟文件/Responses 能力 |
| `src/services/platforms/` | 多平台适配器抽象与 RPA 接入 |
| `src/services/wechat/` | 微信工作台相关服务 |
| `src/ui/` | Qt 界面与交互逻辑 |
| `src/utils/` | 日志、样式、加密、图片转换、窗口辅助等工具 |
| `resources/` | 图标、QRC、Windows 资源 |
| `tests/` | 主工程测试目标 |
| `test/` | 独立验证工程，默认不并入主程序构建 |
| `python/rpa/` | Python 自动化脚本、配置与调试工具 |

## 开发环境

- Windows 10/11
- CMake `>= 3.16`
- Qt `6.10.2`（至少包含 `Core / Gui / Network / Svg / Widgets / Sql / Test`）
- MSVC 2022

如果使用 GitHub Actions，仓库中的 CI 也按 `Qt 6.10.2 + win64_msvc2022_64` 配置。

## 构建主程序

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/Qt/6.10.2/msvc2022_64"
cmake --build build --config Release
```

Qt Creator 也可以直接打开仓库根目录的 `CMakeLists.txt`。

## 运行与数据目录

- 应用启动入口：`src/main.cpp`
- 客户端缓存库默认路径：`QStandardPaths::AppDataLocation/client_cache.db`
- Python 服务事实库默认路径：`database/service.db`
- 如检测到旧版 AppData `app.db` 或源码目录下的 `database/app.db`，程序会在首次启动时尝试迁移到客户端缓存库

本地运行态目录如 `.locator/`、`python/rpa/_media/`、`python/rpa/_state/` 默认不应提交。

## 运行测试

仓库当前的主测试集位于 `tests/`，通过 `ctest` 运行：

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="D:/Qt/6.10.2/msvc2022_64"
cmake --build build --config Debug --target yy_ai_customer_service_tests yy_ai_customer_service_data_tests yy_ai_customer_service_router_tests yy_ai_customer_service_openai_tests
ctest --test-dir build -C Debug --output-on-failure
```

当前测试重点覆盖：

- `CryptoUtil` 密码哈希校验
- `ConversationDao / MessageDao` 数据访问
- `MessageRouter` 路由与持久化行为
- `OpenAiCompatClient` 的 SSE 解析逻辑

## CI

CI 工作流位于 `.github/workflows/ci.yml`，当前会在 Windows 上完成：

- 安装 Qt
- 配置并构建主工程
- 执行 `ctest`

## 相关文档

如需继续了解业务链路或方案背景，优先阅读：

- `docs/需求分析与技术方案.md`
- `docs/各平台消息链路.md`
- `docs/RPA-DB-协议.md`
- `docs/开发路线.md`
- `test/README.md`
