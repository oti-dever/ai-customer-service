# 千牛 RPA 技术实现方案

> **文档定位**：本文档是《消息推送与回写设计方案》中"模式 B：RPA/读屏"在千牛平台的**详细技术实现方案**。  
> **前置依据**：2026-03 在千牛 PC 客户端上运行 Inspect.exe 的实测结论。  
> **与现有架构关系**：实现 `IPlatformAdapter` 接口（见《需求分析与技术方案》4.4.4），可与模拟平台适配器、未来的千牛 API 适配器**平级切换**。

---

## 一、Inspect.exe 实测结论（决策依据）

### 1.1 测试环境

| 项目 | 信息 |
|------|------|
| 千牛版本 | PC 客户端（2026-03 最新版） |
| 操作系统 | Windows 11 |
| 工具 | Inspect.exe（Windows SDK） |
| 测试账号 | tb4947894539 |

### 1.2 各区域探测结果

对千牛"接待中心"窗口的三个关键区域分别放置鼠标，Inspect.exe 的探测结果如下：

| 区域 | Inspect 识别到的元素 | ControlType | FrameworkId | UIA Pattern 支持 |
|------|---------------------|-------------|-------------|------------------|
| **中间消息区** | `"tb4947894539-接待中心"` 窗口 | Window (0xC370) | Win32 | 仅 Window / Transform / LegacyIAccessible |
| **左侧会话列表** | 同上（同一顶层窗口） | 同上 | 同上 | 同上 |
| **底部输入框** | 同上（同一顶层窗口） | 同上 | 同上 | 同上 |

**关键属性**（三个区域完全一致）：

| 属性 | 值 |
|------|-----|
| LegacyIAccessible.Role | 客户端 (0xa) |
| IsTextPatternAvailable | **false** |
| IsValuePatternAvailable | **false** |
| IsScrollPatternAvailable | **false** |
| IsSelectionPatternAvailable | **false** |
| IsInvokePatternAvailable | **false** |

### 1.3 结论

**千牛"接待中心"的整个聊天界面对 Windows UIA 完全不透明。**

无论鼠标放在消息区、会话列表还是输入框，Inspect 都只能识别到同一个顶层窗口 `"tb4947894539-接待中心"`，无法深入到任何内部子元素。控件树中仅有若干空名称的"窗格"嵌套，聊天区域的消息文本、会话列表项、输入框控件均**不可达**。

千牛的接待中心几乎确定使用**内嵌浏览器引擎（CEF/Chromium）渲染**，整个区域在 Win32 层面表现为一个不透明的矩形。

> **对比**：同一千牛客户端中的"授权"弹窗页面可通过 UIA 识别出"文本"、"列表"、"列表项目"等细粒度元素，说明千牛的不同功能模块使用了不同的 UI 渲染技术。聊天区域是 Web 渲染，而部分系统弹窗使用了原生控件。

### 1.4 技术路线定性

| 操作 | UIA 方案 | 实际路线 |
|------|----------|----------|
| 读取聊天消息 | **不可行** | **OCR 截图识别** |
| 检测新消息到达 | **部分可行**（窗口标题） | **多信号融合**（标题监控 + OCR 差异 + 托盘闪烁） |
| 切换会话 | **不可行** | **坐标计算 + 鼠标模拟** |
| 输入回复文本 | **不可行** | **坐标点击 + 剪贴板粘贴** |
| 点击发送按钮 | **不可行** | **坐标点击 / 模拟 Enter** |

---

## 二、整体架构

### 2.1 在系统中的位置

```
┌───────────────────────────────────────────────────────────────────┐
│                          服务端                                    │
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────┐  │
│  │SimPlatformAdapter │  │ (未来)            │  │ MessageRouter  │  │
│  │ (模拟平台)        │  │QianniuIMAdapter  │  │ (统一消息路由)  │  │
│  │ 模式 A            │  │ 模式 A (API)     │  └───────┬────────┘  │
│  └────────┬─────────┘  └──────────────────┘          │            │
│           └──────────────────────────────────────────►│            │
│                                                       │            │
│  ┌────────────────────────────────────────────────────▼────────┐  │
│  │                  数据库 (conversations / messages)            │  │
│  └────────────────────────────────────────────────────┬────────┘  │
│                                                       │            │
│  ┌────────────────────────────────────────────────────▼────────┐  │
│  │              WebSocket Gateway (推送网关)                     │  │
│  └────────────────────────────────────────┬───────────────────┘  │
└───────────────────────────────────────────┼───────────────────────┘
                                            │ WebSocket
┌───────────────────────────────────────────▼───────────────────────┐
│                        桌面客户端 (Qt)                             │
│                                                                    │
│  ┌─────────────────────────┐     ┌──────────────────────────┐     │
│  │  QianniuRPAAdapter       │     │   聚合对话接待界面        │     │
│  │  (千牛 RPA 适配器)        │────►│   (AggregateChatWindow)  │     │
│  │  模式 B                  │     │                          │     │
│  │                          │     │  ┌──────┬───────┬─────┐ │     │
│  │  · OCR 消息采集           │     │  │会话  │对话区  │客户 │ │     │
│  │  · 坐标模拟输入           │     │  │列表  │       │信息 │ │     │
│  │  · 新消息检测             │     │  └──────┴───────┴─────┘ │     │
│  └─────────────┬───────────┘     └──────────────────────────┘     │
│                │                                                    │
│  ┌─────────────▼───────────┐                                      │
│  │  千牛 PC 客户端           │                                      │
│  │  (嵌入窗口 / 独立窗口)    │                                      │
│  └─────────────────────────┘                                      │
└───────────────────────────────────────────────────────────────────┘
```

### 2.2 QianniuRPAAdapter 与 IPlatformAdapter 的关系

`QianniuRPAAdapter` 实现与模拟平台、未来 API 适配器**完全相同的接口**：

```cpp
class QianniuRPAAdapter : public IPlatformAdapter {
    Q_OBJECT
public:
    void connect() override;        // 查找并绑定千牛窗口
    void disconnect() override;     // 释放窗口绑定
    void startListening() override; // 启动 OCR 轮询线程
    void stopListening() override;  // 停止轮询
    void sendMessage(const QString &conversationId,
                     const QString &text) override; // RPA 模拟发送

signals:
    void incomingMessage(PlatformMessage msg);
    void conversationUpdated(ConversationInfo c);
};
```

`MessageRouter` 通过 `platform` 字段找到对应适配器，上层代码完全不感知底层是 API 还是 RPA。未来千牛 API 恢复后，只需新增 `QianniuIMAdapter`（模式 A），在配置中切换即可，无需改动 `MessageRouter`、聚合界面等任何上层逻辑。

### 2.3 模式切换机制

```cpp
// 配置文件 (config.toml)
[platform.taobao]
mode = "rpa"           # 可选值: "rpa" | "api" | "embed_only"
rpa_poll_interval = 500 # 毫秒
ocr_engine = "paddleocr" # 可选: "paddleocr" | "tesseract" | "windows_ocr"

// PlatformManager 根据配置创建对应适配器
IPlatformAdapter* PlatformManager::createAdapter(const PlatformConfig& cfg) {
    if (cfg.mode == "rpa")
        return new QianniuRPAAdapter(cfg);
    else if (cfg.mode == "api")
        return new QianniuIMAdapter(cfg);   // 未来
    else
        return nullptr;  // embed_only 模式无需适配器
}
```

---

## 三、千牛窗口管理

### 3.1 窗口发现

千牛 PC 客户端的窗口结构（来自 Inspect.exe）：

```
"tb4947894539-千牛工作台" 窗口       ← 主窗口（外壳）
  └─ "tb4947894539-接待中心" 窗口    ← 接待中心（聊天区域的容器）
       └─ "千牛工作台" 窗格          ← 内部渲染区
```

关键发现：千牛有**两个层级的窗口**——外层"千牛工作台"是主框架，内层"接待中心"是聊天功能的独立子窗口。RPA 需要定位的是**"接待中心"窗口**。

**窗口查找策略**：

