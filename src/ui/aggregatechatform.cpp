#include "aggregatechatform.h"
#include "foldarrowcombobox.h"
#include "../core/conversationmanager.h"
#include <QButtonGroup>
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../data/messagesendeventdao.h"
#include "../services/app/aichatappservice.h"
#include "../services/app/conversationappservice.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../services/ai/aistreamingsession.h"
#include "../services/platforms/simplatformadapter.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../utils/appsettings.h"
#include "../utils/applystyle.h"
#include "../utils/svgresourcepixmap.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QAction>
#include <QAbstractButton>
#include <QMenu>
#include <QToolButton>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QSvgRenderer>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QSizePolicy>
#include <QColor>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QEasingCurve>

namespace {

/** 右栏「展示模型」图标区，与布局中 model 块一致。 */
constexpr int kAggregateRightBarModelIconBoxSide = 58;
constexpr int kAggregateRightBarModelIconDrawSide = 56;

void applySolidBackground(QWidget* widget, const QColor& color)
{
    if (!widget)
        return;
    QPalette pal = widget->palette();
    pal.setColor(QPalette::Window, color);
    pal.setColor(QPalette::Base, color);
    widget->setPalette(pal);
    widget->setAutoFillBackground(true);
}

QString phaseDisplayName(const QString& phase)
{
    if (phase == QLatin1String("dequeued"))
        return QStringLiteral("已取出待发");
    if (phase == QLatin1String("lock_acquired"))
        return QStringLiteral("已获得窗口锁");
    if (phase == QLatin1String("lock_timeout"))
        return QStringLiteral("窗口锁超时");
    if (phase == QLatin1String("switch_chat"))
        return QStringLiteral("切换会话");
    if (phase == QLatin1String("send_text"))
        return QStringLiteral("输入并发送");
    if (phase == QLatin1String("receipt_check"))
        return QStringLiteral("回执校验");
    if (phase == QLatin1String("receipt_result"))
        return QStringLiteral("回执结果");
    if (phase == QLatin1String("send_attempt"))
        return QStringLiteral("发送尝试");
    if (phase == QLatin1String("success"))
        return QStringLiteral("成功");
    if (phase == QLatin1String("failed"))
        return QStringLiteral("失败");
    return phase;
}

QString formatSendEventLine(const MessageSendEventRecord& e)
{
    const QString t = e.createdAt.isValid()
                          ? e.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                          : QStringLiteral("-");
    QString detail = e.detail.trimmed();
    if (detail.size() > 200)
        detail = detail.left(197) + QStringLiteral("...");
    return QStringLiteral("[%1] %2 消息#%3 %4")
        .arg(t, phaseDisplayName(e.phase))
        .arg(e.messageId)
        .arg(detail.isEmpty() ? QStringLiteral("-") : detail);
}

/** 右栏性能指标卡：高度随列宽变化，与宽度一致，接近正方形。 */
class AggregateRightMetricCardFrame final : public QFrame
{
public:
    explicit AggregateRightMetricCardFrame(QWidget* parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("aggregateRightBarMetricCard"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setAttribute(Qt::WA_StyledBackground, true);
        auto* sh = new QGraphicsDropShadowEffect(this);
        sh->setBlurRadius(18);
        sh->setOffset(0, 2);
        sh->setColor(QColor(15, 23, 42, 36));
        setGraphicsEffect(sh);
    }
protected:
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        return qMax(1, w);
    }
    QSize minimumSizeHint() const override
    {
        if (const QLayout* lay = layout()) {
            const QSize s = lay->minimumSize();
            const int side = qMax(76, qMax(s.width(), s.height()));
            return { side, side };
        }
        return { 80, 80 };
    }
};

/** 与父级 AggregateChatForm 样式隔离，避免 QLabel 继承深色字压在系统深色弹窗背景上。 */
QString aggregateMessageBoxContrastStyle()
{
    return ApplyStyle::messageBoxContrastStyle();
}

/** 与主窗口侧栏一致的圆角栅格化；cornerRadiusLogical 取边长一半即为圆形。 */
static QPixmap roundedAggregateAvatarPixmap(const QPixmap& source, int logicalSide, qreal dpr,
                                            int cornerRadiusLogical)
{
    if (source.isNull())
        return source;
    const int s = qMax(1, qRound(logicalSide * dpr));
    const int r = qBound(1, qRound(cornerRadiusLogical * dpr), s / 2);
    QPixmap square(s, s);
    square.fill(Qt::transparent);
    {
        QPainter pt(&square);
        pt.setRenderHint(QPainter::Antialiasing);
        QPixmap fill = source.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = qMax(0, (fill.width() - s) / 2);
        const int oy = qMax(0, (fill.height() - s) / 2);
        pt.drawPixmap(0, 0, fill, ox, oy, s, s);
    }
    QPixmap out(s, s);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, s, s, qreal(r), qreal(r));
    p.setClipPath(path);
    p.drawPixmap(0, 0, square);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}

/** 与 `m_modeCombo` 项顺序一致：0 人工接待，1 AI 辅助，2 AI 自动回复（全自动发送能力待实现）。 */
constexpr int kAggregateModeIndexAiAssist = 1;
constexpr int kAggregateModeIndexAutoReply = 2;

QString aggregateAiMvpSystemPrompt()
{
    return QStringLiteral(
        "你是电商客服场景的辅助起草助手。请根据用户给出的「客户最后一条入站消息」（可能附带聊天区截图）和下列店铺知识，起草一条可直接发送给客户的回复正文。\n"
        "要求：语气专业、友好；不要编造未在知识中出现的承诺；不要加「客服：」等前缀或引号；若有截图，请结合画面理解客户意图。\n"
        "篇幅：默认简短。订单、物流、商品规格等常见询问用几句话说明要点即可（约 80～200 字量级），不要长篇铺垫、不要展开无关背景；仅在客户问题本身很复杂或明确要求详细说明时再适当增加。\n\n"
        "【店铺知识·MVP 占位，可随版本替换】\n"
        "礼貌问候；明确订单、物流、售后的查询途径；避免无法兑现的承诺。具体规则以公司内部文档为准。");
}

static QString aggregateModelMenuLabel(const QString& sessionModelKey)
{
    return aiPresetLabel(sessionModelKey);
}

void showAggregateInfo(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(aggregateMessageBoxContrastStyle());
    box.exec();
}

void showAggregateWarning(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(aggregateMessageBoxContrastStyle());
    box.exec();
}

bool handleAggregateBuildFailure(QWidget* parent,
                                 const AggregateAiBuiltRequest& built,
                                 bool silent,
                                 QString* silentReason)
{
    if (silentReason)
        *silentReason = built.failureDetail;
    if (silent || built.ok())
        return built.ok();

    switch (built.failure) {
    case AggregateAiBuildFailure::MissingApiKey:
        showAggregateWarning(parent, QStringLiteral("API Key"),
                             QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中为该线路（豆包或 "
                                            "DeepSeek）填写并保存 API Key；"
                                            "亦可在 QSettings 的 aggregateAi/apiKey / ai/apiKey 中配置通用 Key。"));
        break;
    case AggregateAiBuildFailure::IncompleteModelConfig:
        showAggregateWarning(parent, QStringLiteral("模型配置"),
                             QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中补齐并保存 "
                                            "Base URL 与模型名称。"));
        break;
    case AggregateAiBuildFailure::MissingInboundSnapshot:
    case AggregateAiBuildFailure::EmptyInbound:
        showAggregateInfo(parent, QStringLiteral("AI 辅助"),
                          QStringLiteral("暂无可辅助的客户入站消息。请先收到客户消息后再试。"));
        break;
    case AggregateAiBuildFailure::MissingInboundImage:
        showAggregateWarning(parent, QStringLiteral("聊天区截图"),
                             QStringLiteral("本条入站关联的聊天区截图文件不存在或无法读取，可能已被清理。请重新收一条消息或检查路径。"));
        break;
    case AggregateAiBuildFailure::VisionUnsupported:
        showAggregateWarning(
            parent,
            QStringLiteral("多模态模型"),
            QStringLiteral("当前入站含聊天区截图，需要支持图片的模型。请点击下方「模型：…」切换到「豆包」，"
                           "并在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中完成该线路的 "
                           "Base URL、接入点 ID 与 API Key 并保存。"));
        break;
    case AggregateAiBuildFailure::None:
        break;
    }
    return false;
}

} // namespace

AggregateChatForm::AggregateChatForm(const QString& loginUsername, QWidget* parent)
    : QWidget(parent)
    , m_loginUsername(loginUsername)
{
    setupUI();
    m_conversationService = new ConversationAppService();
    m_aiChatService = new AiChatAppService(this);
    setupStyles();
    loadSelfBubbleIdentity();
    connectSignals();
    refreshConversationList();
    QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
    if (m_modeCombo)
        m_lastAggregateModeIndex = m_modeCombo->currentIndex();
}

void AggregateChatForm::loadSelfBubbleIdentity()
{
    const LocalUserProfile profile = m_conversationService
                                         ? m_conversationService->loadLocalUserProfile(m_loginUsername)
                                         : LocalUserProfile{m_loginUsername, m_loginUsername, QString()};
    m_selfDisplayName = profile.displayName;

    constexpr int kAvatarLogical = 36;
    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;

    QPixmap pm;
    if (!profile.avatarAbsolutePath.isEmpty()) {
        if (QFile::exists(profile.avatarAbsolutePath)) {
            const QImage img(profile.avatarAbsolutePath);
            if (!img.isNull()) {
                pm = QPixmap::fromImage(
                    img.scaled(QSize(kAvatarLogical, kAvatarLogical) * dpr, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation));
                pm.setDevicePixelRatio(dpr);
            }
        }
    }
    if (pm.isNull()) {
        QPixmap canvas(QSize(kAvatarLogical, kAvatarLogical) * dpr);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
        QPainter painter(&canvas);
        renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
        pm = canvas;
    }
    m_selfAvatarPixmap = roundedAggregateAvatarPixmap(pm, kAvatarLogical, dpr, kAvatarLogical / 2);

    {
        const QString customerRes = QStringLiteral(
            ":/aggregate_reception_icons/customer_default_avatar.png");
        QPixmap customerPm;
        QImage cimg(customerRes);
        if (!cimg.isNull()) {
            customerPm = QPixmap::fromImage(
                cimg.scaled(QSize(kAvatarLogical, kAvatarLogical) * dpr, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation));
            customerPm.setDevicePixelRatio(dpr);
        }
        if (customerPm.isNull()) {
            QPixmap canvas(QSize(kAvatarLogical, kAvatarLogical) * dpr);
            canvas.setDevicePixelRatio(dpr);
            canvas.fill(Qt::transparent);
            QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
            QPainter painter(&canvas);
            renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
            customerPm = canvas;
        }
        m_customerDefaultAvatarPixmap =
            roundedAggregateAvatarPixmap(customerPm, kAvatarLogical, dpr, kAvatarLogical / 2);
    }
}

void AggregateChatForm::refreshLocalUserProfile()
{
    loadSelfBubbleIdentity();
    if (m_currentConvId <= 0)
        return;
    const auto messages = ConversationManager::instance().messages(m_currentConvId);
    m_currentMessageSignature = buildMessageSignature(messages);
    renderConversationMessages(messages);
    scheduleScrollChatToBottom();
}

AggregateChatForm::~AggregateChatForm()
{
    delete m_conversationService;
}

void AggregateChatForm::setupUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* body = new QWidget(this);
    auto* bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_leftToolBar = buildLeftToolBar();
    bodyLay->addWidget(m_leftToolBar, 0);

    m_hSplitter = new QSplitter(Qt::Horizontal, body);
    m_hSplitter->addWidget(buildLeftPanel());
    m_hSplitter->addWidget(buildCenterPanel());
    m_hSplitter->addWidget(buildRightPanel());
    m_hSplitter->setSizes({248, 576, 296});
    m_hSplitter->setStretchFactor(0, 0);
    m_hSplitter->setStretchFactor(1, 1);
    m_hSplitter->setStretchFactor(2, 0);
    m_hSplitter->setCollapsible(0, false);
    m_hSplitter->setCollapsible(1, false);
    m_hSplitter->setCollapsible(2, true);
    m_hSplitter->setHandleWidth(1);
    bodyLay->addWidget(m_hSplitter, 1);
    setupRightBarToggleButton();

    outerLayout->addWidget(body, 1);

    m_messageRefreshTimer = new QTimer(this);
    m_messageRefreshTimer->setInterval(500);
    m_sendTimelineTimer = new QTimer(this);
    m_sendTimelineTimer->setInterval(900);
}

void AggregateChatForm::setupStyles()
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
    syncSolidBackgrounds();
}

void AggregateChatForm::applyTheme(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
    syncSolidBackgrounds();
    updateAggregateAiControlsVisibility();
    QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
}

void AggregateChatForm::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
    QTimer::singleShot(0, this, &AggregateChatForm::updateRightBarToggleButtonGeometry);
}

bool AggregateChatForm::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_chatInputOverlayHost
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        relayoutChatInputOverlay();
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        updateRightBarToggleButtonGeometry();
    if (event->type() == QEvent::MouseMove) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        updateRightBarToggleButtonVisibility(mouseEvent->globalPosition().toPoint());
    } else if (event->type() == QEvent::Leave) {
        updateRightBarToggleButtonVisibility(QCursor::pos());
    }
    return QWidget::eventFilter(watched, event);
}

void AggregateChatForm::installRightBarToggleEventFilter(QWidget* widget)
{
    if (!widget)
        return;
    widget->setMouseTracking(true);
    widget->installEventFilter(this);
}

void AggregateChatForm::setupRightBarToggleButton()
{
    if (m_rightBarToggleButton)
        return;

    m_rightBarToggleButton = new QToolButton(this);
    m_rightBarToggleButton->setObjectName(QStringLiteral("aggregateRightBarToggleButton"));
    m_rightBarToggleButton->setCursor(Qt::PointingHandCursor);
    m_rightBarToggleButton->setAutoRaise(false);
    m_rightBarToggleButton->setFixedSize(24, 54);
    m_rightBarToggleButton->hide();
    installRightBarToggleEventFilter(m_rightBarToggleButton);
    connect(m_rightBarToggleButton, &QToolButton::clicked, this, [this]() {
        setRightBarHidden(!m_rightBarHidden);
    });

    installRightBarToggleEventFilter(this);
    installRightBarToggleEventFilter(m_hSplitter);
    installRightBarToggleEventFilter(m_centerPanel);
    installRightBarToggleEventFilter(m_centerStack);
    installRightBarToggleEventFilter(m_centerEmptyState);
    installRightBarToggleEventFilter(m_chatArea);
    installRightBarToggleEventFilter(m_chatHeader);
    installRightBarToggleEventFilter(m_chatInputOverlayHost);
    installRightBarToggleEventFilter(m_messageScroll);
    if (m_messageScroll)
        installRightBarToggleEventFilter(m_messageScroll->viewport());
    installRightBarToggleEventFilter(m_rightPanel);
    if (m_hSplitter) {
        connect(m_hSplitter, &QSplitter::splitterMoved, this, [this]() {
            if (!m_rightBarHidden) {
                const QList<int> sizes = m_hSplitter->sizes();
                if (sizes.size() >= 3 && sizes[2] > 0)
                    m_lastRightBarWidth = sizes[2];
            }
            updateRightBarToggleButtonGeometry();
        });
    }
    updateRightBarToggleButtonGeometry();
}

void AggregateChatForm::setRightBarHidden(bool hidden)
{
    if (!m_hSplitter)
        return;
    QList<int> sizes = m_hSplitter->sizes();
    if (sizes.size() < 3)
        return;

    const int leftWidth = qMax(180, sizes[0]);
    const int centerWidth = qMax(240, sizes[1]);
    const int rightWidth = qMax(0, sizes[2]);
    if (hidden) {
        if (rightWidth > 24)
            m_lastRightBarWidth = rightWidth;
        m_rightBarHidden = true;
        m_hSplitter->setSizes({leftWidth, centerWidth + rightWidth, 0});
    } else {
        const int totalWidth = qMax(leftWidth + centerWidth + rightWidth, m_hSplitter->width());
        const int availableForCenterAndRight = qMax(0, totalWidth - leftWidth);
        int restoreRight = qBound(240, m_lastRightBarWidth, qMax(240, availableForCenterAndRight - 320));
        if (availableForCenterAndRight < 560)
            restoreRight = qMax(180, availableForCenterAndRight / 3);
        const int restoreCenter = qMax(240, availableForCenterAndRight - restoreRight);
        m_rightBarHidden = false;
        m_hSplitter->setSizes({leftWidth, restoreCenter, restoreRight});
    }

    if (m_rightBarToggleButton) {
        m_rightBarToggleButton->setText(m_rightBarHidden ? QStringLiteral("‹") : QStringLiteral("›"));
        m_rightBarToggleButton->setToolTip(m_rightBarHidden ? QStringLiteral("显示右栏") : QStringLiteral("隐藏右栏"));
    }
    updateRightBarToggleButtonGeometry();
    updateRightBarToggleButtonVisibility(QCursor::pos());
    QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
}

void AggregateChatForm::updateRightBarToggleButtonGeometry()
{
    if (!m_rightBarToggleButton || !m_centerPanel)
        return;
    const QPoint centerTopLeft = m_centerPanel->mapTo(this, QPoint(0, 0));
    const int boundaryX = centerTopLeft.x() + m_centerPanel->width();
    const int y = centerTopLeft.y() + qMax(0, (m_centerPanel->height() - m_rightBarToggleButton->height()) / 2);
    const int x = boundaryX - m_rightBarToggleButton->width() / 2;
    m_rightBarToggleButton->move(x, y);
    m_rightBarToggleButton->setText(m_rightBarHidden ? QStringLiteral("‹") : QStringLiteral("›"));
    m_rightBarToggleButton->setToolTip(m_rightBarHidden ? QStringLiteral("显示右栏") : QStringLiteral("隐藏右栏"));
    m_rightBarToggleButton->raise();
}

void AggregateChatForm::updateRightBarToggleButtonVisibility(const QPoint& globalPos)
{
    if (!m_rightBarToggleButton || !m_centerPanel || !m_hSplitter)
        return;
    updateRightBarToggleButtonGeometry();

    const QPoint centerTopLeft = m_centerPanel->mapToGlobal(QPoint(0, 0));
    const QRect centerGlobalRect(centerTopLeft, m_centerPanel->size());
    const int boundaryX = centerGlobalRect.right() + 1;
    const bool inVerticalRange = globalPos.y() >= centerGlobalRect.top() && globalPos.y() <= centerGlobalRect.bottom();
    const int activeDistance = m_rightBarHidden ? 32 : 24;
    const bool nearBoundary = inVerticalRange && qAbs(globalPos.x() - boundaryX) <= activeDistance;
    const QRect buttonGlobalRect(m_rightBarToggleButton->mapToGlobal(QPoint(0, 0)),
                                 m_rightBarToggleButton->size());

    const bool shouldShow = nearBoundary || buttonGlobalRect.adjusted(-6, -6, 6, 6).contains(globalPos);
    m_rightBarToggleButton->setVisible(shouldShow);
    if (shouldShow)
        m_rightBarToggleButton->raise();
}

void AggregateChatForm::relayoutChatInputOverlay()
{
    if (!m_chatInputOverlayHost || !m_messageScroll || !m_chatInputPanel)
        return;
    const int w = m_chatInputOverlayHost->width();
    const int h = m_chatInputOverlayHost->height();
    if (w <= 0 || h <= 0)
        return;

    m_chatInputPanel->setFixedWidth(w);
    m_chatInputPanel->adjustSize();
    int ih = m_chatInputPanel->sizeHint().height();
    ih = qBound(72, ih, qMax(72, h - 16));

    m_messageScroll->setGeometry(0, 0, w, h);
    m_chatInputPanel->setGeometry(0, h - ih, w, ih);
    m_chatInputPanel->raise();
    updateMessageListBottomReserve(ih);
    m_chatInputOverlayHost->update();
}

void AggregateChatForm::updateMessageListBottomReserve(int overlayBottomPx)
{
    if (!m_messageLayout)
        return;
    const int extra = qMax(0, overlayBottomPx);
    m_messageLayout->setContentsMargins(16, 12, 16, 12 + extra);
}

void AggregateChatForm::connectSignals()
{
    auto& mgr = ConversationManager::instance();

    connect(&mgr, &ConversationManager::conversationListChanged,
            this, &AggregateChatForm::onConversationListChanged);
    connect(&mgr, &ConversationManager::newMessageReceived,
            this, &AggregateChatForm::onNewMessage);
    connect(&mgr, &ConversationManager::messageSentOk,
            this, &AggregateChatForm::onSentOk);
    connect(&mgr, &ConversationManager::messageSendFailed,
            this, [this](int convId, const QString& reason) {
                Q_UNUSED(convId)
                showStatusMessage(QStringLiteral("发送失败: %1").arg(reason), 5000);
            });

    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, this, &AggregateChatForm::onSendClicked);
    connect(m_messageRefreshTimer, &QTimer::timeout,
            this, &AggregateChatForm::refreshVisibleConversationMessages);
    m_messageRefreshTimer->start();
    connect(m_btnClearSendTimeline, &QToolButton::clicked,
            this, &AggregateChatForm::onClearSendTimeline);
    if (m_btnModelPickerBack)
        connect(m_btnModelPickerBack, &QToolButton::clicked, this, &AggregateChatForm::onModelPickerBackClicked);
    if (m_modelPickerList)
        connect(m_modelPickerList, &QListWidget::itemClicked, this, &AggregateChatForm::onModelPickerListItem);
    connect(m_sendTimelineTimer, &QTimer::timeout,
            this, &AggregateChatForm::pollSendTimeline);
    m_sendTimelineTimer->start();

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AggregateChatForm::onModeComboChanged);
}

// ===================== Left tool bar =====================

QWidget* AggregateChatForm::buildLeftToolBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("aggregateLeftToolBar"));
    bar->setFixedWidth(52);
    bar->setAutoFillBackground(true);
    auto* lay = new QVBoxLayout(bar);
    lay->setContentsMargins(6, 14, 6, 14);
    lay->setSpacing(8);

    m_platformButtonGroup = new QButtonGroup(this);
    m_platformButtonGroup->setExclusive(true);

    const struct {
        const char* icon;
        const char* tip;
    } kItems[] = {
        { ":/aggregate_reception_icons/message_center_selected_icon.svg", "消息中心" },
        { ":/aggregate_reception_icons/qianniu_logo_selected_icon.svg", "千牛" },
        { ":/aggregate_reception_icons/pdd_logo_selected_icon.svg", "拼多多" },
        { ":/aggregate_reception_icons/doudian_logo_selected_icon.svg", "抖店" },
        { ":/aggregate_reception_icons/wechat_logo_selected_icon.svg", "微信" },
    };
    for (int i = 0; i < 5; ++i) {
        auto* btn = new QToolButton(bar);
        btn->setObjectName(QStringLiteral("aggregateToolBarButton"));
        btn->setCheckable(true);
        btn->setIcon(QIcon(QString::fromUtf8(kItems[i].icon)));
        btn->setIconSize(QSize(28, 28));
        btn->setFixedSize(40, 40);
        btn->setAutoRaise(true);
        btn->setToolTip(QString::fromUtf8(kItems[i].tip));
        btn->setCursor(Qt::PointingHandCursor);
        m_platformButtonGroup->addButton(btn, i);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
    }
    if (QAbstractButton* first = m_platformButtonGroup->button(0))
        first->setChecked(true);
    m_platformFilter = AggregatePlatformFilter::All;
    connect(m_platformButtonGroup, &QButtonGroup::idClicked, this,
            &AggregateChatForm::onPlatformFilterButtonIdClicked);
    updatePlatformToolBarButtonIcons();
    lay->addStretch(1);
    return bar;
}

void AggregateChatForm::updatePlatformToolBarButtonIcons()
{
    if (!m_platformButtonGroup)
        return;
    static const char* kIcons[] = {
        ":/aggregate_reception_icons/message_center_selected_icon.svg",
        ":/aggregate_reception_icons/qianniu_logo_selected_icon.svg",
        ":/aggregate_reception_icons/pdd_logo_selected_icon.svg",
        ":/aggregate_reception_icons/doudian_logo_selected_icon.svg",
        ":/aggregate_reception_icons/wechat_logo_selected_icon.svg",
    };
    for (int i = 0; i < 5; ++i) {
        auto* b = qobject_cast<QToolButton*>(m_platformButtonGroup->button(i));
        if (!b)
            continue;
        b->setIcon(QIcon(QString::fromUtf8(kIcons[i])));
    }
}