```cpp
struct QianniuWindowInfo {
    HWND hwndMain;       // "千牛工作台" 主窗口
    HWND hwndChat;       // "接待中心" 窗口
    RECT chatClientRect; // 接待中心的客户区矩形
    QString accountId;   // 从窗口标题提取的账号 ID
};

QianniuWindowInfo findQianniuWindow() {
    QianniuWindowInfo info = {};
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t title[256];
        GetWindowTextW(hwnd, title, 256);
        QString titleStr = QString::fromWCharArray(title);
        
        // 匹配模式: "<账号ID>-千牛工作台"
        if (titleStr.endsWith("-千牛工作台") || titleStr.endsWith("-接待中心")) {
            auto* info = reinterpret_cast<QianniuWindowInfo*>(lParam);
            if (titleStr.contains("接待中心")) {
                info->hwndChat = hwnd;
                // 提取账号 ID
                info->accountId = titleStr.left(titleStr.indexOf("-"));
            } else if (titleStr.contains("千牛工作台")) {
                info->hwndMain = hwnd;
            }
        }
        return TRUE; // 继续枚举
    }, reinterpret_cast<LPARAM>(&info));
    
    // 获取接待中心的客户区矩形
    if (info.hwndChat) {
        GetClientRect(info.hwndChat, &info.chatClientRect);
    }
    return info;
}
```

### 3.2 窗口区域映射

千牛接待中心的 UI 布局是固定的三栏结构。通过对实际千牛界面的像素分析，定义各功能区域的**相对坐标比例**：

```
┌──────────┬────────────────────────────┬──────────────┐
│          │      顶部标题栏区域         │              │
│          │   (店铺名 / 提示信息)       │   右侧面板   │
│ 左侧     ├────────────────────────────┤   (订单/     │
│ 会话     │                            │    商品信息)  │
│ 列表     │     中间消息区域             │              │
│          │   (聊天气泡 / 消息内容)      │              │
│  约30%   │                            │    约20%     │
│ 宽度     │       约50% 宽度            │   宽度       │
│          │                            │              │
│          ├────────────────────────────┤              │
│          │   工具栏 (表情/剪刀/截图)    │              │
│          ├────────────────────────────┤              │
│          │   底部输入框区域             │              │
│          │   约15% 高度               │              │
│          ├──────────────┬─────────────┤              │
│          │   关闭按钮   │   发送按钮    │              │
└──────────┴──────────────┴─────────────┴──────────────┘
```

**区域坐标定义**（基于接待中心窗口客户区的比例）：

```cpp
struct RegionConfig {
    // 所有值为相对于接待中心客户区的比例 (0.0 ~ 1.0)
    
    // 左侧会话列表区域
    struct {
        double left   = 0.0;
        double top    = 0.12;  // 跳过顶部搜索栏和标签
        double right  = 0.30;
        double bottom = 1.0;
    } sessionList;
    
    // 中间消息区域
    struct {
        double left   = 0.30;
        double top    = 0.10;  // 跳过顶部店铺名/提示
        double right  = 0.80;
        double bottom = 0.72;  // 到工具栏之前
    } messageArea;
    
    // 底部输入框区域
    struct {
        double left   = 0.30;
        double top    = 0.78;
        double right  = 0.80;
        double bottom = 0.92;
    } inputArea;
    
    // 发送按钮
    struct {
        double centerX = 0.73;
        double centerY = 0.96;
    } sendButton;
};
```

> **重要**：以上比例为初始估算值，实际使用前需通过截图像素分析精确校准。方案提供**校准工具**（见 3.3），支持运行时调整。

### 3.3 区域校准工具

由于千牛的 UI 布局可能随版本、屏幕分辨率、DPI 缩放而变化，需要提供一个**可视化校准工具**：

**功能**：
1. 对千牛接待中心窗口截图，以半透明覆盖层显示在 Qt 窗口中
2. 用户通过拖拽矩形框标注"会话列表"、"消息区域"、"输入框"、"发送按钮"的位置
3. 自动计算相对比例并保存到配置文件
4. 支持"测试 OCR"按钮——对标注区域截图并运行 OCR，验证结果正确性

**实现**：基于 `QGraphicsView` + `QRubberBand`，工作量约 1-2 天。

```cpp
class RegionCalibrator : public QDialog {
public:
    RegionCalibrator(HWND targetWindow, QWidget* parent = nullptr);
    
    void captureAndShow();                   // 截图并显示
    RegionConfig getCalibrationResult();     // 获取校准结果
    void testOCR(const QRect& region);       // 测试区域 OCR
    void saveConfig(const QString& path);    // 保存到配置文件
};
```

### 3.4 窗口状态监控

`QianniuRPAAdapter` 需要持续监控千牛窗口的状态：

```cpp
enum class WindowState {
    NotFound,       // 千牛未运行或接待中心未打开
    Normal,         // 正常状态，可以操作
    Minimized,      // 窗口最小化
    Occluded,       // 被其他窗口遮挡
    Foreground,     // 在前台（可能正在被用户操作）
    TitleChanged    // 窗口标题发生变化（可能有新消息）
};

WindowState checkWindowState(HWND hwnd) {
    if (!IsWindow(hwnd))
        return WindowState::NotFound;
    if (IsIconic(hwnd))
        return WindowState::Minimized;
    if (GetForegroundWindow() == hwnd)
        return WindowState::Foreground;
    // 可选: 检查是否被遮挡 (通过比较窗口区域的可见性)
    return WindowState::Normal;
}
```

**窗口不可用时的降级策略**：

| 状态 | 处理 |
|------|------|
| NotFound | 标记平台离线，停止轮询，定期重试查找窗口 |
| Minimized | OCR 不可用（截图为空），暂停消息采集，监控窗口恢复 |
| Occluded | 尝试 `PrintWindow` API 截图（不依赖窗口可见性），若失败则暂停 |
| Foreground | 正常采集，但发送操作需排队等待用户停止操作 |

---

## 四、OCR 消息采集

### 4.1 截图策略

#### 4.1.1 截图方式选择

| 方式 | API | 优点 | 缺点 | 选择 |
|------|-----|------|------|------|
| `BitBlt` 屏幕截图 | `GetDC(NULL)` + `BitBlt` | 简单快速 | 窗口必须可见且不被遮挡 | 备选 |
| `PrintWindow` | `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` | 窗口可被遮挡甚至最小化时仍可截图 | 部分应用不支持，性能略低 | **首选** |
| `DXGI Desktop Duplication` | IDXGIOutputDuplication | 性能最好，GPU 加速 | 实现复杂，全屏截图需裁剪 | 暂不采用 |

**推荐**：首选 `PrintWindow`，因为千牛窗口在嵌入模式下可能被其他面板部分遮挡。如果 `PrintWindow` 对千牛无效（部分 CEF 应用不响应 `WM_PRINT`），降级为 `BitBlt`。

#### 4.1.2 区域截图实现

```cpp
class ScreenCapture {
public:
    // 对指定窗口的指定区域截图，返回 QImage
    static QImage captureRegion(HWND hwnd, const QRect& region) {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int fullW = clientRect.right - clientRect.left;
        int fullH = clientRect.bottom - clientRect.top;
        
        // 方式一: PrintWindow (首选)
        HDC hdcWindow = GetDC(hwnd);
        HDC hdcMem = CreateCompatibleDC(hdcWindow);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, fullW, fullH);
        SelectObject(hdcMem, hBitmap);
        
        BOOL result = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
        
        if (!result) {
            // 降级: BitBlt 从屏幕 DC 截取
            HDC hdcScreen = GetDC(NULL);
            POINT pt = {0, 0};
            ClientToScreen(hwnd, &pt);
            BitBlt(hdcMem, 0, 0, fullW, fullH,
                   hdcScreen, pt.x, pt.y, SRCCOPY);
            ReleaseDC(NULL, hdcScreen);
        }
        
        // 转换为 QImage
        QImage fullImage = QtWin::fromHBITMAP(hBitmap);
        
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        
        // 裁剪到目标区域
        return fullImage.copy(region);
    }
};
```

#### 4.1.3 截图频率控制

```cpp
// 自适应轮询频率
class AdaptivePollTimer {
    int baseIntervalMs = 500;    // 基础间隔
    int currentIntervalMs = 500;
    int maxIntervalMs = 3000;    // 最大间隔（无变化时逐步退避）
    int minIntervalMs = 300;     // 最小间隔（检测到活跃对话时加速）
    int noChangeCount = 0;

public:
    int nextInterval() {
        if (noChangeCount > 10) {
            // 长时间无变化，逐步降低频率
            currentIntervalMs = qMin(currentIntervalMs + 200, maxIntervalMs);
        } else if (noChangeCount == 0) {
            // 刚检测到变化，恢复高频
            currentIntervalMs = minIntervalMs;
        }
        return currentIntervalMs;
    }
    
    void onNewMessageDetected() { noChangeCount = 0; }
    void onNoChange() { noChangeCount++; }
};
```

### 4.2 OCR 引擎集成

#### 4.2.1 引擎选型

结合千牛聊天界面的特征（白色背景、标准中文字体、清晰渲染），各 OCR 引擎的适用性分析：