void AggregateChatForm::onPlatformFilterButtonIdClicked(int id)
{
    m_platformFilter = static_cast<AggregatePlatformFilter>(id);
    updatePlatformToolBarButtonIcons();
    updatePlatformSectionTitle();
    refreshConversationList();
}

void AggregateChatForm::updatePlatformSectionTitle()
{
    if (!m_platformSectionTitle)
        return;
    QString t;
    switch (m_platformFilter) {
    case AggregatePlatformFilter::All:
        t = QStringLiteral("消息中心");
        break;
    case AggregatePlatformFilter::Qianniu:
        t = QStringLiteral("千牛");
        break;
    case AggregatePlatformFilter::Pdd:
        t = QStringLiteral("拼多多");
        break;
    case AggregatePlatformFilter::Doudian:
        t = QStringLiteral("抖店");
        break;
    case AggregatePlatformFilter::Wechat:
        t = QStringLiteral("微信");
        break;
    }
    m_platformSectionTitle->setText(t);
}

// ===================== Left Panel =====================

QWidget* AggregateChatForm::buildLeftPanel()
{
    auto* panel = new QWidget(this);
    m_leftPanel = panel;
    panel->setObjectName("aggregateLeftPanel");
    panel->setAutoFillBackground(true);
    panel->setMinimumWidth(236);
    panel->setMaximumWidth(320);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_platformSectionTitle = new QLabel(panel);
    m_platformSectionTitle->setObjectName(QStringLiteral("aggregatePlatformSectionTitle"));
    m_platformSectionTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    updatePlatformSectionTitle();
    layout->addWidget(m_platformSectionTitle);

    // Mode row
    auto* modeRow = new QWidget(panel);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(8);
    auto* modeLabel = new QLabel(QStringLiteral("模式："), modeRow);
    modeLabel->setObjectName(QStringLiteral("aggregateModeLabel"));
    m_modeCombo = new FoldArrowComboBox(modeRow);
    m_modeCombo->addItem(QStringLiteral("人工接待"));
    m_modeCombo->addItem(QStringLiteral("AI 辅助"));
    m_modeCombo->addItem(QStringLiteral("AI自动回复"));
    m_modeCombo->setMinimumWidth(90);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch(1);

    m_btnStopAutoReply = new QPushButton(modeRow);
    m_btnStopAutoReply->setObjectName(QStringLiteral("aggregateStopAutoReplyButton"));
    m_btnStopAutoReply->setIcon(QIcon(QStringLiteral(":/stop_auto_reply.svg")));
    m_btnStopAutoReply->setIconSize(QSize(22, 22));
    m_btnStopAutoReply->setFixedSize(34, 34);
    m_btnStopAutoReply->setCursor(Qt::PointingHandCursor);
    m_btnStopAutoReply->setToolTip(
        QStringLiteral("停止自动回复：立即停止 AI 自动回复意图，并切换为「人工接待」。"
                       "再次使用「AI自动回复」需在模式下拉中重新选择并确认风险提示。"));
    m_btnStopAutoReply->setAccessibleName(QStringLiteral("停止自动回复"));
    connect(m_btnStopAutoReply, &QPushButton::clicked, this, &AggregateChatForm::onStopAutoReplyClicked);
    modeLayout->addWidget(m_btnStopAutoReply);
    layout->addWidget(modeRow);

    // Tabs
    auto* tabRow = new QWidget(panel);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(8);
    m_btnAll = new QPushButton(QStringLiteral("全部"), tabRow);
    m_btnAll->setObjectName("aggregateTabAll");
    m_btnAll->setCheckable(true);
    m_btnAll->setChecked(false);
    m_btnAll->setCursor(Qt::PointingHandCursor);
    m_btnPending = new QPushButton(QStringLiteral("待处理"), tabRow);
    m_btnPending->setObjectName("aggregateTabPending");
    m_btnPending->setCheckable(true);
    m_btnPending->setChecked(true);
    m_btnPending->setCursor(Qt::PointingHandCursor);
    m_btnReplied = new QPushButton(QStringLiteral("已回复"), tabRow);
    m_btnReplied->setObjectName("aggregateTabReplied");
    m_btnReplied->setCheckable(true);
    m_btnReplied->setChecked(false);
    m_btnReplied->setCursor(Qt::PointingHandCursor);
    tabLayout->addWidget(m_btnAll);
    tabLayout->addWidget(m_btnPending);
    tabLayout->addWidget(m_btnReplied);
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);
    connect(m_btnAll, &QPushButton::clicked, this, &AggregateChatForm::onTabAllClicked);
    connect(m_btnPending, &QPushButton::clicked, this, &AggregateChatForm::onTabPendingClicked);
    connect(m_btnReplied, &QPushButton::clicked, this, &AggregateChatForm::onTabRepliedClicked);

    // Search
    auto* searchRow = new QWidget(panel);
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(0);
    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setObjectName("aggregateSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名"));
    m_searchEdit->addAction(QIcon(QStringLiteral(":/aggregate_reception_icons/search_icon.svg")),
                            QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { refreshConversationList(); });
    searchLayout->addWidget(m_searchEdit, 1);
    layout->addWidget(searchRow);

    // Conversation list
    m_leftStack = new QStackedWidget(panel);
    m_leftStack->setObjectName(QStringLiteral("aggregateLeftStack"));
    m_conversationList = new QListWidget(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_conversationList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_conversationList, &QListWidget::itemClicked,
            this, &AggregateChatForm::onConversationItemClicked);
    connect(m_conversationList, &QListWidget::customContextMenuRequested,
            this, &AggregateChatForm::onConversationListContextMenu);
    m_leftStack->addWidget(m_conversationList);

    auto* listEmpty = new QWidget(panel);
    listEmpty->setObjectName("aggregateListEmpty");
    auto* listEmptyLayout = new QVBoxLayout(listEmpty);
    listEmptyLayout->setAlignment(Qt::AlignCenter);
    listEmptyLayout->setContentsMargins(12, 0, 12, 0);
    auto* emptyText = new QLabel(QStringLiteral("暂无会话"), listEmpty);
    emptyText->setObjectName("aggregateListEmptyText");
    emptyText->setAlignment(Qt::AlignCenter);
    listEmptyLayout->addWidget(emptyText);
    m_leftStack->addWidget(listEmpty);
    m_leftStack->setCurrentIndex(1);
    layout->addWidget(m_leftStack, 1);

    return panel;
}

// ===================== Center Panel =====================

QWidget* AggregateChatForm::buildCenterPanel()
{
    auto* panel = new QWidget(this);
    m_centerPanel = panel;
    panel->setObjectName("aggregateCenterPanel");
    panel->setAutoFillBackground(true);
    m_centerStack = new QStackedWidget(panel);
    m_centerStack->setObjectName(QStringLiteral("aggregateCenterStack"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_centerStack);

    // Empty state
    m_centerEmptyState = new QWidget(panel);
    m_centerEmptyState->setObjectName(QStringLiteral("aggregateCenterEmptyState"));
    auto* emptyLayout = new QVBoxLayout(m_centerEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(8);
    emptyLayout->setContentsMargins(24, 0, 24, 0);
    auto* mainText = new QLabel(QStringLiteral("选择一个会话开始聊天"), m_centerEmptyState);
    mainText->setObjectName("aggregateEmptyMain");
    mainText->setAlignment(Qt::AlignHCenter);
    auto* subText = new QLabel(
        QStringLiteral("从左侧列表中选择会话开始接待"),
        m_centerEmptyState);
    subText->setObjectName("aggregateEmptySub");
    subText->setAlignment(Qt::AlignHCenter);
    subText->setWordWrap(true);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);
    m_centerStack->addWidget(m_centerEmptyState);

    // Chat area
    m_chatArea = new QWidget(panel);
    m_chatArea->setObjectName("chatArea");
    m_chatArea->setAutoFillBackground(true);
    auto* chatLayout = new QVBoxLayout(m_chatArea);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    // Chat header
    m_chatHeader = new QLabel(m_chatArea);
    m_chatHeader->setObjectName("chatHeader");
    m_chatHeader->setFixedHeight(48);
    m_chatHeader->setAlignment(Qt::AlignVCenter);
    m_chatHeader->setContentsMargins(0, 0, 0, 0);
    chatLayout->addWidget(m_chatHeader);

    m_chatInputOverlayHost = new QWidget(m_chatArea);
    m_chatInputOverlayHost->setObjectName(QStringLiteral("aggregateChatInputOverlayHost"));
    // 须不透明绘制：若 WA_TranslucentBackground + 透明底，分割器改宽时子控件平移会擦不掉旧像素，出现输入框「重影」
    chatLayout->addWidget(m_chatInputOverlayHost, 1);

    // Message scroll：与底部输入条叠放，输入条盖在消息区下缘
    m_messageScroll = new QScrollArea(m_chatInputOverlayHost);
    m_messageScroll->setObjectName("messageScroll");
    m_messageScroll->setFrameShape(QFrame::NoFrame);
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageContainer = new QWidget();
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(16, 12, 16, 12);
    m_messageLayout->setSpacing(8);
    m_messageLayout->addStretch(1);
    m_messageScroll->setWidget(m_messageContainer);
    if (QWidget* vp = m_messageScroll->viewport())
        vp->setObjectName(QStringLiteral("messageScrollViewport"));
    if (QWidget* vp = m_messageScroll->viewport())
        vp->setAutoFillBackground(false);

    m_chatInputPanel = new QWidget(m_chatInputOverlayHost);
    m_chatInputPanel->setObjectName(QStringLiteral("aggregateChatInputPanel"));
    auto* inputOuter = new QVBoxLayout(m_chatInputPanel);
    inputOuter->setContentsMargins(0, 0, 0, 0);
    inputOuter->setSpacing(0);
    // 内层 16 控制输入/按钮与白色块边缘的留白；外层 0 使整块白底与中间栏（分割器）左右对齐
    auto* inputInner = new QWidget(m_chatInputPanel);
    auto* inputLayout = new QVBoxLayout(inputInner);
    inputLayout->setContentsMargins(16, 8, 16, 12);
    inputLayout->setSpacing(8);

    {
        QSettings st = AppSettings::create();
        m_aggregateAiSessionModelKey =
            st.value(QStringLiteral("aggregateAi/sessionModelKey"), QStringLiteral("deepseek:deepseek-chat"))
                .toString();
        if (m_aggregateAiSessionModelKey != QLatin1String("doubao:ark")
            && m_aggregateAiSessionModelKey != QLatin1String("deepseek:deepseek-chat"))
            m_aggregateAiSessionModelKey = QStringLiteral("deepseek:deepseek-chat");
    }

    m_btnAiModelPick = new QToolButton(inputInner);
    m_btnAiModelPick->setObjectName(QStringLiteral("aggregateAiModelButton"));
    m_btnAiModelPick->setCursor(Qt::PointingHandCursor);
    m_btnAiModelPick->setPopupMode(QToolButton::InstantPopup);
    m_btnAiModelPick->setAutoRaise(false);
    m_btnAiModelPick->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_btnAiModelPick->setVisible(false);
    m_aggregateAiModelMenu = new QMenu(m_btnAiModelPick);
    auto* actDoubao = m_aggregateAiModelMenu->addAction(QStringLiteral("豆包"));
    actDoubao->setData(QStringLiteral("doubao:ark"));
    actDoubao->setCheckable(true);
    auto* actDs = m_aggregateAiModelMenu->addAction(QStringLiteral("DeepSeek"));
    actDs->setData(QStringLiteral("deepseek:deepseek-chat"));
    actDs->setCheckable(true);
    m_btnAiModelPick->setMenu(m_aggregateAiModelMenu);
    m_btnAiModelPick->setToolTip(
        QStringLiteral("选择「生成本条回复」使用的线路；与左栏「管理后台」→「AI 客服后台」→"
                       "「API 配置/模型」中同一线路的配置一致。"));
    connect(m_aggregateAiModelMenu, &QMenu::triggered, this,
            &AggregateChatForm::onAggregateAiModelMenuTriggered);
    refreshAggregateAiModelButtonUi();

    auto* modelRow = new QHBoxLayout();
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->addWidget(m_btnAiModelPick);
    modelRow->addStretch(1);
    inputLayout->addLayout(modelRow);

    m_inputEdit = new QPlainTextEdit(inputInner);
    m_inputEdit->setObjectName("messageInput");
    m_inputEdit->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_inputEdit->setMaximumHeight(100);
    inputLayout->addWidget(m_inputEdit);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_btnAiGenerate = new QPushButton(QStringLiteral("生成本条回复"), inputInner);
    m_btnAiGenerate->setObjectName(QStringLiteral("simulateButton"));
    m_btnAiGenerate->setCursor(Qt::PointingHandCursor);
    m_btnAiGenerate->setVisible(false);
    connect(m_btnAiGenerate, &QPushButton::clicked, this, &AggregateChatForm::onGenerateAiDraftClicked);
    m_btnSend = new QPushButton(QStringLiteral("发送"), inputInner);
    m_btnSend->setObjectName("sendButton");
    m_btnSend->setCursor(Qt::PointingHandCursor);
    m_btnSend->setFixedWidth(80);
    connect(m_btnSend, &QPushButton::clicked, this, &AggregateChatForm::onSendClicked);
    btnRow->addWidget(m_btnAiGenerate);
    btnRow->addWidget(m_btnSend);
    inputLayout->addLayout(btnRow);

    m_statusLabel = new QLabel(inputInner);
    m_statusLabel->setObjectName(QStringLiteral("aggregateInlineStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    inputLayout->addWidget(m_statusLabel);

    inputOuter->addWidget(inputInner, 0);

    m_chatInputOverlayHost->installEventFilter(this);
    connect(m_inputEdit->document(), &QTextDocument::contentsChanged, this, [this]() {
        QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
    });

    m_centerStack->addWidget(m_chatArea);
    m_centerStack->setCurrentWidget(m_centerEmptyState);

    return panel;
}

// ===================== Right Panel =====================

QWidget* AggregateChatForm::buildRightPanel()
{
    auto* panel = new QWidget(this);
    m_rightPanel = panel;
    panel->setObjectName("aggregateRightPanel");
    panel->setAutoFillBackground(true);
    panel->setMinimumWidth(208);
    panel->setMaximumWidth(288);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rightStack = new QStackedWidget(panel);
    m_rightStack->setObjectName(QStringLiteral("aggregateRightStack"));
    auto* rightStackHost = new QWidget(panel);
    rightStackHost->setObjectName(QStringLiteral("aggregateRightStackHost"));
    auto* stackGrid = new QGridLayout(rightStackHost);
    stackGrid->setContentsMargins(16, 16, 16, 16);
    stackGrid->setRowStretch(0, 1);
    stackGrid->setColumnStretch(0, 1);
    stackGrid->addWidget(m_rightStack, 0, 0);
    m_modelPickerOverlay = new QWidget(rightStackHost);
    m_modelPickerOverlay->setObjectName(QStringLiteral("aggregateModelPickerOverlay"));
    m_modelPickerOverlay->hide();
    stackGrid->addWidget(m_modelPickerOverlay, 0, 0);
    layout->addWidget(rightStackHost, 1);

    auto* oLay = new QHBoxLayout(m_modelPickerOverlay);
    oLay->setContentsMargins(0, 0, 0, 0);
    oLay->setSpacing(0);
    oLay->addStretch(1);
    m_modelPickerSheet = new QWidget(m_modelPickerOverlay);
    m_modelPickerSheet->setObjectName(QStringLiteral("aggregateModelPickerSheet"));
    m_modelPickerSheet->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_modelPickerSheet->setFixedWidth(0);
    auto* sheetV = new QVBoxLayout(m_modelPickerSheet);
    sheetV->setContentsMargins(12, 8, 12, 8);
    sheetV->setSpacing(8);
    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    m_btnModelPickerBack = new QToolButton(m_modelPickerSheet);
    m_btnModelPickerBack->setObjectName(QStringLiteral("aggregateModelPickerBack"));
    m_btnModelPickerBack->setText(QStringLiteral("返回"));
    m_btnModelPickerBack->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_btnModelPickerBack->setAutoRaise(true);
    m_btnModelPickerBack->setFocusPolicy(Qt::NoFocus);
    m_btnModelPickerBack->setCursor(Qt::PointingHandCursor);
    auto* pickerTitle = new QLabel(QStringLiteral("选择展示模型"), m_modelPickerSheet);
    pickerTitle->setObjectName(QStringLiteral("aggregateModelPickerTitle"));
    topBar->addWidget(m_btnModelPickerBack, 0, Qt::AlignLeft | Qt::AlignVCenter);
    topBar->addWidget(pickerTitle, 1, Qt::AlignCenter);
    sheetV->addLayout(topBar);
    m_modelPickerList = new QListWidget(m_modelPickerSheet);
    m_modelPickerList->setObjectName(QStringLiteral("aggregateModelPickerList"));
    m_modelPickerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_modelPickerList->setFrameShape(QFrame::NoFrame);
    for (const AiPresetDefinition& def : aiPresetDefinitions()) {
        auto* li = new QListWidgetItem(def.label, m_modelPickerList);
        QString tip = def.label;
        if (!def.defaultModel.isEmpty())
            tip += QLatin1String("\n") + QStringLiteral("默认模型：%1").arg(def.defaultModel);
        if (!def.defaultBaseUrl.isEmpty())
            tip += QLatin1String("\n") + QStringLiteral("Base：%1").arg(def.defaultBaseUrl);
        if (!def.available)
            tip += QStringLiteral("\n（尚未接入，仅作展示）");
        li->setToolTip(tip);
    }
    sheetV->addWidget(m_modelPickerList, 1);
    oLay->addWidget(m_modelPickerSheet, 0, Qt::AlignRight);

    // Customer detail：上模型 / 中性能指标 / 下发送状态（无会话时亦显示，不再单独整页占位）
    m_customerDetail = new QWidget(panel);
    auto* detailLayout = new QVBoxLayout(m_customerDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    detailLayout->setAlignment(Qt::AlignTop);

    m_rightBarScroll = new QScrollArea(m_customerDetail);
    m_rightBarScroll->setObjectName(QStringLiteral("aggregateRightBarScroll"));
    m_rightBarScroll->setFrameShape(QFrame::NoFrame);
    m_rightBarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rightBarScroll->setWidgetResizable(true);
    if (QWidget* v = m_rightBarScroll->viewport()) {
        v->setObjectName(QStringLiteral("aggregateRightBarScrollViewport"));
        v->setAttribute(Qt::WA_StyledBackground, true);
    }
    m_rightBarScrollContent = new QWidget();
    m_rightBarScrollContent->setObjectName(QStringLiteral("aggregateRightBarScrollContent"));
    m_rightBarScrollContent->setAttribute(Qt::WA_StyledBackground, true);
    /* 避免 setWidgetResizable(true) 把内容区拉满视口高度，进而在中间/底部出现大块空白；仅按内容要高度。 */
    m_rightBarScrollContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_rightBarVLayout = new QVBoxLayout(m_rightBarScrollContent);
    m_rightBarVLayout->setContentsMargins(0, 0, 0, 0);
    m_rightBarVLayout->setSpacing(12);
    m_rightBarVLayout->setAlignment(Qt::AlignTop);
    m_rightBarScroll->setWidget(m_rightBarScrollContent);
    detailLayout->addWidget(m_rightBarScroll, 1);

    // —— 上：模型区（图标/标题文字居中，对照浅色参考图） ——
    auto* modelBlock = new QWidget(m_rightBarScrollContent);
    modelBlock->setObjectName(QStringLiteral("aggregateRightBarModelBlock"));
    modelBlock->setAttribute(Qt::WA_StyledBackground, true);
    modelBlock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* modelBlockLay = new QVBoxLayout(modelBlock);
    modelBlockLay->setContentsMargins(0, 0, 0, 0);
    modelBlockLay->setSpacing(8);

    auto* iconRow = new QHBoxLayout();
    iconRow->setContentsMargins(0, 0, 0, 0);
    iconRow->addStretch(1);
    auto* iconBox = new QFrame(modelBlock);
    iconBox->setObjectName(QStringLiteral("aggregateRightBarModelIcon"));
    iconBox->setFixedSize(kAggregateRightBarModelIconBoxSide, kAggregateRightBarModelIconBoxSide);
    auto* iconL = new QVBoxLayout(iconBox);
    iconL->setContentsMargins(0, 0, 0, 0);
    iconL->setSpacing(0);
    m_rightBarModelIconLabel = new QLabel(iconBox);
    m_rightBarModelIconLabel->setAlignment(Qt::AlignCenter);
    m_rightBarModelIconLabel->setFixedSize(kAggregateRightBarModelIconDrawSide,
                                           kAggregateRightBarModelIconDrawSide);
    iconL->addWidget(m_rightBarModelIconLabel, 0, Qt::AlignCenter);
    m_rightBarModelStatusDot = new QWidget(iconBox);
    m_rightBarModelStatusDot->setObjectName(QStringLiteral("aggregateRightBarModelStatusDot"));
    m_rightBarModelStatusDot->setFixedSize(8, 8);
    m_rightBarModelStatusDot->move(kAggregateRightBarModelIconBoxSide - 8 - 4, 4);
    m_rightBarModelStatusDot->raise();
    iconRow->addWidget(iconBox, 0, Qt::AlignHCenter);
    iconRow->addStretch(1);
    modelBlockLay->addLayout(iconRow);

    m_rightBarModelNameLabel = new QLabel(modelBlock);
    m_rightBarModelNameLabel->setObjectName(QStringLiteral("aggregateRightBarModelName"));
    m_rightBarModelNameLabel->setWordWrap(true);
    m_rightBarModelNameLabel->setAlignment(Qt::AlignHCenter);
    modelBlockLay->addWidget(m_rightBarModelNameLabel);
    m_rightBarVLayout->addWidget(modelBlock);

    auto* sep1 = new QFrame(m_rightBarScrollContent);
    sep1->setObjectName(QStringLiteral("aggregateRightBarBlockSep"));
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFixedHeight(1);
    sep1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rightBarVLayout->addWidget(sep1);

    // —— 中：性能指标 2x2 占位 ——
    auto* metricsWrap = new QWidget(m_rightBarScrollContent);
    auto* metLay = new QVBoxLayout(metricsWrap);
    metLay->setContentsMargins(0, 0, 0, 0);
    metLay->setSpacing(6);
    metricsWrap->setObjectName(QStringLiteral("aggregateRightBarMetricsWrap"));
    metricsWrap->setAttribute(Qt::WA_StyledBackground, true);
    metricsWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* metTitle = new QHBoxLayout();
    auto* metTitleL = new QLabel(QStringLiteral("性能指标"), metricsWrap);
    metTitleL->setObjectName(QStringLiteral("aggregateRightBarMetricsTitle"));
    auto* metBadge = new QLabel(QStringLiteral("HEALTHY"), metricsWrap);
    metBadge->setObjectName(QStringLiteral("aggregateRightBarMetricsBadge"));
    metTitle->addWidget(metTitleL);
    metTitle->addStretch(1);
    metTitle->addWidget(metBadge, 0, Qt::AlignRight | Qt::AlignVCenter);
    metLay->addLayout(metTitle);
    const QStringList metricCap = { QStringLiteral("运行时间"), QStringLiteral("请求处理"), QStringLiteral("系统健康"),
                                    QStringLiteral("响应速度") };
    const QString kMetricResPaths[4] = { QStringLiteral(":/aggregate_reception_icons/runtime_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/request_handle_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/system_healthy_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/response_speed_icon.svg") };
    const QString kMetricKeyProp[4] = { QStringLiteral("runtime"), QStringLiteral("request"),
                                        QStringLiteral("system"), QStringLiteral("response") };
    auto* grid2 = new QGridLayout();
    grid2->setContentsMargins(1, 2, 1, 4);
    grid2->setHorizontalSpacing(8);
    grid2->setVerticalSpacing(8);
    grid2->setColumnStretch(0, 1);
    grid2->setColumnStretch(1, 1);
    for (int i = 0; i < 4; ++i) {
        auto* card = new AggregateRightMetricCardFrame(metricsWrap);
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(14, 14, 14, 14);
        cv->setSpacing(6);

        auto* iconWrap = new QFrame(card);
        iconWrap->setObjectName(QStringLiteral("aggregateRightBarMetricIconWrap"));
        iconWrap->setFrameShape(QFrame::NoFrame);
        iconWrap->setAttribute(Qt::WA_StyledBackground, true);
        iconWrap->setFixedSize(40, 40);
        iconWrap->setProperty("metricKey", kMetricKeyProp[i]);
        auto* iconInLay = new QVBoxLayout(iconWrap);
        iconInLay->setContentsMargins(0, 0, 0, 0);
        iconInLay->setSpacing(0);
        iconInLay->addStretch(1);
        auto* icoL = new QLabel(iconWrap);
        icoL->setFixedSize(22, 22);
        icoL->setPixmap(
            QIcon(kMetricResPaths[i]).pixmap(22, 22, QIcon::Normal, QIcon::Off));
        icoL->setObjectName(QStringLiteral("aggregateRightBarMetricIcon"));
        iconInLay->addWidget(icoL, 0, Qt::AlignHCenter);
        iconInLay->addStretch(1);

        m_rightBarMetricValues[i] = new QLabel(QStringLiteral("—"), card);
        m_rightBarMetricValues[i]->setObjectName(QStringLiteral("aggregateRightBarMetricValue"));
        m_rightBarMetricValues[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* capL = new QLabel(metricCap[i], card);
        capL->setObjectName(QStringLiteral("aggregateRightBarMetricCaption"));
        capL->setWordWrap(true);
        cv->addWidget(iconWrap, 0, Qt::AlignLeft);
        cv->addWidget(m_rightBarMetricValues[i]);
        cv->addWidget(capL);
        grid2->addWidget(card, i / 2, i % 2);
    }
    metLay->addLayout(grid2);
    m_btnModelSwitchRow = new QPushButton(metricsWrap);
    m_btnModelSwitchRow->setObjectName(QStringLiteral("aggregateModelSwitchRowButton"));
    m_btnModelSwitchRow->setCursor(Qt::PointingHandCursor);
    m_btnModelSwitchRow->setFocusPolicy(Qt::NoFocus);
    m_btnModelSwitchRow->setContextMenuPolicy(Qt::CustomContextMenu);
    m_btnModelSwitchRow->setToolTip(QStringLiteral("右键切换展示模型"));
    connect(m_btnModelSwitchRow, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_btnModelSwitchRow)
            return;
        const QList<AiPresetDefinition> presets = aiPresetDefinitions();
        if (presets.isEmpty())
            return;

        QMenu menu(m_btnModelSwitchRow);
        for (int i = 0; i < presets.size(); ++i) {
            QAction* action = menu.addAction(presets[i].label);
            action->setCheckable(true);
            action->setChecked(i == m_rightBarDisplayModelIndex);
            action->setData(i);
        }

        QAction* chosen = menu.exec(m_btnModelSwitchRow->mapToGlobal(pos));
        if (!chosen)
            return;
        const int row = chosen->data().toInt();
        if (row < 0 || row >= presets.size() || row == m_rightBarDisplayModelIndex)
            return;
        m_rightBarDisplayModelIndex = row;
        refreshRightBarModelDisplay();
    });
    metLay->addWidget(m_btnModelSwitchRow);
    m_rightBarVLayout->addWidget(metricsWrap);

    auto* sep2 = new QFrame(m_rightBarScrollContent);
    sep2->setObjectName(QStringLiteral("aggregateRightBarBlockSep"));
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFixedHeight(1);
    sep2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rightBarVLayout->addWidget(sep2);

    m_rightBarSendStatusSection = new QWidget(m_rightBarScrollContent);
    m_rightBarSendStatusSection->setObjectName(QStringLiteral("aggregateRightBarSendStatusSection"));
    m_rightBarSendStatusSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* sendStatusOuterLay = new QVBoxLayout(m_rightBarSendStatusSection);
    sendStatusOuterLay->setContentsMargins(0, 0, 0, 0);
    sendStatusOuterLay->setSpacing(3);

    auto* sendTimelineHeaderRow = new QWidget(m_rightBarSendStatusSection);
    sendTimelineHeaderRow->setObjectName(QStringLiteral("aggregateSendTimelineHeaderRow"));
    sendTimelineHeaderRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* sendTimelineHeaderLay = new QHBoxLayout(sendTimelineHeaderRow);
    sendTimelineHeaderLay->setContentsMargins(0, 0, 0, 0);
    sendTimelineHeaderLay->setSpacing(4);

    m_sendTimelineToggle = new QToolButton(sendTimelineHeaderRow);
    m_sendTimelineToggle->setObjectName(QStringLiteral("aggregateSendTimelineToggle"));
    m_sendTimelineToggle->setText(QStringLiteral("查看运行日志"));
    m_sendTimelineToggle->setCheckable(true);
    m_sendTimelineToggle->setChecked(false);
    m_sendTimelineToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sendTimelineToggle->setArrowType(Qt::RightArrow);
    m_sendTimelineToggle->setAutoRaise(true);
    m_sendTimelineToggle->setCursor(Qt::PointingHandCursor);
    m_sendTimelineToggle->setFocusPolicy(Qt::NoFocus);
    m_sendTimelineToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_btnClearSendTimeline = new QToolButton(sendTimelineHeaderRow);
    m_btnClearSendTimeline->setObjectName(QStringLiteral("sendTimelineClearBtn"));
    m_btnClearSendTimeline->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnClearSendTimeline->setAutoRaise(true);
    m_btnClearSendTimeline->setCursor(Qt::PointingHandCursor);
    m_btnClearSendTimeline->setFocusPolicy(Qt::NoFocus);
    m_btnClearSendTimeline->setFixedSize(28, 28);
    m_btnClearSendTimeline->setIconSize(QSize(20, 20));
    m_btnClearSendTimeline->setToolTip(QStringLiteral("清空显示"));
    m_btnClearSendTimeline->setAccessibleName(QStringLiteral("清空显示"));
    {
        const QString n = QStringLiteral(":/aggregate_reception_icons/clear_output_normal_icon.svg");
        const QString s = QStringLiteral(":/aggregate_reception_icons/clear_output_selected_icon.svg");
        QIcon clearOutIcon;
        clearOutIcon.addFile(n, QSize(20, 20), QIcon::Normal, QIcon::Off);
        clearOutIcon.addFile(s, QSize(20, 20), QIcon::Active, QIcon::Off);
        clearOutIcon.addFile(s, QSize(20, 20), QIcon::Selected, QIcon::Off);
        m_btnClearSendTimeline->setIcon(clearOutIcon);
    }
    sendTimelineHeaderLay->addWidget(m_sendTimelineToggle, 1);
    sendTimelineHeaderLay->addWidget(m_btnClearSendTimeline, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_sendTimelineBody = new QWidget(m_rightBarSendStatusSection);
    m_sendTimelineBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* bodyLayout = new QVBoxLayout(m_sendTimelineBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_sendTimeline = new QPlainTextEdit(m_sendTimelineBody);
    m_sendTimeline->setObjectName(QStringLiteral("sendStatusTimeline"));
    m_sendTimeline->setReadOnly(true);
    m_sendTimeline->setMinimumHeight(120);
    bodyLayout->addWidget(m_sendTimeline, 1);

    connect(m_sendTimelineToggle, &QToolButton::toggled, this, [this](bool expanded) {
        if (m_sendTimelineBody)
            m_sendTimelineBody->setVisible(expanded);
        if (m_sendTimelineToggle)
            m_sendTimelineToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        updateRightBarSendSectionStretch();
    });
    m_sendTimelineBody->setVisible(false);

    sendStatusOuterLay->addWidget(sendTimelineHeaderRow, 0);
    sendStatusOuterLay->addWidget(m_sendTimelineBody, 0);
    m_rightBarVLayout->addWidget(m_rightBarSendStatusSection, 0);
    m_rightBarBottomSpacer = new QWidget(m_rightBarScrollContent);
    m_rightBarBottomSpacer->setObjectName(QStringLiteral("aggregateRightBarBottomSpacer"));
    m_rightBarBottomSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightBarVLayout->addWidget(m_rightBarBottomSpacer, 1);
    updateRightBarSendSectionStretch();
    m_rightStack->addWidget(m_customerDetail);

    m_rightStack->setCurrentWidget(m_customerDetail);
    refreshRightBarModelDisplay();
    return panel;
}

// ===================== Conversation Item =====================

QWidget* AggregateChatForm::createConversationItem(const ConversationInfo& conv)
{
    auto* widget = new QWidget();
    widget->setObjectName("convItemWidget");
    widget->setProperty("selected", false);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // Platform dot
    auto* dot = new QLabel(widget);
    dot->setObjectName("convItemDot");
    dot->setFixedSize(10, 10);
    QString dotColor = (conv.platform == QLatin1String("simulator"))
                           ? "#5aaf68" : "#7eb8e8";
    dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor));
    layout->addWidget(dot, 0, Qt::AlignTop | Qt::AlignLeft);

    // Text info
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(3);

    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(conv.customerName, widget);
    nameLabel->setObjectName("convItemName");
    nameRow->addWidget(nameLabel);
    nameRow->addStretch(1);

    QString timeStr;
    if (conv.lastTime.isValid()) {
        if (conv.lastTime.date() == QDate::currentDate())
            timeStr = conv.lastTime.toString("HH:mm");
        else
            timeStr = conv.lastTime.toString("MM-dd HH:mm");
    }
    auto* timeLabel = new QLabel(timeStr, widget);
    timeLabel->setObjectName("convItemTime");
    nameRow->addWidget(timeLabel);
    textCol->addLayout(nameRow);

    auto* msgRow = new QHBoxLayout();
    auto* msgLabel = new QLabel(conv.lastMessage.left(30), widget);
    msgLabel->setObjectName("convItemPreview");
    msgLabel->setMaximumWidth(180);
    msgRow->addWidget(msgLabel);
    msgRow->addStretch(1);

    if (conv.unreadCount > 0) {
        auto* badge = new QLabel(QString::number(conv.unreadCount), widget);
        badge->setObjectName("unreadBadge");
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(20, 20);
        msgRow->addWidget(badge);
    }
    textCol->addLayout(msgRow);

    layout->addLayout(textCol, 1);
    widget->setFocusPolicy(Qt::NoFocus);
    return widget;
}

void AggregateChatForm::syncConversationItemVisualState()
{
    if (!m_conversationList)
        return;
    for (int i = 0; i < m_conversationList->count(); ++i) {
        QListWidgetItem* item = m_conversationList->item(i);
        QWidget* widget = item ? m_conversationList->itemWidget(item) : nullptr;
        if (!widget)
            continue;
        const bool selected = item->isSelected();
        widget->setProperty("selected", selected);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void AggregateChatForm::syncSolidBackgrounds()
{
    const QColor leftBg(QStringLiteral("#EEF1F6"));
    const QColor centerBg(QStringLiteral("#F5F6F7"));
    const QColor rightBg(QStringLiteral("#F7F9FB"));
    const QColor inputBg(QStringLiteral("#FFFFFF"));

    if (m_leftToolBar)
        applySolidBackground(m_leftToolBar, leftBg);
    if (m_hSplitter)
        applySolidBackground(m_hSplitter, leftBg);
    applySolidBackground(m_leftPanel, leftBg);
    applySolidBackground(m_centerPanel, centerBg);
    applySolidBackground(m_rightPanel, rightBg);
    applySolidBackground(m_leftStack, leftBg);
    applySolidBackground(m_centerStack, centerBg);
    applySolidBackground(m_rightStack, rightBg);
    applySolidBackground(m_centerEmptyState, centerBg);
    applySolidBackground(m_chatArea, centerBg);
    applySolidBackground(m_chatInputOverlayHost, centerBg);
    applySolidBackground(m_chatInputPanel, inputBg);
    applySolidBackground(m_messageContainer, centerBg);
    if (m_messageScroll)
        applySolidBackground(m_messageScroll->viewport(), centerBg);
    if (m_conversationList) {
        applySolidBackground(m_conversationList, leftBg);
        applySolidBackground(m_conversationList->viewport(), leftBg);
    }
    applySolidBackground(m_customerDetail, rightBg);
    if (m_rightBarScroll) {
        applySolidBackground(m_rightBarScroll, rightBg);
        applySolidBackground(m_rightBarScroll->viewport(), rightBg);
    }
    if (m_rightBarScrollContent)
        applySolidBackground(m_rightBarScrollContent, rightBg);
    if (m_rightBarSendStatusSection)
        applySolidBackground(m_rightBarSendStatusSection, rightBg);
    if (m_rightBarBottomSpacer)
        applySolidBackground(m_rightBarBottomSpacer, rightBg);
    applySolidBackground(m_modelPickerSheet, rightBg);
    if (m_modelPickerList) {
        applySolidBackground(m_modelPickerList, rightBg);
        applySolidBackground(m_modelPickerList->viewport(), rightBg);
    }
}

void AggregateChatForm::updateRightBarSendSectionStretch()
{
    if (!m_rightBarVLayout || !m_rightBarSendStatusSection || !m_rightBarBottomSpacer)
        return;
    const bool expanded = m_sendTimelineBody && m_sendTimelineBody->isVisible();
    const int iSec = m_rightBarVLayout->indexOf(m_rightBarSendStatusSection);
    const int iSp = m_rightBarVLayout->indexOf(m_rightBarBottomSpacer);
    if (iSec >= 0)
        m_rightBarVLayout->setStretch(iSec, expanded ? 1 : 0);
    if (iSp >= 0)
        m_rightBarVLayout->setStretch(iSp, expanded ? 0 : 1);
}

void AggregateChatForm::refreshRightBarModelDisplay()
{
    if (!m_rightBarModelNameLabel)
        return;
    const QList<AiPresetDefinition> presets = aiPresetDefinitions();
    if (presets.isEmpty())
        return;
    m_rightBarDisplayModelIndex = qBound(0, m_rightBarDisplayModelIndex, presets.size() - 1);
    const AiPresetDefinition& d = presets[m_rightBarDisplayModelIndex];
    m_rightBarModelNameLabel->setText(d.label);
    if (m_btnModelSwitchRow)
        m_btnModelSwitchRow->setText(d.label);
    if (m_rightBarModelIconLabel) {
        QPixmap ap;
        if (d.assistantAvatarResource.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
            ap = svgResourcePixmapFittedInSquare(d.assistantAvatarResource,
                                                 kAggregateRightBarModelIconDrawSide);
        else
            ap = rasterResourcePixmapFittedInSquare(d.assistantAvatarResource,
                                                    kAggregateRightBarModelIconDrawSide);
        if (!ap.isNull())
            m_rightBarModelIconLabel->setPixmap(ap);
        else
            m_rightBarModelIconLabel->setPixmap(
                qApp->style()
                    ->standardIcon(QStyle::SP_FileDialogListView)
                    .pixmap(kAggregateRightBarModelIconDrawSide, kAggregateRightBarModelIconDrawSide));
    }
    if (m_modelPickerList)
        m_modelPickerList->setCurrentRow(m_rightBarDisplayModelIndex);
}

void AggregateChatForm::openModelPickerSheet()
{
    if (!m_modelPickerOverlay || !m_modelPickerSheet)
        return;
    m_modelPickerOverlay->show();
    m_modelPickerOverlay->raise();
    m_modelPickerSheet->setFixedWidth(0);
    if (m_modelSheetWidthAnim) {
        m_modelSheetWidthAnim->stop();
        m_modelSheetWidthAnim->deleteLater();
        m_modelSheetWidthAnim = nullptr;
    }
    if (m_modelPickerList) {
        m_modelPickerList->setCurrentRow(m_rightBarDisplayModelIndex);
    }
    QTimer::singleShot(0, this, [this]() {
        if (!m_modelPickerOverlay || !m_modelPickerSheet)
            return;
        int w = m_modelPickerOverlay->width();
        if (w < 1)
            w = 220;
        startModelSheetWidthAnimation(0, w, false);
    });
}

void AggregateChatForm::closeModelPickerSheet()
{
    if (!m_modelPickerSheet) {
        if (m_modelPickerOverlay)
            m_modelPickerOverlay->hide();
        return;
    }
    if (m_modelPickerSheet->width() <= 0) {
        if (m_modelPickerOverlay)
            m_modelPickerOverlay->hide();
        return;
    }
    startModelSheetWidthAnimation(m_modelPickerSheet->width(), 0, true);
}

void AggregateChatForm::startModelSheetWidthAnimation(int fromWidth, int toWidth, bool hideOverlayOnFinish)
{
    if (!m_modelPickerSheet)
        return;
    if (m_modelSheetWidthAnim) {
        m_modelSheetWidthAnim->stop();
        m_modelSheetWidthAnim->deleteLater();
        m_modelSheetWidthAnim = nullptr;
    }
    m_modelPickerSheet->setFixedWidth(fromWidth);
    auto* a = new QVariantAnimation(this);
    a->setDuration(220);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->setStartValue(fromWidth);
    a->setEndValue(toWidth);
    m_modelSheetWidthAnim = a;
    connect(
        a, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) {
            if (m_modelPickerSheet)
                m_modelPickerSheet->setFixedWidth(v.toInt());
        });
    connect(
        a, &QVariantAnimation::finished, this, [this, a, hideOverlayOnFinish]() {
            m_modelSheetWidthAnim = nullptr;
            if (hideOverlayOnFinish && m_modelPickerOverlay)
                m_modelPickerOverlay->hide();
            a->deleteLater();
        });
    a->start();
}

void AggregateChatForm::onModelPickerBackClicked()
{
    closeModelPickerSheet();
}

void AggregateChatForm::onModelPickerListItem(QListWidgetItem* item)
{
    if (!item || !m_modelPickerList)
        return;
    const int row = m_modelPickerList->row(item);
    if (row < 0 || row >= aiPresetDefinitions().size())
        return;
    m_rightBarDisplayModelIndex = row;
    refreshRightBarModelDisplay();
    closeModelPickerSheet();
}

// ===================== Message Bubble =====================

QWidget* AggregateChatForm::createBubble(const MessageRecord& msg)
{
    const QString text = msg.content;
    const QString senderName = msg.senderName;
    const QDateTime time = msg.createdAt;
    const bool isOutgoing = (msg.direction == QLatin1String("out"));
    const QString originalTimestamp = msg.originalTimestamp;
    auto* row = new QWidget();
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    QString timeStr;
    if (!isOutgoing && !originalTimestamp.isEmpty()) {
        timeStr = originalTimestamp;
    } else if (isOutgoing && time.isValid()) {
        timeStr = time.toString(QStringLiteral("HH:mm"));
    }

    if (isOutgoing) {
        rowLayout->addStretch(1);

        auto* rightCol = new QWidget(row);
        auto* rightColLayout = new QVBoxLayout(rightCol);
        rightColLayout->setContentsMargins(0, 0, 0, 0);
        rightColLayout->setSpacing(4);

        auto* metaRow = new QWidget(rightCol);
        auto* metaRowLayout = new QHBoxLayout(metaRow);
        metaRowLayout->setContentsMargins(0, 0, 0, 0);
        metaRowLayout->setSpacing(8);
        metaRowLayout->addStretch(1);
        auto* timeAbove = new QLabel(timeStr, metaRow);
        timeAbove->setObjectName(QStringLiteral("bubbleOutSenderTime"));
        timeAbove->setVisible(!timeStr.isEmpty());
        metaRowLayout->addWidget(timeAbove, 0, Qt::AlignVCenter);
        auto* nickAbove = new QLabel(m_selfDisplayName, metaRow);
        nickAbove->setObjectName(QStringLiteral("bubbleOutSenderNick"));
        nickAbove->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        metaRowLayout->addWidget(nickAbove, 0, Qt::AlignVCenter);
        rightColLayout->addWidget(metaRow);

        auto* bubbleRow = new QWidget(rightCol);
        auto* bubbleRowLayout = new QHBoxLayout(bubbleRow);
        bubbleRowLayout->setContentsMargins(0, 0, 0, 0);
        bubbleRowLayout->setSpacing(0);
        bubbleRowLayout->addStretch(1);

        auto* bubble = new QFrame(bubbleRow);
        bubble->setObjectName(QStringLiteral("bubbleOut"));
        auto* bubbleLayout = new QVBoxLayout(bubble);
        bubbleLayout->setContentsMargins(12, 8, 12, 8);
        bubbleLayout->setSpacing(4);

        auto* contentLabel = new QLabel(text, bubble);
        contentLabel->setWordWrap(true);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        contentLabel->setObjectName(QStringLiteral("bubbleTextOut"));
        bubbleLayout->addWidget(contentLabel);

        QString statusText = QStringLiteral("已发送");
        if (msg.syncStatus == 10)
            statusText = QStringLiteral("待发送");
        else if (msg.syncStatus == 12)
            statusText = QStringLiteral("发送失败");
        else if (msg.syncStatus == 11 || msg.syncStatus == 1)
            statusText = QStringLiteral("已发送");

        auto* statusLabel = new QLabel(statusText, bubble);
        statusLabel->setObjectName(QStringLiteral("bubbleMetaOut"));
        bubbleLayout->addWidget(statusLabel);

        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
            auto* errorLabel = new QLabel(QStringLiteral("原因: %1").arg(msg.errorReason), bubble);
            errorLabel->setObjectName(QStringLiteral("bubbleMetaOut"));
            errorLabel->setWordWrap(true);
            bubbleLayout->addWidget(errorLabel);
        }

        bubble->setMaximumWidth(420);
        bubbleRowLayout->addWidget(bubble, 0, Qt::AlignTop);
        rightColLayout->addWidget(bubbleRow);

        rowLayout->addWidget(rightCol, 0, Qt::AlignTop);

        auto* avatarLabel = new QLabel(row);
        avatarLabel->setObjectName(QStringLiteral("bubbleOutAvatar"));
        avatarLabel->setFixedSize(36, 36);
        avatarLabel->setScaledContents(false);
        if (!m_selfAvatarPixmap.isNull())
            avatarLabel->setPixmap(m_selfAvatarPixmap);
        rowLayout->addWidget(avatarLabel, 0, Qt::AlignTop);

        return row;
    }

    // 对方消息：左侧通用头像，与右侧己方气泡布局对称；仅在存在 senderName / originalTimestamp 时展示元信息。
    // 微信 RPA：originalTimestamp 为入库/解析时刻；千牛常为 OCR 名称与时间。
    auto* avatarIn = new QLabel(row);
    avatarIn->setObjectName(QStringLiteral("bubbleInAvatar"));
    avatarIn->setFixedSize(36, 36);
    avatarIn->setScaledContents(false);
    if (!m_customerDefaultAvatarPixmap.isNull())
        avatarIn->setPixmap(m_customerDefaultAvatarPixmap);
    rowLayout->addWidget(avatarIn, 0, Qt::AlignTop);

    auto* col = new QWidget(row);
    auto* colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(4);

    QString metaAbove;
    if (!senderName.isEmpty() && !timeStr.isEmpty())
        metaAbove = senderName + QStringLiteral("  ") + timeStr;
    else if (!senderName.isEmpty())
        metaAbove = senderName;
    else if (!timeStr.isEmpty())
        metaAbove = timeStr;

    if (!metaAbove.isEmpty()) {
        auto* metaLabel = new QLabel(metaAbove, col);
        metaLabel->setObjectName(QStringLiteral("bubbleMetaIn"));
        metaLabel->setWordWrap(false);
        metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        colLayout->addWidget(metaLabel);
    }

    auto* bubble = new QFrame(col);
    bubble->setObjectName(QStringLiteral("bubbleIn"));
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(4);

    if (!msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath)) {
        QPixmap pm(msg.contentImagePath);
        if (!pm.isNull()) {
            const int maxW = 360;
            if (pm.width() > maxW)
                pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
            auto* imgLabel = new QLabel(bubble);
            imgLabel->setPixmap(pm);
            imgLabel->setObjectName(QStringLiteral("bubbleImageIn"));
            bubbleLayout->addWidget(imgLabel);
        }
    }

    auto* contentLabel = new QLabel(text, bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setObjectName(QStringLiteral("bubbleTextIn"));
    bubbleLayout->addWidget(contentLabel);

    bubble->setMaximumWidth(420);
    colLayout->addWidget(bubble);

    rowLayout->addWidget(col);
    rowLayout->addStretch(1);

    return row;
}

// ===================== Date Separator =====================

QWidget* AggregateChatForm::createDateSeparator(const QDate& date)
{
    auto* row = new QWidget();
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(40, 10, 40, 6);
    layout->setSpacing(10);

    auto* leftLine = new QFrame(row);
    leftLine->setFixedHeight(1);
    leftLine->setObjectName("dateSeparatorLine");

    QString dateText;
    if (date == QDate::currentDate())
        dateText = QStringLiteral("今天");
    else if (date == QDate::currentDate().addDays(-1))
        dateText = QStringLiteral("昨天");
    else
        dateText = date.toString(QStringLiteral("yyyy年M月d日"));

    auto* label = new QLabel(dateText, row);
    label->setObjectName("dateSeparatorText");
    label->setAlignment(Qt::AlignCenter);

    auto* rightLine = new QFrame(row);
    rightLine->setFixedHeight(1);
    rightLine->setObjectName("dateSeparatorLine");

    layout->addWidget(leftLine, 1);
    layout->addWidget(label);
    layout->addWidget(rightLine, 1);

    return row;
}

// ===================== Data Operations =====================

void AggregateChatForm::refreshConversationList()
{
    auto& mgr = ConversationManager::instance();
    const auto convs = mgr.allConversations();
    MessageDao msgDao;
    const QHash<int, QString> lastDirs = msgDao.lastDirectionsByConversation();

    QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

    m_conversationList->clear();
    int count = 0;
    for (const auto& conv : convs) {
        const QString lastDir = lastDirs.value(conv.id);

        bool inThisTab = false;
        if (m_currentTab == AggregateConversationTab::All) {
            inThisTab = true;
        } else if (m_currentTab == AggregateConversationTab::Pending) {
            if (lastDir == QLatin1String("in"))
                inThisTab = true;
            else if (lastDir == QLatin1String("out") && m_pendingStickyConvId == conv.id)
                inThisTab = true;
        } else {
            // 无消息、己方回复、或其它非 in 方向均归入「已回复」（与「待处理」互补，避免遗漏）
            if (lastDir.isEmpty() || lastDir != QLatin1String("in"))
                inThisTab = true;
        }
        if (!inThisTab)
            continue;

        if (m_platformFilter != AggregatePlatformFilter::All) {
            QString wantPlat;
            switch (m_platformFilter) {
            case AggregatePlatformFilter::Qianniu:
                wantPlat = QStringLiteral("qianniu");
                break;
            case AggregatePlatformFilter::Pdd:
                wantPlat = QStringLiteral("pdd_web");
                break;
            case AggregatePlatformFilter::Doudian:
                wantPlat = QStringLiteral("douyin");
                break;
            case AggregatePlatformFilter::Wechat:
                wantPlat = QStringLiteral("wechat_pc");
                break;
            default:
                break;
            }
            if (conv.platform != wantPlat)
                continue;
        }

        if (!keyword.isEmpty() && !conv.customerName.contains(keyword, Qt::CaseInsensitive)
            && !conv.lastMessage.contains(keyword, Qt::CaseInsensitive))
            continue;

        auto* item = new QListWidgetItem(m_conversationList);
        auto* widget = createConversationItem(conv);
        item->setSizeHint(widget->sizeHint() + QSize(0, 4));
        item->setData(Qt::UserRole, conv.id);
        m_conversationList->setItemWidget(item, widget);
        count++;
    }

    m_leftStack->setCurrentIndex(count > 0 ? 0 : 1);

    if (m_currentConvId > 0) {
        bool currentStillVisible = false;
        for (int i = 0; i < m_conversationList->count(); ++i) {
            QListWidgetItem* listItem = m_conversationList->item(i);
            if (listItem && listItem->data(Qt::UserRole).toInt() == m_currentConvId) {
                currentStillVisible = true;
                break;
            }
        }
        if (!currentStillVisible) {
            const int lost = m_currentConvId;
            m_currentConvId = -1;
            if (m_pendingStickyConvId == lost)
                m_pendingStickyConvId = -1;
            ConversationManager::instance().selectConversation(-1);
            abortAggregateAiRequest();
            showCenterEmptyState();
            showRightEmptyState();
        }
    }

    if (m_currentConvId > 0 && m_conversationList->count() > 0) {
        for (int i = 0; i < m_conversationList->count(); ++i) {
            QListWidgetItem* listItem = m_conversationList->item(i);
            if (!listItem || listItem->data(Qt::UserRole).toInt() != m_currentConvId)
                continue;
            m_conversationList->setCurrentItem(listItem);
            listItem->setSelected(true);
            m_conversationList->scrollToItem(
                listItem,
                QAbstractItemView::PositionAtCenter);
            break;
        }
    }
    syncConversationItemVisualState();
}

void AggregateChatForm::showConversation(int conversationId)
{
    abortAggregateAiRequest();

    const int prevId = m_currentConvId;
    if (prevId > 0 && prevId != conversationId && m_pendingStickyConvId == prevId)
        m_pendingStickyConvId = -1;

    m_currentConvId = conversationId;
    auto& mgr = ConversationManager::instance();
    mgr.selectConversation(conversationId);

    auto messages = mgr.messages(conversationId);
    renderConversationMessages(messages);
    m_currentMessageSignature = buildMessageSignature(messages);

    // Update header
    const auto conv = m_conversationService
                          ? m_conversationService->conversationById(conversationId)
                          : std::optional<ConversationInfo>();
    if (conv) {
        m_chatHeader->setText(QStringLiteral("%1 (%2)").arg(conv->customerName, conv->platform));
        updateCustomerInfo(*conv);
        resetSendTimelineForConversation();
    }

    m_centerStack->setCurrentWidget(m_chatArea);
    m_inputEdit->setFocus();

    scheduleScrollChatToBottom();
    updateAggregateAiControlsVisibility();

    tryAggregateAutoReply(conversationId, QStringLiteral("T1"));
}

void AggregateChatForm::renderConversationMessages(const QVector<MessageRecord>& messages)
{
    while (m_messageLayout->count() > 1) {
        auto* item = m_messageLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_lastBubbleDate = QDate();
    for (const auto& msg : messages)
        appendMessageBubble(msg);
}

void AggregateChatForm::appendMessageBubble(const MessageRecord& msg)
{
    QDate msgDate = msg.createdAt.isValid() ? msg.createdAt.date() : QDate::currentDate();
    if (!m_lastBubbleDate.isValid() || msgDate != m_lastBubbleDate) {
        int sepIdx = m_messageLayout->count() - 1;
        m_messageLayout->insertWidget(sepIdx, createDateSeparator(msgDate));
        m_lastBubbleDate = msgDate;
    }

    auto* bubble = createBubble(msg);
    int idx = m_messageLayout->count() - 1;
    m_messageLayout->insertWidget(idx, bubble);
}

QString AggregateChatForm::buildMessageSignature(const QVector<MessageRecord>& messages) const
{
    QStringList parts;
    parts.reserve(messages.size());
    for (const auto& msg : messages) {
        parts.append(QStringLiteral("%1:%2:%3")
                         .arg(msg.id)
                         .arg(msg.syncStatus)
                         .arg(msg.errorReason));
    }
    return parts.join(QChar('|'));
}

void AggregateChatForm::refreshVisibleConversationMessages()
{
    if (m_currentConvId <= 0 || !m_messageLayout || !m_messageScroll)
        return;

    auto messages = ConversationManager::instance().messages(m_currentConvId);
    const QString newSignature = buildMessageSignature(messages);
    if (newSignature == m_currentMessageSignature)
        return;

    auto* sb = m_messageScroll->verticalScrollBar();
    const bool wasNearBottom = !sb || sb->value() >= sb->maximum() - 24;
    renderConversationMessages(messages);
    m_currentMessageSignature = newSignature;
    if (wasNearBottom)
        scheduleScrollChatToBottom();
}

void AggregateChatForm::scrollToBottom()
{
    auto* sb = m_messageScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AggregateChatForm::scheduleScrollChatToBottom()
{
    QTimer::singleShot(100, this, [this]() {
        if (!m_messageScroll || !m_messageContainer)
            return;
        m_messageContainer->updateGeometry();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        scrollToBottom();
        // QScrollArea 内容高度偶发晚一帧才更新，再拉一次
        QTimer::singleShot(0, this, [this]() { scrollToBottom(); });
    });
}

void AggregateChatForm::updateCustomerInfo(const ConversationInfo& /*conv*/)
{
    if (m_rightStack)
        m_rightStack->setCurrentWidget(m_customerDetail);
}

void AggregateChatForm::resetSendTimelineForConversation()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
}

void AggregateChatForm::pollSendTimeline()
{
    if (m_currentConvId <= 0 || !m_sendTimeline)
        return;

    MessageSendEventDao dao;
    const QVector<MessageSendEventRecord> rows =
        dao.listSince(m_currentConvId, m_sendTimelineBaselineId);
    if (rows.isEmpty())
        return;

    for (const MessageSendEventRecord& e : rows) {
        m_sendTimeline->appendPlainText(formatSendEventLine(e));
        m_sendTimelineBaselineId = std::max(m_sendTimelineBaselineId, e.id);
    }
}

void AggregateChatForm::onClearSendTimeline()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
}

void AggregateChatForm::showCenterEmptyState()
{
    abortAggregateAiRequest();
    m_centerStack->setCurrentWidget(m_centerEmptyState);
    updateAggregateAiControlsVisibility();
}

void AggregateChatForm::showRightEmptyState()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    m_sendTimelineBaselineId = 0;
}

// ===================== Slots =====================

void AggregateChatForm::setConversationTab(AggregateConversationTab tab)
{
    m_currentTab = tab;
    if (m_btnAll)
        m_btnAll->setChecked(tab == AggregateConversationTab::All);
    if (m_btnPending)
        m_btnPending->setChecked(tab == AggregateConversationTab::Pending);
    if (m_btnReplied)
        m_btnReplied->setChecked(tab == AggregateConversationTab::Replied);
    refreshConversationList();
}

void AggregateChatForm::onTabAllClicked()
{
    setConversationTab(AggregateConversationTab::All);
}

void AggregateChatForm::onTabPendingClicked()
{
    setConversationTab(AggregateConversationTab::Pending);
}

void AggregateChatForm::onTabRepliedClicked()
{
    setConversationTab(AggregateConversationTab::Replied);
}

void AggregateChatForm::onConversationItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    int convId = item->data(Qt::UserRole).toInt();
    qDebug() << "点击会话:" << convId;
    showConversation(convId);
    refreshConversationList();
}

void AggregateChatForm::onConversationListContextMenu(const QPoint& pos)
{
    if (!m_conversationList)
        return;
    QListWidgetItem* item = m_conversationList->itemAt(pos);
    if (!item)
        return;
    const int convId = item->data(Qt::UserRole).toInt();
    if (convId <= 0)
        return;

    QMenu menu(this);
    QAction* actClear = menu.addAction(QStringLiteral("清空聊天记录"));
    QAction* actDelete = menu.addAction(QStringLiteral("删除会话"));
    QAction* chosen = menu.exec(m_conversationList->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actClear) {
        QMessageBox confirmBox(this);
        confirmBox.setIcon(QMessageBox::Question);
        confirmBox.setWindowTitle(QStringLiteral("清空聊天记录"));
        confirmBox.setText(
            QStringLiteral("确定清空该会话的全部聊天记录？此操作不可恢复。"));
        confirmBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirmBox.setDefaultButton(QMessageBox::No);
        confirmBox.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (confirmBox.exec() != QMessageBox::Yes)
            return;

        MessageDao msgDao;
        if (!msgDao.clearAllForConversation(convId)) {
            QMessageBox warnBox(this);
            warnBox.setIcon(QMessageBox::Warning);
            warnBox.setWindowTitle(QStringLiteral("错误"));
            warnBox.setText(QStringLiteral("清空失败，请查看日志或确认数据库已升级。"));
            warnBox.setStandardButtons(QMessageBox::Ok);
            warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
            warnBox.exec();
            return;
        }

        ConversationDao convDao;
        convDao.updateLastMessage(convId, QString(), QDateTime::currentDateTime());

        if (m_pendingStickyConvId == convId)
            m_pendingStickyConvId = -1;

        if (m_currentConvId == convId) {
            m_lastBubbleDate = QDate();
            renderConversationMessages({});
            m_currentMessageSignature = buildMessageSignature({});
            resetSendTimelineForConversation();
        }

        refreshConversationList();
        showStatusMessage(QStringLiteral("已清空聊天记录"), 3000);
        return;
    }

    if (chosen == actDelete) {
        QMessageBox delBox(this);
        delBox.setIcon(QMessageBox::Warning);
        delBox.setWindowTitle(QStringLiteral("删除会话"));
        delBox.setText(QStringLiteral(
            "确定删除该会话？将同时删除会话记录、全部聊天消息、入站队列中该会话相关数据，且不可恢复。"));
        delBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        delBox.setDefaultButton(QMessageBox::No);
        delBox.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (delBox.exec() != QMessageBox::Yes)
            return;

        if (m_currentConvId == convId) {
            m_currentConvId = -1;
            m_lastBubbleDate = QDate();
            renderConversationMessages({});
            m_currentMessageSignature = buildMessageSignature({});
            showCenterEmptyState();
            showRightEmptyState();
            if (m_inputEdit)
                m_inputEdit->clear();
        }
        if (m_pendingStickyConvId == convId)
            m_pendingStickyConvId = -1;

        ConversationManager::instance().deleteConversation(convId);
        showStatusMessage(QStringLiteral("已删除会话"), 3000);
    }
}

void AggregateChatForm::onSendClicked()
{
    if (m_currentConvId <= 0) return;
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_inputEdit->clear();
    ConversationManager::instance().sendMessage(m_currentConvId, text);
}

void AggregateChatForm::onStopAutoReplyClicked()
{
    QSettings s = AppSettings::create();
    s.setValue(QStringLiteral("aggregateAutoReply/emergencyUserStop"), true);

    abortAggregateAiRequest();

    if (m_modeCombo) {
        m_suppressingModeComboChange = true;
        m_modeCombo->setCurrentIndex(0);
        m_lastAggregateModeIndex = 0;
        m_suppressingModeComboChange = false;
        updateAggregateAiControlsVisibility();
    }

    showStatusMessage(QStringLiteral("已停止自动回复并切换为「人工接待」"), 5000);
}

void AggregateChatForm::onConversationListChanged()
{
    refreshConversationList();
}

void AggregateChatForm::onNewMessage(int conversationId, const MessageRecord& msg)
{
    if (msg.direction == QLatin1String("in") && m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("新消息: %1").arg(msg.content.left(30)), 3000);

    if (conversationId == m_currentConvId && msg.direction == QLatin1String("in"))
        tryAggregateAutoReply(conversationId, QStringLiteral("T2"));
}

void AggregateChatForm::onSentOk(int conversationId, const MessageRecord& msg)
{
    if (conversationId == m_currentConvId && msg.direction == QLatin1String("out"))
        m_pendingStickyConvId = conversationId;
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
}

void AggregateChatForm::showStatusMessage(const QString& text, int timeoutMs)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.trimmed().isEmpty());
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, text]() {
            if (m_statusLabel && m_statusLabel->text() == text) {
                m_statusLabel->clear();
                m_statusLabel->setVisible(false);
            }
        });
    }
}