| 引擎 | 中文识别精度 | 速度（单帧） | 部署复杂度 | 模型大小 | 推荐度 |
|------|-------------|-------------|-----------|---------|--------|
| **PaddleOCR** | ★★★★★ | 50-150ms | 中等（需 ONNX Runtime） | ~10MB | **首选** |
| **Windows OCR** | ★★★☆ | 30-80ms | 极低（系统内置） | 0 | **次选** |
| **Tesseract** | ★★★☆ | 100-300ms | 低（vcpkg 安装） | ~15MB | 备选 |
| **云端 OCR** | ★★★★★ | 200-500ms (含网络) | 低 | 0 | 不推荐（延迟+成本） |

**推荐路线**：

1. **初期开发**：使用 **Windows OCR**（零部署成本，快速验证方案可行性）
2. **精度不足时**：切换到 **PaddleOCR**（C++ 推理，通过 ONNX Runtime 集成）
3. 通过接口抽象支持引擎热切换

#### 4.2.2 OCR 引擎抽象接口

```cpp
struct OCRResult {
    struct TextBlock {
        QString text;           // 识别文本
        QRect boundingBox;      // 文本在图片中的矩形区域
        float confidence;       // 置信度 0.0~1.0
    };
    QVector<TextBlock> blocks;  // 按从上到下、从左到右排序
    int processingTimeMs;       // 处理耗时
};

class IOCREngine {
public:
    virtual ~IOCREngine() = default;
    virtual bool initialize(const QString& modelPath = "") = 0;
    virtual OCRResult recognize(const QImage& image) = 0;
    virtual QString engineName() const = 0;
};

// Windows OCR 实现
class WindowsOCREngine : public IOCREngine {
    // 使用 Windows.Media.Ocr WinRT API
    bool initialize(const QString&) override;
    OCRResult recognize(const QImage& image) override;
    QString engineName() const override { return "WindowsOCR"; }
};

// PaddleOCR 实现
class PaddleOCREngine : public IOCREngine {
    // 使用 PaddleOCR 的 ONNX Runtime C++ 推理
    bool initialize(const QString& modelPath) override;
    OCRResult recognize(const QImage& image) override;
    QString engineName() const override { return "PaddleOCR"; }
};
```

#### 4.2.3 Windows OCR 集成方式

Windows 10+ 内置的 OCR 通过 WinRT API 调用：

```cpp
// 需要链接: WindowsApp.lib
// 需要包含: <winrt/Windows.Media.Ocr.h>
//           <winrt/Windows.Graphics.Imaging.h>

#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Graphics::Imaging;

OCRResult WindowsOCREngine::recognize(const QImage& image) {
    OCRResult result;
    auto start = std::chrono::steady_clock::now();
    
    // QImage → SoftwareBitmap (通过内存流中转)
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "BMP");
    
    // ... WinRT 流转换 ...
    
    OcrEngine engine = OcrEngine::TryCreateFromLanguage(
        winrt::Windows::Globalization::Language(L"zh-Hans-CN"));
    
    auto ocrResult = engine.RecognizeAsync(softwareBitmap).get();
    
    for (auto const& line : ocrResult.Lines()) {
        OCRResult::TextBlock block;
        block.text = QString::fromStdWString(std::wstring(line.Text()));
        // 计算行的边界矩形
        auto words = line.Words();
        if (words.Size() > 0) {
            auto first = words.GetAt(0).BoundingRect();
            auto last = words.GetAt(words.Size() - 1).BoundingRect();
            block.boundingBox = QRect(
                first.X, first.Y,
                last.X + last.Width - first.X,
                qMax(first.Height, last.Height));
        }
        block.confidence = 0.9f; // Windows OCR 不提供单行置信度
        result.blocks.append(block);
    }
    
    auto end = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<
        std::chrono::milliseconds>(end - start).count();
    
    return result;
}
```

### 4.3 消息解析与结构化

OCR 返回的是原始文本块列表，需要将其解析为结构化的聊天消息。

#### 4.3.1 千牛消息区域的 OCR 文本特征

观察千牛聊天界面，每条消息的 OCR 输出具有以下特征：

```
买家消息格式（典型）:
  [买家昵称]        [时间]
  消息内容第一行
  消息内容第二行（如有）

商家回复格式（典型）:
            [时间]   [商家标识]
            回复内容第一行
            回复内容第二行（如有）

系统提示格式:
      该会话因您购物咨询产生...
```

关键区分特征：
- **买家消息**：文本靠左对齐，头像/昵称在左侧
- **商家回复**：文本靠右对齐，头像/昵称在右侧
- **系统提示**：居中显示，无头像
- **时间戳**：通常在消息上方居中或右侧

#### 4.3.2 消息解析器

```cpp
struct ParsedMessage {
    enum Direction { Incoming, Outgoing, System };
    
    Direction direction;
    QString senderName;
    QString content;
    QString timestamp;      // OCR 识别的时间文本
    QRect sourceRegion;     // 该消息在截图中的区域
};

class MessageParser {
public:
    QVector<ParsedMessage> parse(const OCRResult& ocrResult, 
                                  int imageWidth) {
        QVector<ParsedMessage> messages;
        ParsedMessage current;
        bool inMessage = false;
        
        int centerX = imageWidth / 2;
        
        for (const auto& block : ocrResult.blocks) {
            int blockCenterX = block.boundingBox.center().x();
            
            // 跳过系统提示（居中文本且包含特征关键词）
            if (isSystemPrompt(block.text)) {
                if (inMessage) {
                    messages.append(current);
                    inMessage = false;
                }
                continue;
            }
            
            // 检测时间戳行（匹配 HH:MM 或 YYYY-MM-DD 等模式）
            if (isTimestamp(block.text)) {
                if (inMessage) {
                    messages.append(current);
                }
                current = ParsedMessage();
                current.timestamp = block.text.trimmed();
                inMessage = false;
                continue;
            }
            
            // 根据水平位置判断消息方向
            if (!inMessage) {
                current.content.clear();
                if (blockCenterX < centerX * 0.7) {
                    current.direction = ParsedMessage::Incoming;
                } else if (blockCenterX > centerX * 1.3) {
                    current.direction = ParsedMessage::Outgoing;
                }
                current.sourceRegion = block.boundingBox;
                inMessage = true;
            }
            
            // 追加消息内容
            if (!current.content.isEmpty())
                current.content += "\n";
            current.content += block.text;
            
            // 更新区域范围
            current.sourceRegion = current.sourceRegion.united(
                block.boundingBox);
        }
        
        if (inMessage) {
            messages.append(current);
        }
        
        return messages;
    }

private:
    bool isTimestamp(const QString& text) {
        // 匹配: "14:30", "2026-03-06 14:30", "03-06 14:30", "昨天 14:30"
        static QRegularExpression re(
            R"((\d{4}[-/])?\d{1,2}[-/]\d{1,2}\s+\d{1,2}:\d{2}|"
            R"(\d{1,2}:\d{2}|昨天|前天|今天))");
        return re.match(text.trimmed()).hasMatch() 
               && text.trimmed().length() < 20;
    }
    
    bool isSystemPrompt(const QString& text) {
        static QStringList keywords = {
            "该会话因", "购物咨询产生", "手机淘宝",
            "也能收发", "手机查看", "以下商品"
        };
        for (const auto& kw : keywords) {
            if (text.contains(kw)) return true;
        }
        return false;
    }
};
```

### 4.4 增量检测算法

核心挑战：每次 OCR 都会获取当前可见的全部消息，需要区分哪些是**新消息**。

#### 4.4.1 基于文本指纹的增量检测

```cpp
class IncrementalDetector {
    // 已知消息的文本指纹集合
    QSet<QString> knownFingerprints;
    // 上一次 OCR 的完整文本快照
    QString lastSnapshot;
    // 上一次截图的哈希值（用于快速判断画面是否变化）
    quint64 lastImageHash = 0;
    
public:
    struct DetectionResult {
        QVector<ParsedMessage> newMessages;
        bool hasScrolled;  // 是否发生了滚动
    };
    
    DetectionResult detect(const QImage& currentImage,
                           const QVector<ParsedMessage>& parsedMessages) {
        DetectionResult result;
        
        // 第一步: 快速图像哈希比较（避免无变化时的重复解析）
        quint64 currentHash = computeImageHash(currentImage);
        if (currentHash == lastImageHash) {
            return result; // 画面未变化
        }
        lastImageHash = currentHash;
        
        // 第二步: 逐条消息检查指纹
        for (const auto& msg : parsedMessages) {
            QString fingerprint = computeFingerprint(msg);
            if (!knownFingerprints.contains(fingerprint)) {
                knownFingerprints.insert(fingerprint);
                result.newMessages.append(msg);
            }
        }
        
        // 第三步: 检测滚动（已知消息在新结果中消失）
        // 这表明用户或系统滚动了消息区域
        // ... 滚动检测逻辑 ...
        
        return result;
    }

private:
    QString computeFingerprint(const ParsedMessage& msg) {
        // 使用消息方向 + 内容前50字符 + 时间戳组合作为指纹
        return QString("%1|%2|%3")
            .arg(int(msg.direction))
            .arg(msg.content.left(50))
            .arg(msg.timestamp);
    }
    
    quint64 computeImageHash(const QImage& image) {
        // 感知哈希 (pHash): 缩放到 8x8 → 灰度 → 计算均值 → 生成位图
        QImage small = image.scaled(8, 8, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_Grayscale8);
        int total = 0;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                total += qGray(small.pixel(x, y));
        int avg = total / 64;
        
        quint64 hash = 0;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (qGray(small.pixel(x, y)) > avg)
                    hash |= (1ULL << (y * 8 + x));
        return hash;
    }
};
```