void AggregateChatForm::updateAggregateAiControlsVisibility()
{
    const bool modeAi = m_modeCombo && m_modeCombo->currentIndex() == kAggregateModeIndexAiAssist;
    const bool convOk = m_currentConvId > 0;
    const bool onChat = m_centerStack && m_centerStack->currentWidget() == m_chatArea;
    const bool showGen = modeAi && convOk && onChat;
    if (m_btnAiModelPick) {
        m_btnAiModelPick->setVisible(showGen);
        m_btnAiModelPick->setEnabled(showGen && !m_aggregateAiGenerating);
    }
    if (m_btnAiGenerate) {
        m_btnAiGenerate->setVisible(showGen);
        m_btnAiGenerate->setEnabled(showGen && !m_aggregateAiGenerating);
    }
}

void AggregateChatForm::setAggregateAiBusy(bool busy)
{
    m_aggregateAiGenerating = busy;
    if (m_btnSend)
        m_btnSend->setEnabled(!busy);
    if (m_inputEdit)
        m_inputEdit->setReadOnly(busy);
    if (m_modeCombo)
        m_modeCombo->setEnabled(!busy);
    if (busy)
        showStatusMessage(QStringLiteral("AI 正在生成草稿..."), 0);
    updateAggregateAiControlsVisibility();
}

void AggregateChatForm::abortAggregateAiRequest()
{
    clearStreamingSession(m_aggregateAiSession);
    clearStreamingSession(m_autoReplySession);
    if (m_aggregateAiGenerating)
        setAggregateAiBusy(false);
    if (m_autoReplyBusy) {
        m_autoReplyBusy = false;
        m_autoReplyTargetConvId = -1;
        m_autoReplyAccumulated.clear();
    }
}

void AggregateChatForm::clearStreamingSession(IAiStreamingSession*& session)
{
    if (!session)
        return;
    session->disconnect(this);
    session->abort();
    session->deleteLater();
    session = nullptr;
}

void AggregateChatForm::onModeComboChanged()
{
    if (!m_modeCombo || m_suppressingModeComboChange)
        return;

    const int newIdx = m_modeCombo->currentIndex();
    if (newIdx == kAggregateModeIndexAutoReply && m_lastAggregateModeIndex != kAggregateModeIndexAutoReply) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("AI 自动回复"),
                        QStringLiteral("开启后，AI 将接管来自千牛平台会话中的客户消息，并在满足条件时自动生成并发送回复（当前版本仅讨论千牛；"
                                       "具体触发与风控规则以后续实现为准）。\n\n"
                                       "请确认已在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中"
                                       "配置好模型与 API，并了解误发、合规与费用风险。\n\n"
                                       "确定要进入「AI 自动回复」模式吗？"),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        box.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (box.exec() != QMessageBox::Yes) {
            m_suppressingModeComboChange = true;
            m_modeCombo->setCurrentIndex(m_lastAggregateModeIndex);
            m_suppressingModeComboChange = false;
            return;
        }
        QSettings st = AppSettings::create();
        st.setValue(QStringLiteral("aggregateAutoReply/emergencyUserStop"), false);
    }

    m_lastAggregateModeIndex = newIdx;
    updateAggregateAiControlsVisibility();
}