#### 4.4.2 防重复与去噪

| 问题 | 解决方案 |
|------|----------|
| OCR 识别结果略有差异（如"你好"被识别为"你好.") | 指纹计算时做文本标准化（去标点、去空格） |
| 同一条消息因滚动位置不同导致 boundingBox 变化 | 指纹不包含位置信息，仅基于文本内容 |
| 消息区域滚动后旧消息重新出现 | `knownFingerprints` 持久保存已处理消息 |
| 指纹集合无限膨胀 | 定期清理超过 24 小时的指纹；按会话隔离指纹集 |

---

## 五、新消息实时检测

### 5.1 多信号融合检测机制

仅靠 OCR 轮询来检测新消息存在延迟。采用多信号联合检测可以在**新消息到达的第一时间**触发 OCR 采集：

```
┌──────────────────────────────────────────────────────────────┐
│                    新消息检测引擎                               │
│                                                              │
│  信号源 1: 窗口标题监控 ─────────────────────┐                │
│    · 千牛新消息时窗口标题可能闪烁/变化        │                │
│    · 通过 GetWindowText 定时检查              │                │
│    · 检测频率: 200ms                         │                │
│                                              ▼                │
│  信号源 2: 任务栏闪烁检测 ──────────────►  信号融合器         │
│    · FlashWindowEx 产生的 WM_FLASHWINDOW   (任一信号触发即    │
│    · 通过 Shell Hook 或轮询检测             启动 OCR 采集)    │
│    · 检测频率: 500ms                         │                │
│                                              ▼                │
│  信号源 3: 像素级变化检测 ─────────────┐   立即执行 OCR       │
│    · 对消息区域底部条带快速截图         │   (跳过等待间隔)     │
│    · 计算像素差异比例                  │                      │
│    · 检测频率: 300ms                   │                      │
│    · 开销: 仅截取 ~50px 高度的条带     ┘                      │
│                                                              │
│  兜底: 定时 OCR 全量扫描 ───────────────────────────────────  │
│    · 即使无信号触发，每 N 秒做一次完整 OCR                    │
│    · 间隔: 3-5 秒（自适应）                                   │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 信号源实现

#### 5.2.1 窗口标题监控

```cpp
class TitleMonitor : public QObject {
    Q_OBJECT
    HWND hwnd;
    QString lastTitle;
    QTimer timer;

public:
    TitleMonitor(HWND hwnd) : hwnd(hwnd) {
        timer.setInterval(200);
        QObject::connect(&timer, &QTimer::timeout, this, &TitleMonitor::check);
    }
    
    void start() { 
        lastTitle = getTitle();
        timer.start(); 
    }

signals:
    void titleChanged(const QString& oldTitle, const QString& newTitle);

private slots:
    void check() {
        QString current = getTitle();
        if (current != lastTitle) {
            emit titleChanged(lastTitle, current);
            lastTitle = current;
        }
    }

private:
    QString getTitle() {
        wchar_t buf[512];
        GetWindowTextW(hwnd, buf, 512);
        return QString::fromWCharArray(buf);
    }
};
```

#### 5.2.2 消息区域底部条带变化检测

对消息区域最底部 50 像素的窄条进行截图和比较，开销远低于全量 OCR：

```cpp
class PixelChangeDetector : public QObject {
    Q_OBJECT
    HWND hwnd;
    QRect stripRegion;    // 消息区域底部条带
    QImage lastStrip;
    QTimer timer;
    
    static constexpr int STRIP_HEIGHT = 50; // 像素
    static constexpr double CHANGE_THRESHOLD = 0.05; // 5% 像素变化即触发

public:
    PixelChangeDetector(HWND hwnd, const QRect& messageAreaRect)
        : hwnd(hwnd) {
        // 取消息区域最底部 50px
        stripRegion = QRect(
            messageAreaRect.left(),
            messageAreaRect.bottom() - STRIP_HEIGHT,
            messageAreaRect.width(),
            STRIP_HEIGHT);
        timer.setInterval(300);
        QObject::connect(&timer, &QTimer::timeout, 
                         this, &PixelChangeDetector::check);
    }

signals:
    void changeDetected();

private slots:
    void check() {
        QImage current = ScreenCapture::captureRegion(hwnd, stripRegion);
        if (lastStrip.isNull()) {
            lastStrip = current;
            return;
        }
        
        double diffRatio = computeDifference(lastStrip, current);
        lastStrip = current;
        
        if (diffRatio > CHANGE_THRESHOLD) {
            emit changeDetected();
        }
    }

private:
    double computeDifference(const QImage& a, const QImage& b) {
        if (a.size() != b.size()) return 1.0;
        int diffPixels = 0;
        int totalPixels = a.width() * a.height();
        for (int y = 0; y < a.height(); y++) {
            const QRgb* lineA = reinterpret_cast<const QRgb*>(a.scanLine(y));
            const QRgb* lineB = reinterpret_cast<const QRgb*>(b.scanLine(y));
            for (int x = 0; x < a.width(); x++) {
                if (qAbs(qRed(lineA[x]) - qRed(lineB[x])) > 10 ||
                    qAbs(qGreen(lineA[x]) - qGreen(lineB[x])) > 10 ||
                    qAbs(qBlue(lineA[x]) - qBlue(lineB[x])) > 10) {
                    diffPixels++;
                }
            }
        }
        return double(diffPixels) / totalPixels;
    }
};
```

### 5.3 信号融合器

```cpp
class NewMessageDetector : public QObject {
    Q_OBJECT
    
    TitleMonitor* titleMonitor;
    PixelChangeDetector* pixelDetector;
    QTimer fullScanTimer;      // 兜底定时全量扫描
    QElapsedTimer lastOCRTime; // 防止短时间内重复触发 OCR
    
    static constexpr int OCR_COOLDOWN_MS = 400; // OCR 最小间隔

public:
    NewMessageDetector(HWND hwnd, const RegionConfig& regions) {
        titleMonitor = new TitleMonitor(hwnd);
        // ... 初始化 pixelDetector ...
        
        fullScanTimer.setInterval(3000); // 3 秒兜底
        
        // 任一信号触发 → 请求 OCR
        QObject::connect(titleMonitor, &TitleMonitor::titleChanged,
                         this, &NewMessageDetector::onSignalTriggered);
        QObject::connect(pixelDetector, &PixelChangeDetector::changeDetected,
                         this, &NewMessageDetector::onSignalTriggered);
        QObject::connect(&fullScanTimer, &QTimer::timeout,
                         this, &NewMessageDetector::onSignalTriggered);
    }

signals:
    void requestOCRScan();  // 通知主轮询循环立即执行 OCR

private slots:
    void onSignalTriggered() {
        if (lastOCRTime.elapsed() < OCR_COOLDOWN_MS) return;
        lastOCRTime.restart();
        emit requestOCRScan();
    }
};
```

---

## 六、会话管理

### 6.1 会话列表识别

左侧会话列表同样需要通过 OCR 识别。与消息区域不同，会话列表需要识别的信息包括：

- 店铺/买家名称
- 最后消息摘要
- 时间
- 未读标记（红点/数字）

```cpp
struct DetectedSession {
    QString name;           // 买家/店铺名
    QString lastMessage;    // 最后一条消息摘要
    QString time;           // 时间标注
    int unreadCount = 0;    // 未读数（0=无红点）
    QRect clickRegion;      // 点击该会话的区域坐标
    bool isSelected = false;// 是否当前选中（蓝色高亮）
};