void AggregateChatForm::refreshAggregateAiModelButtonUi()
{
    if (m_btnAiModelPick)
        m_btnAiModelPick->setText(
            QStringLiteral("模型：%1").arg(aggregateModelMenuLabel(m_aggregateAiSessionModelKey)));
    if (m_aggregateAiModelMenu) {
        for (QAction* a : m_aggregateAiModelMenu->actions()) {
            const QString k = a->data().toString();
            a->setChecked(k == m_aggregateAiSessionModelKey);
        }
    }
}

void AggregateChatForm::onAggregateAiModelMenuTriggered(QAction* action)
{
    if (!action)
        return;
    const QString key = action->data().toString();
    if (key.isEmpty() || key == m_aggregateAiSessionModelKey)
        return;
    m_aggregateAiSessionModelKey = key;
    QSettings s = AppSettings::create();
    s.setValue(QStringLiteral("aggregateAi/sessionModelKey"), key);
    refreshAggregateAiModelButtonUi();
}

void AggregateChatForm::onGenerateAiDraftClicked()
{
    if (m_currentConvId <= 0 || m_aggregateAiGenerating || !m_aiChatService)
        return;

    QString skip;
    AggregateAiBuiltRequest built =
        m_aiChatService->buildAggregateReplyRequest(m_currentConvId, m_aggregateAiSessionModelKey);
    if (!handleAggregateBuildFailure(this, built, false, &skip))
        return;

    m_aggregateAiBaseline = m_inputEdit ? m_inputEdit->toPlainText() : QString();
    m_aggregateAiAccumulated.clear();
    setAggregateAiBusy(true);
    built.request.extraRootFields.insert(QStringLiteral("max_tokens"), 512);
    clearStreamingSession(m_aggregateAiSession);
    m_aggregateAiSession = m_aiChatService->createSession(built.config, built.request, this);
    connect(m_aggregateAiSession, &IAiStreamingSession::delta, this, &AggregateChatForm::onAggregateAiStreamDelta);
    connect(m_aggregateAiSession, &IAiStreamingSession::completed, this, &AggregateChatForm::onAggregateAiCompleted);
    connect(m_aggregateAiSession, &IAiStreamingSession::failed, this, &AggregateChatForm::onAggregateAiFailed);
    m_aggregateAiSession->start();
}

void AggregateChatForm::tryAggregateAutoReply(int conversationId, const QString& triggerTag)
{
    if (conversationId <= 0 || !m_aiChatService)
        return;
    if (!m_modeCombo || m_modeCombo->currentIndex() != kAggregateModeIndexAutoReply)
        return;

    QSettings st = AppSettings::create();
    if (st.value(QStringLiteral("aggregateAutoReply/emergencyUserStop"), false).toBool()) {
        qInfo() << "[AggregateAutoReply] skip emergencyUserStop trigger=" << triggerTag << "conv=" << conversationId;
        return;
    }

    const auto conv = m_conversationService
                          ? m_conversationService->conversationById(conversationId)
                          : std::optional<ConversationInfo>();
    if (!m_conversationService || !m_conversationService->isAggregateAutoReplyCandidate(conversationId)) {
        qDebug() << "[AggregateAutoReply] skip eligibility trigger=" << triggerTag << "conv=" << conversationId
                 << "platform=" << (conv ? conv->platform : QString());
        return;
    }

    if (m_aggregateAiGenerating || m_autoReplyBusy) {
        qInfo() << "[AggregateAutoReply] skip busy trigger=" << triggerTag << "conv=" << conversationId;
        return;
    }

    QString skipReason;
    AggregateAiBuiltRequest built =
        m_aiChatService->buildAggregateReplyRequest(conversationId, m_aggregateAiSessionModelKey);
    if (!handleAggregateBuildFailure(this, built, true, &skipReason)) {
        qInfo() << "[AggregateAutoReply] skip build:" << skipReason << "trigger=" << triggerTag
                << "conv=" << conversationId;
        return;
    }

    m_autoReplyTargetConvId = conversationId;
    m_autoReplyAccumulated.clear();
    m_autoReplyBusy = true;
    built.request.extraRootFields.insert(QStringLiteral("max_tokens"), 512);
    clearStreamingSession(m_autoReplySession);
    m_autoReplySession = m_aiChatService->createSession(built.config, built.request, this);
    connect(m_autoReplySession, &IAiStreamingSession::delta, this, &AggregateChatForm::onAutoReplyStreamDelta);
    connect(m_autoReplySession, &IAiStreamingSession::completed, this, &AggregateChatForm::onAutoReplyCompleted);
    connect(m_autoReplySession, &IAiStreamingSession::failed, this, &AggregateChatForm::onAutoReplyFailed);
    m_autoReplySession->start();
    showStatusMessage(QStringLiteral("AI 自动回复处理中..."), 0);
    qInfo() << "[AggregateAutoReply] started trigger=" << triggerTag << "conv=" << conversationId;
}

void AggregateChatForm::onAutoReplyStreamDelta(const QString& delta)
{
    if (!m_autoReplyBusy)
        return;
    m_autoReplyAccumulated += delta;
}

void AggregateChatForm::onAutoReplyCompleted()
{
    if (!m_autoReplyBusy)
        return;
    const int cid = m_autoReplyTargetConvId;
    m_autoReplyBusy = false;
    clearStreamingSession(m_autoReplySession);
    m_autoReplyTargetConvId = -1;
    const QString text = m_autoReplyAccumulated.trimmed();
    m_autoReplyAccumulated.clear();

    if (text.isEmpty()) {
        qInfo() << "[AggregateAutoReply] empty completion conv=" << cid;
        showStatusMessage(QStringLiteral("自动回复：模型未返回正文"), 5000);
        return;
    }

    ConversationManager::instance().sendMessage(cid, text);
    qInfo() << "[AggregateAutoReply] sent conv=" << cid << "len=" << text.size();
    showStatusMessage(QStringLiteral("已自动发送 AI 回复"), 4000);
}

void AggregateChatForm::onAutoReplyFailed(const QString& reason)
{
    if (!m_autoReplyBusy)
        return;
    m_autoReplyBusy = false;
    clearStreamingSession(m_autoReplySession);
    m_autoReplyTargetConvId = -1;
    m_autoReplyAccumulated.clear();
    qInfo() << "[AggregateAutoReply] failed:" << reason;
    showStatusMessage(QStringLiteral("自动回复失败：%1").arg(reason.left(120)), 6000);
}

void AggregateChatForm::onAggregateAiStreamDelta(const QString& delta)
{
    if (!m_aggregateAiGenerating)
        return;
    m_aggregateAiAccumulated += delta;
    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + m_aggregateAiAccumulated);
}

void AggregateChatForm::onAggregateAiCompleted()
{
    if (!m_aggregateAiGenerating)
        return;
    clearStreamingSession(m_aggregateAiSession);
    setAggregateAiBusy(false);
    showStatusMessage(QStringLiteral("AI 草稿已生成，可直接发送或继续修改"), 4000);
}

void AggregateChatForm::onAggregateAiFailed(const QString& reason)
{
    if (!m_aggregateAiGenerating)
        return;
    clearStreamingSession(m_aggregateAiSession);
    const QString tail =
        m_aggregateAiAccumulated.isEmpty()
            ? QStringLiteral("\n\n（AI 未能完成：%1）").arg(reason)
            : QStringLiteral("\n\n（后续内容未能完成：%1）").arg(reason);
    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + m_aggregateAiAccumulated + tail);
    setAggregateAiBusy(false);
    showStatusMessage(QStringLiteral("AI 草稿生成失败：%1").arg(reason.left(120)), 6000);
}