class SessionListParser {
public:
    QVector<DetectedSession> parse(const QImage& sessionListImage,
                                    const OCRResult& ocrResult) {
        QVector<DetectedSession> sessions;
        
        // 千牛会话列表每项的大致高度约 60-80 像素
        // 通过 OCR 文本块的 Y 坐标聚类来分割不同会话
        auto clusters = clusterByY(ocrResult.blocks, 40); // 40px 为聚类阈值
        
        for (const auto& cluster : clusters) {
            DetectedSession session;
            for (const auto& block : cluster) {
                if (isTimeLike(block.text)) {
                    session.time = block.text;
                } else if (block.text.contains("商家") || 
                           block.boundingBox.y() == cluster.first().boundingBox.y()) {
                    session.name = block.text;
                } else {
                    session.lastMessage += block.text;
                }
            }
            
            // 计算该会话项的点击中心坐标
            QRect unionRect;
            for (const auto& block : cluster)
                unionRect = unionRect.united(block.boundingBox);
            session.clickRegion = unionRect;
            
            // 检测选中状态（蓝色背景）
            session.isSelected = isBlueBackground(
                sessionListImage, unionRect);
            
            // 检测未读红点（在区域右上角查找红色像素团）
            session.unreadCount = detectUnreadBadge(
                sessionListImage, unionRect);
            
            sessions.append(session);
        }
        
        return sessions;
    }

private:
    bool isBlueBackground(const QImage& img, const QRect& rect) {
        // 采样区域中心几个像素的蓝色通道
        int cx = rect.center().x();
        int cy = rect.center().y();
        if (cx >= img.width() || cy >= img.height()) return false;
        QRgb pixel = img.pixel(cx, cy);
        // 千牛选中状态的蓝色背景 RGB 大约在 (52, 145, 240) 附近
        return qBlue(pixel) > 180 && qRed(pixel) < 100;
    }
    
    int detectUnreadBadge(const QImage& img, const QRect& rect) {
        // 在右上角 20x20 区域查找红色像素密度
        QRect badgeArea(rect.right() - 20, rect.top(), 20, 20);
        int redCount = 0;
        for (int y = badgeArea.top(); y < badgeArea.bottom() && y < img.height(); y++) {
            for (int x = badgeArea.left(); x < badgeArea.right() && x < img.width(); x++) {
                QRgb px = img.pixel(x, y);
                if (qRed(px) > 200 && qGreen(px) < 80 && qBlue(px) < 80)
                    redCount++;
            }
        }
        return (redCount > 30) ? 1 : 0; // 简化：仅判断有无，不解析具体数字
    }
};
```

### 6.2 会话切换操作

当聚合界面需要查看某个特定会话的消息时，RPA 需要在千牛左侧列表中点击对应会话：

```cpp
void QianniuRPAAdapter::switchToSession(const QString& sessionName) {
    // 1. OCR 识别左侧会话列表
    QImage listImage = ScreenCapture::captureRegion(
        windowInfo.hwndChat, getAbsoluteRect(regions.sessionList));
    OCRResult ocrResult = ocrEngine->recognize(listImage);
    auto sessions = sessionParser.parse(listImage, ocrResult);
    
    // 2. 查找目标会话
    int targetIdx = -1;
    for (int i = 0; i < sessions.size(); i++) {
        if (sessions[i].name.contains(sessionName)) {
            targetIdx = i;
            break;
        }
    }
    
    if (targetIdx < 0) {
        emit error("未在千牛会话列表中找到: " + sessionName);
        return;
    }
    
    // 3. 如果目标已选中，无需点击
    if (sessions[targetIdx].isSelected) return;
    
    // 4. 计算屏幕坐标并点击
    QRect clickRect = sessions[targetIdx].clickRegion;
    // 将相对坐标转换为屏幕坐标
    POINT screenPt = regionToScreen(
        windowInfo.hwndChat,
        regions.sessionList,
        clickRect.center());
    
    simulateClick(screenPt.x, screenPt.y);
    
    // 5. 等待会话切换完成（消息区域变化）
    QThread::msleep(300);
}
```

### 6.3 会话列表滚动

如果目标会话不在当前可见范围内，需要滚动列表：

```cpp
void QianniuRPAAdapter::scrollSessionList(int direction) {
    // direction: -1 向上, +1 向下
    
    // 将鼠标移动到会话列表区域中心
    POINT center = regionToScreen(
        windowInfo.hwndChat,
        regions.sessionList,
        QPoint(
            (regions.sessionList.left + regions.sessionList.right) / 2 
                * clientWidth,
            (regions.sessionList.top + regions.sessionList.bottom) / 2 
                * clientHeight));
    
    SetCursorPos(center.x, center.y);
    
    // 模拟鼠标滚轮
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = direction * WHEEL_DELTA * 3; // 滚动 3 行
    SendInput(1, &input, sizeof(INPUT));
    
    QThread::msleep(200); // 等待滚动动画
}
```

---

## 七、回复发送流程

### 7.1 完整发送链路

```
┌──────────────────────────────────────────────────────────────────┐
│  QianniuRPAAdapter::sendMessage(conversationId, text)            │
│                                                                  │
│  步骤 1: 切换到目标会话                                           │
│    ├── 查找 conversationId 对应的千牛会话名                       │
│    ├── OCR 识别左侧列表，定位目标会话                              │
│    ├── 模拟点击切换                                               │
│    └── 等待 300ms，验证切换成功（当前选中会话名匹配）               │
│                                                                  │
│  步骤 2: 聚焦输入框                                               │
│    ├── 计算输入框区域中心的屏幕坐标                                │
│    ├── 模拟鼠标点击输入框区域                                     │
│    └── 等待 100ms                                                │
│                                                                  │
│  步骤 3: 清空输入框                                               │
│    ├── 模拟 Ctrl+A (全选)                                        │
│    ├── 等待 50ms                                                 │
│    └── 模拟 Delete 键                                            │
│                                                                  │
│  步骤 4: 输入文本（剪贴板粘贴）                                    │
│    ├── 保存当前剪贴板内容（用完后恢复）                             │
│    ├── 将回复文本写入剪贴板                                       │
│    ├── 模拟 Ctrl+V                                               │
│    ├── 等待 100ms                                                │
│    └── 恢复原剪贴板内容                                           │
│                                                                  │
│  步骤 5: 发送                                                    │
│    ├── 方案 A: 模拟点击"发送"按钮（坐标点击）                      │
│    └── 方案 B: 模拟 Enter 键（如果千牛默认 Enter 发送）            │
│                                                                  │
│  步骤 6: 验证发送成功                                             │
│    ├── 等待 500ms                                                │
│    ├── 对消息区域截图 + OCR                                       │
│    ├── 检查最新消息是否包含刚发送的文本                             │
│    └── 返回成功/失败                                              │
└──────────────────────────────────────────────────────────────────┘
```

### 7.2 核心实现

```cpp
bool QianniuRPAAdapter::sendMessage(const QString& conversationId,
                                     const QString& text) {
    // 获取互斥锁，防止并发发送
    QMutexLocker locker(&sendMutex);
    
    // === 步骤 1: 切换会话 ===
    QString sessionName = getSessionName(conversationId);
    switchToSession(sessionName);
    
    // === 步骤 2: 点击输入框 ===
    POINT inputCenter = regionToScreen(
        windowInfo.hwndChat,
        regions.inputArea,
        QPoint(
            (regions.inputArea.left + regions.inputArea.right) / 2 
                * clientWidth,
            (regions.inputArea.top + regions.inputArea.bottom) / 2 
                * clientHeight));
    simulateClick(inputCenter.x, inputCenter.y);
    QThread::msleep(100);
    
    // === 步骤 3: 清空输入框 ===
    simulateKeyCombo(VK_CONTROL, 'A'); // Ctrl+A
    QThread::msleep(50);
    simulateKey(VK_DELETE);
    QThread::msleep(50);
    
    // === 步骤 4: 剪贴板粘贴 ===
    ClipboardGuard clipGuard; // RAII: 保存/恢复剪贴板
    setClipboardText(text);
    simulateKeyCombo(VK_CONTROL, 'V'); // Ctrl+V
    QThread::msleep(150);
    
    // === 步骤 5: 发送 ===
    // 优先尝试点击"发送"按钮
    POINT sendBtnPos = regionToScreen(
        windowInfo.hwndChat,
        QPointF(regions.sendButton.centerX, regions.sendButton.centerY));
    simulateClick(sendBtnPos.x, sendBtnPos.y);
    QThread::msleep(500);
    
    // === 步骤 6: 验证 ===
    return verifySendSuccess(text);
}
```

### 7.3 底层模拟操作封装

```cpp
class InputSimulator {
public:
    // 模拟鼠标点击
    static void simulateClick(int screenX, int screenY) {
        SetCursorPos(screenX, screenY);
        QThread::msleep(30);
        
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
    
    // 模拟键盘按键
    static void simulateKey(WORD vk) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vk;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
    
    // 模拟组合键 (如 Ctrl+A, Ctrl+V)
    static void simulateKeyCombo(WORD modifier, WORD key) {
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = modifier;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = key;
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = key;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = modifier;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    }
    
    // 将文本写入剪贴板
    static bool setClipboardText(const QString& text) {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        
        std::wstring wstr = text.toStdWString();
        size_t size = (wstr.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hMem) {
            CloseClipboard();
            return false;
        }
        
        wcscpy_s(static_cast<wchar_t*>(GlobalLock(hMem)),
                 wstr.size() + 1, wstr.c_str());
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
        return true;
    }
};

// RAII 剪贴板保存/恢复
class ClipboardGuard {
    QString savedText;
    bool hasSaved = false;
public:
    ClipboardGuard() {
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                auto* text = static_cast<wchar_t*>(GlobalLock(hData));
                if (text) {
                    savedText = QString::fromWCharArray(text);
                    hasSaved = true;
                }
                GlobalUnlock(hData);
            }
            CloseClipboard();
        }
    }
    ~ClipboardGuard() {
        if (hasSaved) {
            InputSimulator::setClipboardText(savedText);
        }
    }
};
```

### 7.4 坐标转换工具

将比例坐标转换为屏幕绝对坐标：

```cpp
// 将区域比例坐标 → 接待中心客户区内的像素坐标
QRect getAbsoluteRect(HWND hwndChat, const RegionConfig::Area& area) {
    RECT client;
    GetClientRect(hwndChat, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    return QRect(
        int(area.left * w),
        int(area.top * h),
        int((area.right - area.left) * w),
        int((area.bottom - area.top) * h));
}

// 将客户区像素坐标 → 屏幕坐标
POINT clientToScreen(HWND hwnd, int clientX, int clientY) {
    POINT pt = {clientX, clientY};
    ClientToScreen(hwnd, &pt);
    return pt;
}

// 一步到位: 比例坐标 → 屏幕坐标
POINT regionToScreen(HWND hwndChat, double ratioX, double ratioY) {
    RECT client;
    GetClientRect(hwndChat, &client);
    int cx = int(ratioX * (client.right - client.left));
    int cy = int(ratioY * (client.bottom - client.top));
    return clientToScreen(hwndChat, cx, cy);
}
```

---

## 八、主轮询循环

### 8.1 QianniuRPAAdapter 工作线程

```cpp
void QianniuRPAAdapter::startListening() {
    workerThread = QThread::create([this]() {
        spdlog::info("QianniuRPAAdapter: 开始监听");
        
        AdaptivePollTimer pollTimer;
        IncrementalDetector detector;
        MessageParser parser;
        
        while (!stopRequested) {
            // 1. 检查窗口状态
            WindowState state = checkWindowState(windowInfo.hwndChat);
            if (state == WindowState::NotFound) {
                spdlog::warn("千牛窗口丢失，尝试重新查找...");
                windowInfo = findQianniuWindow();
                if (!windowInfo.hwndChat) {
                    QThread::msleep(5000);
                    continue;
                }
            }
            if (state == WindowState::Minimized) {
                QThread::msleep(1000);
                continue;
            }
            
            // 2. 截取消息区域
            QRect msgRect = getAbsoluteRect(
                windowInfo.hwndChat, regions.messageArea);
            QImage msgImage = ScreenCapture::captureRegion(
                windowInfo.hwndChat, msgRect);
            
            if (msgImage.isNull()) {
                spdlog::warn("消息区域截图失败");
                QThread::msleep(1000);
                continue;
            }
            
            // 3. OCR 识别
            OCRResult ocrResult = ocrEngine->recognize(msgImage);
            spdlog::debug("OCR 识别完成: {} 个文本块, 耗时 {}ms",
                          ocrResult.blocks.size(), 
                          ocrResult.processingTimeMs);
            
            // 4. 解析消息
            auto parsedMessages = parser.parse(ocrResult, msgImage.width());
            
            // 5. 增量检测
            auto detection = detector.detect(msgImage, parsedMessages);
            
            // 6. 处理新消息
            for (const auto& msg : detection.newMessages) {
                if (msg.direction == ParsedMessage::Incoming) {
                    PlatformMessage platformMsg;
                    platformMsg.platform = "taobao";
                    platformMsg.conversationId = currentConversationId();
                    platformMsg.customerName = 
                        getCurrentSessionName(); // 从左侧列表识别
                    platformMsg.content = msg.content;
                    platformMsg.direction = "in";
                    platformMsg.createdAt = QDateTime::currentDateTime();
                    
                    emit incomingMessage(platformMsg);
                    pollTimer.onNewMessageDetected();
                    
                    spdlog::info("检测到新消息: [{}] {}", 
                                 platformMsg.customerName.toStdString(),
                                 msg.content.left(50).toStdString());
                }
            }
            
            if (detection.newMessages.isEmpty()) {
                pollTimer.onNoChange();
            }
            
            // 7. 定期扫描会话列表（频率低于消息区域）
            static int scanCounter = 0;
            if (++scanCounter % 10 == 0) { // 每 10 次消息扫描做 1 次列表扫描
                scanSessionList();
            }
            
            // 8. 等待下一轮
            QThread::msleep(pollTimer.nextInterval());
        }
        
        spdlog::info("QianniuRPAAdapter: 监听已停止");
    });
    
    workerThread->start();
}
```

### 8.2 会话列表定期扫描

```cpp
void QianniuRPAAdapter::scanSessionList() {
    QRect listRect = getAbsoluteRect(
        windowInfo.hwndChat, regions.sessionList);
    QImage listImage = ScreenCapture::captureRegion(
        windowInfo.hwndChat, listRect);
    
    if (listImage.isNull()) return;
    
    OCRResult ocrResult = ocrEngine->recognize(listImage);
    auto sessions = sessionParser.parse(listImage, ocrResult);
    
    // 检测新会话或未读变化
    for (const auto& session : sessions) {
        if (session.unreadCount > 0) {
            // 如果有未读消息的会话不是当前活跃会话
            // 通知上层可能需要切换过去读取
            emit conversationUpdated(ConversationInfo{
                .platform = "taobao",
                .customerName = session.name,
                .unreadCount = session.unreadCount,
                .lastMessage = session.lastMessage
            });
        }
    }
    
    // 更新已知会话列表（用于后续切换查找）
    knownSessions = sessions;
}
```

### 8.3 多会话消息采集策略

千牛 RPA 一次只能看到**当前选中会话**的消息。如果有多个会话有新消息，需要**轮流切换采集**：

```cpp
class MultiSessionCollector {
    QQueue<QString> pendingSessionQueue; // 待采集的会话名队列
    QString currentActiveSession;
    int maxSwitchPerMinute = 10; // 每分钟最大切换次数
    QElapsedTimer lastSwitchTime;

public:
    // 有新的未读会话时加入队列
    void enqueueSession(const QString& sessionName) {
        if (!pendingSessionQueue.contains(sessionName)) {
            pendingSessionQueue.enqueue(sessionName);
        }
    }
    
    // 决定是否需要切换会话
    QString nextSessionToCollect() {
        if (pendingSessionQueue.isEmpty()) return "";
        
        // 限制切换频率
        if (lastSwitchTime.elapsed() < 60000 / maxSwitchPerMinute)
            return "";
        
        lastSwitchTime.restart();
        return pendingSessionQueue.dequeue();
    }
};
```

> **权衡**：频繁切换会话会导致"窗口跳动"，影响用户体验。建议在千牛窗口被嵌入且用户未在操作时才执行自动切换，用户正在操作时暂停自动切换。

---

## 九、异常处理

### 9.1 异常分类与处理策略

| 异常场景 | 检测方式 | 处理策略 |
|----------|----------|----------|
| **千牛未运行/已退出** | `FindWindow` 返回 NULL | 标记离线，每 10s 重试查找 |
| **接待中心窗口关闭** | HWND 失效 (IsWindow=false) | 重新枚举窗口，查找新的接待中心 |
| **千牛弹窗遮挡** | 截图内容与预期差异过大 / 检测到弹窗特征 | 识别弹窗类型，尝试自动关闭（点击确定/关闭） |
| **窗口被最小化** | `IsIconic` 返回 true | 暂停 OCR，监控窗口恢复 |
| **OCR 识别率突降** | 连续多帧识别文本块为 0 | 触发截图诊断，保存截图供人工排查 |
| **发送失败** | 验证步骤未检测到已发送文本 | 重试一次；仍失败则通知上层，保留消息内容 |
| **千牛版本更新** | 区域校准验证失败 / 持续 OCR 异常 | 提示用户重新校准区域坐标 |
| **DPI/分辨率变化** | 窗口尺寸与预期不符 | 自动重新计算比例坐标 |

### 9.2 弹窗处理

千牛常见弹窗（授权确认、系统提示等）会遮挡聊天区域。通过 OCR 识别弹窗特征文本自动处理：

```cpp
class PopupHandler {
    struct PopupPattern {
        QStringList keywords;      // 特征关键词
        QPoint closeButtonOffset;  // 关闭按钮相对于弹窗中心的偏移
        QString action;            // "close" | "confirm" | "ignore"
    };
    
    QVector<PopupPattern> knownPopups = {
        {{"确定授权", "用户授权协议"}, {200, 150}, "confirm"},
        {{"系统提示", "确定"}, {0, 80}, "confirm"},
        {{"版本更新", "稍后再说"}, {-80, 100}, "close"},
    };

public:
    bool detectAndHandle(HWND hwnd, const QImage& fullScreenshot) {
        OCRResult result = ocrEngine->recognize(fullScreenshot);
        
        for (const auto& popup : knownPopups) {
            bool allMatch = true;
            for (const auto& kw : popup.keywords) {
                bool found = false;
                for (const auto& block : result.blocks) {
                    if (block.text.contains(kw)) {
                        found = true;
                        break;
                    }
                }
                if (!found) { allMatch = false; break; }
            }
            
            if (allMatch) {
                spdlog::info("检测到弹窗: {}", 
                             popup.keywords.first().toStdString());
                
                // 计算弹窗中心
                QPoint center = fullScreenshot.rect().center();
                POINT screenPt = clientToScreen(hwnd,
                    center.x() + popup.closeButtonOffset.x(),
                    center.y() + popup.closeButtonOffset.y());
                
                if (popup.action != "ignore") {
                    InputSimulator::simulateClick(screenPt.x, screenPt.y);
                    QThread::msleep(500);
                }
                return true;
            }
        }
        return false;
    }
};
```

### 9.3 健康检查与诊断

```cpp
class RPAHealthChecker {
    int consecutiveFailures = 0;
    int maxConsecutiveFailures = 10;
    QString diagnosticDir;

public:
    enum HealthStatus { Healthy, Degraded, Critical };
    
    HealthStatus check(HWND hwnd, const QImage& lastCapture,
                        const OCRResult& lastOCR) {
        // 检查窗口是否存在
        if (!IsWindow(hwnd)) return Critical;
        
        // 检查截图是否有效（非全黑/全白）
        if (isBlankImage(lastCapture)) {
            consecutiveFailures++;
            saveDiagnostic("blank_capture", lastCapture);
        }
        // 检查 OCR 是否有输出
        else if (lastOCR.blocks.isEmpty()) {
            consecutiveFailures++;
            saveDiagnostic("empty_ocr", lastCapture);
        } else {
            consecutiveFailures = 0;
        }
        
        if (consecutiveFailures >= maxConsecutiveFailures)
            return Critical;
        if (consecutiveFailures >= 3)
            return Degraded;
        return Healthy;
    }
    
    void saveDiagnostic(const QString& reason, const QImage& image) {
        QString filename = QString("%1/diag_%2_%3.png")
            .arg(diagnosticDir)
            .arg(reason)
            .arg(QDateTime::currentDateTime()
                     .toString("yyyyMMdd_HHmmss"));
        image.save(filename);
        spdlog::warn("RPA 诊断截图已保存: {}", filename.toStdString());
    }
    
private:
    bool isBlankImage(const QImage& img) {
        // 检查图片是否全黑或全白
        int sample = 100;
        int sameCount = 0;
        QRgb firstPixel = img.pixel(0, 0);
        for (int i = 0; i < sample; i++) {
            int x = qrand() % img.width();
            int y = qrand() % img.height();
            if (img.pixel(x, y) == firstPixel) sameCount++;
        }
        return sameCount > sample * 0.95;
    }
};
```

---

## 十、与聚合界面的数据对接

### 10.1 消息流转路径

```
[千牛 PC 客户端]
       │
       │  (屏幕像素)
       ▼
[QianniuRPAAdapter - 桌面端]
  │  OCR 截图 → 文字识别 → 消息解析 → 增量检测
  │
  │  emit incomingMessage(PlatformMessage)
  ▼
[MessageRouter - 桌面端]
  │  HTTP POST /api/messages
  ▼
[服务端]
  │  写入 conversations / messages 表
  │  WebSocket 推送 new_message 事件
  ▼
[桌面客户端 - AggregateChatWindow]
  │  收到 WebSocket 推送
  │  更新会话列表 + 消息气泡
  ▼
[客服看到新消息]
```

**回复路径**：

```
[客服在聚合界面输入回复，点击"发送"]
       │
       │  HTTP POST /api/messages/send
       ▼
[服务端]
  │  判断 platform=taobao, mode=rpa
  │  WebSocket 指令: {type: "rpa_send", conversationId, text}
  ▼
[桌面客户端 - QianniuRPAAdapter]
  │  收到发送指令
  │  执行 sendMessage() → 切换会话 → 粘贴 → 发送
  │  返回发送结果
  ▼
[服务端]
  │  写入 messages 表 (direction=out)
  │  WebSocket 推送 message_sent 事件
  ▼
[聚合界面显示"已发送"]
```

### 10.2 PlatformMessage 映射

RPA 采集到的消息需要映射到统一的 `PlatformMessage` 结构：

```cpp
PlatformMessage QianniuRPAAdapter::toPlatformMessage(
    const ParsedMessage& parsed) {
    
    PlatformMessage msg;
    msg.platform = "taobao";
    msg.platformConversationId = 
        QString("qn_%1_%2").arg(windowInfo.accountId, currentSessionName);
    msg.customerName = currentSessionName;
    msg.content = parsed.content;
    msg.direction = (parsed.direction == ParsedMessage::Incoming) 
                    ? "in" : "out";
    msg.sender = (parsed.direction == ParsedMessage::Incoming) 
                 ? "customer" : "agent";
    msg.createdAt = parseTimestamp(parsed.timestamp);
    msg.rawPayload = QJsonObject{
        {"source", "rpa_ocr"},
        {"ocrConfidence", 0.9},
        {"ocrEngine", ocrEngine->engineName()}
    };
    
    return msg;
}
```

### 10.3 RPA 状态上报

RPA 适配器需要向聚合界面上报自身的运行状态，让客服了解当前采集是否正常：

```cpp
struct RPAStatus {
    QString platform;
    bool isRunning;
    WindowState windowState;
    int ocrSuccessRate;     // 最近 N 次的成功率百分比
    int avgOCRLatencyMs;    // 平均 OCR 耗时
    int messagesCaptured;   // 本次运行累计采集消息数
    int lastErrorCode;      // 最近一次错误代码
    QString lastError;      // 最近一次错误描述
};
```

聚合界面在平台名旁边显示 RPA 状态指示灯：
- 🟢 绿色：正常运行
- 🟡 黄色：降级（窗口最小化/遮挡）
- 🔴 红色：异常（窗口丢失/连续失败）
- ⚪ 灰色：未启动

---

## 十一、线程模型与并发控制

### 11.1 线程分布

```
┌──────────────────────────────────────────────────┐
│                   桌面客户端进程                    │
│                                                    │
│  [UI 主线程]                                       │
│    · Qt 事件循环                                   │
│    · 聚合界面更新                                   │
│    · 窗口嵌入管理                                   │
│                                                    │
│  [RPA 工作线程] (QThread)                          │
│    · OCR 截图轮询                                  │
│    · 消息解析与增量检测                              │
│    · 通过信号传递 PlatformMessage 到主线程           │
│                                                    │
│  [RPA 发送线程] (QThread / 按需创建)                │
│    · 执行 sendMessage 操作                         │
│    · 模拟点击、键盘输入（需要操作窗口）               │
│    · 互斥锁保护，同一时间只执行一个发送               │
│                                                    │
│  [WebSocket 客户端线程]                             │
│    · 与服务端通信                                   │
│    · 接收推送、发送请求                              │
│                                                    │
│  [新消息检测线程]                                    │
│    · 轻量级：窗口标题监控、像素条带变化检测           │
│    · 触发 RPA 工作线程加速扫描                       │
└──────────────────────────────────────────────────┘
```

### 11.2 线程安全注意事项

| 问题 | 解决方案 |
|------|----------|
| OCR 轮询与发送操作可能同时操作千牛窗口 | 发送时暂停 OCR 轮询（通过标志位），发送完毕后恢复 |
| 剪贴板是全局资源 | `ClipboardGuard` RAII 保存/恢复，发送操作持有互斥锁 |
| `SendInput` 是全局输入 | 发送操作期间暂停所有自动鼠标/键盘模拟 |
| 多个 RPA 适配器（未来扩展拼多多等） | 全局输入模拟器加锁，同一时间只有一个适配器在操作 |
| 信号跨线程 | 使用 `Qt::QueuedConnection`（Qt 默认跨线程自动切换） |

---

## 十二、配置与可调参数

### 12.1 完整配置项

```toml
[platform.taobao]
mode = "rpa"                      # "rpa" | "api" | "embed_only"
enabled = true

[platform.taobao.rpa]
# OCR 引擎
ocr_engine = "windows_ocr"        # "windows_ocr" | "paddleocr" | "tesseract"
paddleocr_model_path = "./models/paddleocr"

# 轮询参数
poll_interval_min_ms = 300         # 最小轮询间隔
poll_interval_max_ms = 3000        # 最大轮询间隔（无变化时退避）
poll_interval_base_ms = 500        # 基础间隔

# 新消息检测
title_monitor_interval_ms = 200    # 窗口标题检查间隔
pixel_change_interval_ms = 300     # 像素变化检测间隔
pixel_change_threshold = 0.05      # 像素变化阈值 (0~1)
full_scan_interval_ms = 3000       # 兜底全量扫描间隔

# 发送参数
send_retry_count = 1               # 发送失败重试次数
send_verify_delay_ms = 500         # 发送后验证等待时间
clipboard_restore = true           # 是否恢复剪贴板内容

# 会话管理
session_scan_interval = 10         # 每 N 次消息扫描做 1 次列表扫描
max_session_switch_per_minute = 10 # 每分钟最大会话切换次数
pause_on_user_active = true        # 用户操作千牛时暂停自动切换

# 健康检查
max_consecutive_failures = 10      # 连续失败多少次标记为 Critical
diagnostic_save_path = "./logs/rpa_diag"

# 区域坐标（初始值，可通过校准工具覆盖）
[platform.taobao.rpa.regions]
session_list = { left = 0.0, top = 0.12, right = 0.30, bottom = 1.0 }
message_area = { left = 0.30, top = 0.10, right = 0.80, bottom = 0.72 }
input_area = { left = 0.30, top = 0.78, right = 0.80, bottom = 0.92 }
send_button = { cx = 0.73, cy = 0.96 }
```

---

## 十三、开发计划

### 13.1 分阶段交付

| 阶段 | 目标 | 核心交付物 | 预估工期 | 依赖 |
|------|------|-----------|---------|------|
| **P0: 基础设施** | 截图 + OCR 能力验证 | `ScreenCapture` + `WindowsOCREngine` + 命令行测试工具 | 3-4 天 | 无 |
| **P1: 消息采集** | 单会话消息 OCR 采集 | `MessageParser` + `IncrementalDetector` + 简单轮询 | 4-5 天 | P0 |
| **P2: 回复发送** | 剪贴板粘贴发送 | `InputSimulator` + `sendMessage` + 发送验证 | 3-4 天 | P0 |
| **P3: 会话管理** | 多会话识别与切换 | `SessionListParser` + 自动切换逻辑 | 3-4 天 | P1 |
| **P4: 系统集成** | 接入 `IPlatformAdapter` + 聚合界面 | `QianniuRPAAdapter` 完整实现 + 服务端对接 | 3-4 天 | P1+P2+P3 |
| **P5: 健壮性** | 异常处理 + 健康检查 + 弹窗处理 | `PopupHandler` + `RPAHealthChecker` + 诊断日志 | 3-4 天 | P4 |
| **P6: 体验优化** | 区域校准工具 + 自适应轮询 + 配置管理 | `RegionCalibrator` + 配置 UI | 2-3 天 | P4 |

**总预估**：21-28 个工作日（约 4-6 周）

### 13.2 建议开发顺序

```
模拟平台跑通完整链路（优先，与 RPA 并行）
       ↓
P0: 截图 + OCR 验证 ──→ 确认技术可行性
       ↓
P1: 单会话消息采集 ──→ 验证 OCR 精度、增量检测
       ↓
P2: 回复发送 ──→ 验证模拟输入可靠性
       ↓
P3: 多会话管理 ──→ 支持多个买家会话
       ↓
P4: 系统集成 ──→ 与聚合界面端到端打通
       ↓
P5+P6: 健壮性 + 体验 ──→ 生产级可用
```

### 13.3 P0 阶段快速验证清单

在投入完整开发前，先用 P0 阶段（3-4 天）快速验证关键技术假设：

| # | 验证项 | 预期结果 | 不通过的应对 |
|---|--------|---------|-------------|
| 1 | `PrintWindow` 对千牛接待中心是否有效 | 能截到完整内容 | 降级为 BitBlt（要求窗口可见） |
| 2 | Windows OCR 对千牛聊天文字的识别率 | >95% 准确率 | 切换到 PaddleOCR |
| 3 | OCR 单帧处理延迟 | <200ms | 降低截图分辨率或裁剪更小区域 |
| 4 | 模拟点击能否操作千牛嵌入窗口 | 可以切换会话 | 尝试 `PostMessage` / `SendMessage` 替代 |
| 5 | 剪贴板粘贴到千牛输入框是否有效 | 中文文本正确粘贴 | 尝试 `SendInput` + `KEYEVENTF_UNICODE` |

---

## 十四、未来扩展

### 14.1 多平台 RPA 复用

当前方案虽然针对千牛设计，但核心组件（`ScreenCapture`、`IOCREngine`、`InputSimulator`、`IncrementalDetector`）均为**平台无关**的通用模块。扩展到拼多多、京东等平台时，只需：

1. 新增 `PinduoduoRPAAdapter`，实现同一 `IPlatformAdapter` 接口
2. 提供该平台的 `RegionConfig`（通过校准工具标注）
3. 提供该平台的 `MessageParser`（适配不同的消息格式）

### 14.2 API 模式切换

千牛 IM API 恢复后，可在配置中将 `mode` 从 `"rpa"` 切换为 `"api"`：

```toml
[platform.taobao]
mode = "api"  # 切换到 API 模式
```

`PlatformManager` 会创建 `QianniuIMAdapter` 替代 `QianniuRPAAdapter`，上层完全无感知。RPA 代码保留，可随时切回。

### 14.3 AI 辅助 OCR 纠错

后续可引入 LLM 对 OCR 结果进行纠错和补全：
- OCR 原始输出 → LLM 上下文纠错 → 结构化消息
- 特别适用于表情符号、特殊字符、模糊文字的修正

---

## 附录

### A. 技术栈依赖清单

| 依赖 | 用途 | 引入方式 |
|------|------|----------|
| **Windows SDK** (UIAutomation.h, winrt/) | 截图、窗口操作、Windows OCR | 系统自带 |
| **Qt 6** (Core, Widgets, Network, WebSockets) | UI、线程、信号槽、网络通信 | 已有 |
| **spdlog** | 日志 | 已有 (vcpkg) |
| **toml++** | 配置文件解析 | vcpkg: `tomlplusplus` |
| **PaddleOCR** (可选) | 高精度中文 OCR | ONNX Runtime + 模型文件 |

### B. 关键 Win32 API 速查

| API | 用途 |
|-----|------|
| `EnumWindows` | 枚举顶层窗口 |
| `GetWindowText` | 获取窗口标题 |
| `GetClientRect` | 获取窗口客户区大小 |
| `ClientToScreen` | 客户区坐标 → 屏幕坐标 |
| `PrintWindow` | 截取窗口内容到 DC |
| `BitBlt` | 位块传输（屏幕截图） |
| `SetCursorPos` | 设置鼠标位置 |
| `SendInput` | 模拟键盘/鼠标输入 |
| `OpenClipboard` / `SetClipboardData` | 操作剪贴板 |
| `IsWindow` / `IsIconic` | 检查窗口状态 |
| `GetForegroundWindow` | 获取前台窗口 |

### C. 参考资源

- [Windows UI Automation 官方文档](https://learn.microsoft.com/en-us/windows/win32/winauto/entry-uiauto-win32)
- [PaddleOCR C++ 部署指南](https://github.com/PaddlePaddle/PaddleOCR/tree/main/deploy/cpp_infer)
- [Windows OCR WinRT API](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/optical-character-recognition)
- [Inspect.exe 使用指南](https://learn.microsoft.com/en-us/windows/win32/winauto/inspect-objects)
