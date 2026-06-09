#include "aggregatechatform.h"
#include "foldarrowcombobox.h"
#include "../core/conversationmanager.h"
#include "../core/messagerouter.h"
#include "../ipc/ipcservice.h"
#include "conversationlistmodel.h"
#include "messagelistmodel.h"
#include <QButtonGroup>
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../data/messagesendeventdao.h"
#include "../data/qianniuconversationdao.h"
#include "../data/wechatmessagedao.h"
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
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QElapsedTimer>
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
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QSvgRenderer>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QShowEvent>
#include <QSlider>
#include <QSizePolicy>
#include <QColor>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QListWidget>
#include <QListWidgetItem>
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

MessageRecord messageRecordFromUnified(const Models::Message& msg)
{
    MessageRecord record;
    record.id = msg.id;
    record.conversationId = msg.conversationId;
    record.direction = Models::legacyDirectionFromMessageDirection(msg.direction);
    record.content = msg.content;
    record.sender = msg.direction == Models::MessageDirection::Outbound
        ? QStringLiteral("agent")
        : (msg.direction == Models::MessageDirection::System
               ? QStringLiteral("system")
               : QStringLiteral("customer"));
    record.senderName = msg.metadata.value(QStringLiteral("senderName")).toString();
    record.createdAt = msg.platformDisplayedAt.isValid() ? msg.platformDisplayedAt : msg.observedAt;
    record.platformMsgId = msg.platformMessageId;
    record.syncStatus = Models::legacySyncStatusFromMessageStatus(msg.status);
    record.errorReason = msg.metadata.value(QStringLiteral("errorReason")).toString();
    record.originalTimestamp = msg.metadata.value(QStringLiteral("originalTimestamp")).toString();
    record.contentImagePath = msg.evidenceRef;
    record.status = Models::toString(msg.status);
    record.sourceType = Models::toString(msg.sourceType);
    record.confidence = msg.confidence;
    record.verificationStatus = Models::toString(msg.verificationStatus);
    record.contentType = Models::toString(msg.contentType);
    record.observedAt = msg.observedAt;
    return record;
}

ConversationInfo conversationInfoFromUnified(const Models::Conversation& conv)
{
    ConversationInfo info;
    info.id = conv.id;
    info.platform = Models::toString(conv.platformType);
    info.platformConversationId = conv.platformConversationId;
    info.customerName = conv.title;
    info.lastMessage = conv.lastMessage;
    info.lastTime = conv.lastMessageAt;
    info.unreadCount = conv.unreadCount;
    info.status = Models::toString(conv.status);
    info.createdAt = conv.createdAt;
    info.accountId = conv.accountId;
    info.sourceType = Models::toString(conv.sourceType);
    info.confidence = conv.confidence;
    info.updatedAt = conv.updatedAt;
    return info;
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

class ConversationItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ConversationItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return { 240, 76 };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const ConversationInfo conv = index.data(ConversationListModel::ConversationRole).value<ConversationInfo>();
        const QString lastDirection = index.data(ConversationListModel::LastDirectionRole).toString();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect r = option.rect.adjusted(4, 3, -4, -3);
        QColor fill = Qt::transparent;
        if (selected)
            fill = QColor(QStringLiteral("#DFF2FC"));
        else if (hovered)
            fill = QColor(QStringLiteral("#F5FBFE"));
        if (fill.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(r, 6, 6);
        }

        const QColor avatarColor =
            conv.platform == QLatin1String("wechat") ? QColor(QStringLiteral("#57C17A"))
            : conv.platform == QLatin1String("pdd_web")  ? QColor(QStringLiteral("#F2A93B"))
            : conv.platform == QLatin1String("douyin")   ? QColor(QStringLiteral("#EE4D5A"))
                                                         : QColor(QStringLiteral("#20B8E8"));
        const int avatarSide = 44;
        const QRect avatarRect(r.left() + 8, r.top() + (r.height() - avatarSide) / 2, avatarSide, avatarSide);
        painter->setBrush(avatarColor.lighter(172));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(avatarRect);
        painter->setBrush(avatarColor);
        painter->drawEllipse(avatarRect.adjusted(4, 4, -4, -4));
        painter->setPen(Qt::white);
        QFont avatarFont(option.font);
        avatarFont.setBold(true);
        painter->setFont(avatarFont);
        const QString avatarText = conv.customerName.trimmed().isEmpty()
                                        ? QStringLiteral("?")
                                        : conv.customerName.left(1);
        painter->drawText(avatarRect, Qt::AlignCenter, avatarText);

        const QRect textRect(avatarRect.right() + 8, r.top() + 8,
                             qMax(1, r.right() - avatarRect.right() - 14), r.height() - 16);
        QFont titleFont(option.font);
        titleFont.setBold(true);
        const QFontMetrics titleFm(titleFont);
        QString preview = conv.lastMessage;
        if (!lastDirection.isEmpty())
            preview = preview.isEmpty() ? lastDirection : preview;
        const bool hasPreview = !preview.trimmed().isEmpty();
        const int titleY = hasPreview
                               ? textRect.top()
                               : textRect.top() + qMax(0, (textRect.height() - titleFm.height()) / 2);
        QString timeStr;
        if (conv.lastTime.isValid()) {
            timeStr = conv.lastTime.date() == QDate::currentDate()
                          ? conv.lastTime.toString(QStringLiteral("HH:mm"))
                          : conv.lastTime.toString(QStringLiteral("M-dd"));
        }
        QFont smallFont(option.font);
        const QFontMetrics timeFm(smallFont);
        const int titleTimeGap = 8;
        const int timeWidth = timeStr.isEmpty() ? 0 : qMin(46, qMax(30, timeFm.horizontalAdvance(timeStr) + 2));
        const int titleMaxWidth = qMax(1, textRect.width() - (timeWidth > 0 ? timeWidth + titleTimeGap : 0));
        const int titleNaturalWidth = titleFm.horizontalAdvance(conv.customerName);
        const bool titleFits = titleNaturalWidth <= titleMaxWidth;
        const int titleDrawWidth = titleFits ? titleNaturalWidth + 2 : titleMaxWidth;

        painter->setFont(titleFont);
        painter->setPen(QColor(QStringLiteral("#111827")));
        const QString title = titleFm.elidedText(conv.customerName, Qt::ElideRight, titleMaxWidth);
        painter->drawText(QRect(textRect.left(), titleY, titleDrawWidth, titleFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, title);

        painter->setFont(smallFont);
        painter->setPen(QColor(QStringLiteral("#9CA3AF")));
        if (timeWidth > 0) {
            const int timeX = titleFits
                                  ? qMin(textRect.right() - timeWidth, textRect.left() + titleDrawWidth + titleTimeGap)
                                  : textRect.right() - timeWidth;
            painter->drawText(QRect(timeX, titleY, timeWidth, titleFm.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, timeStr);
        }

        QFont previewFont(option.font);
        if (previewFont.pointSizeF() > 0)
            previewFont.setPointSizeF(previewFont.pointSizeF() + 0.5);
        else if (previewFont.pixelSize() > 0)
            previewFont.setPixelSize(previewFont.pixelSize() + 1);
        const QFontMetrics bodyFm(previewFont);
        if (hasPreview) {
            painter->setFont(previewFont);
            painter->setPen(QColor(QStringLiteral("#4B5563")));
            preview = bodyFm.elidedText(preview, Qt::ElideRight, textRect.width() - (conv.unreadCount > 0 ? 22 : 0));
            painter->drawText(QRect(textRect.left(), titleY + titleFm.height() + 8,
                                    textRect.width() - (conv.unreadCount > 0 ? 22 : 0), bodyFm.height() + 4),
                              Qt::AlignLeft | Qt::AlignTop, preview);
        }

        if (conv.unreadCount > 0) {
            const QString badge = conv.unreadCount > 99 ? QStringLiteral("99+") : QString::number(conv.unreadCount);
            const QSize badgeSize(qMax(18, bodyFm.horizontalAdvance(badge) + 10), 18);
            const int badgeY = hasPreview
                                   ? titleY + titleFm.height() + 7
                                   : textRect.top() + qMax(0, (textRect.height() - badgeSize.height()) / 2);
            const QRect badgeRect(textRect.right() - badgeSize.width(), badgeY,
                                  badgeSize.width(), badgeSize.height());
            painter->setBrush(QColor(QStringLiteral("#20B8E8")));
            painter->drawRoundedRect(badgeRect, 9, 9);
            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, badge);
        }

        painter->restore();
    }
};

class MessageItemDelegate final : public QStyledItemDelegate
{
public:
    explicit MessageItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setSelfProfile(const QString& displayName, const QPixmap& avatar)
    {
        m_selfDisplayName = displayName;
        m_selfAvatarPixmap = avatar;
    }

    void setCustomerAvatar(const QPixmap& avatar)
    {
        m_customerAvatarPixmap = avatar;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool separator = index.data(MessageListModel::IsSeparatorRole).toBool();
        if (separator)
            return { qMax(320, option.rect.width()), 34 };

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const int avail = qMax(280, option.rect.width() > 0 ? option.rect.width() - 132 : 420);
        const int bubbleWidth = qMin(420, avail);
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;
        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setTextWidth(qMax(1, bubbleWidth - 24));
        doc.setPlainText(displayText);
        const int textH = qMax(QFontMetrics(option.font).height(), qCeil(doc.size().height()));
        const int imageH = messageImageHeight(msg, bubbleWidth);
        const int metaH = messageMetaHeight(msg);
        int h = 2 + qMax(36, imageH > 0 ? imageH + textH + 32 : textH + 28) + metaH;
        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty())
            h += 18;
        return { qMax(320, option.rect.width()), qMax(64, h) };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const bool separator = index.data(MessageListModel::IsSeparatorRole).toBool();
        if (separator) {
            const QDate date = index.data(MessageListModel::SeparatorDateRole).toDate();
            const QRect r = option.rect.adjusted(16, 4, -16, -4);
            painter->setPen(QColor(QStringLiteral("#CBD5E1")));
            painter->drawLine(r.left(), r.center().y(), r.left() + r.width() / 2 - 28, r.center().y());
            painter->drawLine(r.left() + r.width() / 2 + 28, r.center().y(), r.right(), r.center().y());
            painter->setPen(QColor(QStringLiteral("#64748B")));
            const QString text = date == QDate::currentDate()
                                     ? QStringLiteral("今天")
                                     : date.toString(QStringLiteral("yyyy-MM-dd"));
            painter->drawText(r, Qt::AlignCenter, text);
            painter->restore();
            return;
        }

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const bool outgoing = msg.direction == QLatin1String("out");
        const QRect rowRect = option.rect.adjusted(16, 2, -16, -2);
        const int avatarSide = 36;
        const int gap = 8;
        const int bubbleMax = qMin(420, qMax(220, rowRect.width() - avatarSide - gap - 72));
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;

        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setTextWidth(qMax(1, bubbleMax - 24));
        doc.setPlainText(displayText);
        const QSizeF docSize = doc.size();
        const int imageH = messageImageHeight(msg, bubbleMax);
        QFontMetrics fm(option.font);
        const QRect textBounds = fm.boundingRect(QRect(0, 0, bubbleMax - 24, 10000),
                                                 Qt::TextWordWrap,
                                                 displayText);
        const int naturalTextW = displayText.trimmed().isEmpty() ? 28 : qBound(28, textBounds.width() + 2, bubbleMax - 24);
        const int naturalImageW = imageH > 0 ? qMin(bubbleMax - 24, 360) : 0;
        const int minBubbleW = displayText.trimmed().isEmpty() && imageH == 0 ? 72 : 96;
        const int bubbleW = qMin(bubbleMax, qMax(minBubbleW, qMax(naturalTextW, naturalImageW) + 24));
        const int metaH = messageMetaHeight(msg);
        const int bubbleH = messageBubbleHeight(msg, bubbleW, option.font);
        const int totalBubbleH = bubbleH + metaH;
        const int avatarX = outgoing ? rowRect.right() - avatarSide : rowRect.left();
        const int bubbleX = outgoing ? avatarX - gap - bubbleW : avatarX + avatarSide + gap;
        const int topY = rowRect.top() + qMax(0, (rowRect.height() - totalBubbleH) / 2);

        const QRect avatarRect(avatarX, topY + metaH, avatarSide, avatarSide);
        painter->setPen(Qt::NoPen);
        painter->setBrush(outgoing ? QColor(QStringLiteral("#5C8DF6")) : QColor(QStringLiteral("#E5E7EB")));
        painter->drawRoundedRect(avatarRect, avatarSide / 2, avatarSide / 2);
        const QPixmap& avatarPixmap = outgoing ? m_selfAvatarPixmap : m_customerAvatarPixmap;
        if (!avatarPixmap.isNull())
            painter->drawPixmap(avatarRect, avatarPixmap);
        painter->setPen(outgoing ? Qt::white : QColor(QStringLiteral("#64748B")));
        painter->drawText(avatarRect, Qt::AlignCenter, outgoing ? QStringLiteral("我") : QStringLiteral("客"));

        if (outgoing) {
            const QRect metaRect(rowRect.left(), topY, bubbleX + bubbleW - rowRect.left(), metaH);
            if (metaH > 0) {
                const QString timeStr = msg.createdAt.isValid() ? msg.createdAt.toString(QStringLiteral("HH:mm")) : QString();
                const QString nick = m_selfDisplayName.trimmed().isEmpty() ? QStringLiteral("我") : m_selfDisplayName.trimmed();
                const int nickWidth = qMin(160, fm.horizontalAdvance(nick) + 4);
                const int timeWidth = timeStr.isEmpty() ? 0 : qMin(92, fm.horizontalAdvance(timeStr) + 4);
                if (!timeStr.isEmpty()) {
                    painter->setPen(QColor(QStringLiteral("#6B7280")));
                    painter->drawText(QRect(metaRect.right() - nickWidth - timeWidth - 8, metaRect.top(),
                                            timeWidth, metaRect.height()),
                                      Qt::AlignRight | Qt::AlignVCenter, timeStr);
                }
                painter->setPen(QColor(QStringLiteral("#374151")));
                painter->drawText(QRect(metaRect.right() - nickWidth, metaRect.top(), nickWidth, metaRect.height()),
                                  Qt::AlignRight | Qt::AlignVCenter, nick);
            }
        } else if (metaH > 0) {
            painter->setPen(QColor(QStringLiteral("#6B7280")));
            QString meta;
            if (!msg.senderName.isEmpty())
                meta = msg.senderName;
            if (msg.createdAt.isValid()) {
                const QString t = msg.createdAt.toString(QStringLiteral("HH:mm"));
                meta = meta.isEmpty() ? t : meta + QStringLiteral("  ") + t;
            } else if (!msg.originalTimestamp.isEmpty()) {
                meta = msg.senderName.isEmpty() ? msg.originalTimestamp : msg.senderName + QStringLiteral("  ") + msg.originalTimestamp;
            }
            painter->drawText(QRect(bubbleX, topY, bubbleW + 120, metaH),
                              Qt::AlignLeft | Qt::AlignVCenter, meta);
        }

        QRect bubbleRect(bubbleX, topY + metaH, bubbleW, bubbleH);
        QColor bubbleColor = outgoing ? QColor(QStringLiteral("#D9E9FF")) : QColor(QStringLiteral("#FFFFFF"));
        painter->setPen(QColor(QStringLiteral("#D1D5DB")));
        painter->setBrush(bubbleColor);
        painter->drawRoundedRect(bubbleRect, 12, 12);

        int contentY = bubbleRect.top() + 10;
        if (!msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath)) {
            QPixmap pm(msg.contentImagePath);
            if (!pm.isNull()) {
                pm = pm.scaledToWidth(qMin(bubbleW - 24, 360), Qt::SmoothTransformation);
                painter->drawPixmap(QRect(bubbleRect.left() + 12, contentY, pm.width(), pm.height()), pm);
                contentY += pm.height() + 8;
            }
        }

        QRect textRect(bubbleRect.left() + 12, contentY, bubbleW - 24, bubbleRect.bottom() - contentY - 20);
        painter->setPen(QColor(QStringLiteral("#111827")));
        doc.setTextWidth(textRect.width());
        painter->translate(textRect.topLeft());
        doc.drawContents(painter, QRectF(0, 0, textRect.width(), textRect.height()));
        painter->translate(-textRect.topLeft());

        QString statusText;
        if (outgoing) {
            if (msg.syncStatus == 10)
                statusText = QStringLiteral("待发送");
            else if (msg.syncStatus == 12)
                statusText = QStringLiteral("发送失败");
            else
                statusText = QStringLiteral("已发送");
            painter->setPen(QColor(QStringLiteral("#6B7280")));
            painter->drawText(QRect(bubbleRect.left() + 12, bubbleRect.bottom() - 16, bubbleW - 24, 14),
                              Qt::AlignLeft | Qt::AlignVCenter, statusText);
            if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
                painter->drawText(QRect(bubbleRect.left() + 12, bubbleRect.bottom() + 2, bubbleW - 24, 24),
                                  Qt::AlignLeft | Qt::AlignTop, msg.errorReason);
            }
        }

        painter->restore();
    }

private:
    static bool hasMessageImage(const MessageRecord& msg)
    {
        return !msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath);
    }

    static int messageMetaHeight(const MessageRecord& msg)
    {
        return (!msg.senderName.isEmpty() || msg.createdAt.isValid() || !msg.originalTimestamp.isEmpty()) ? 18 : 0;
    }

    static int messageImageHeight(const MessageRecord& msg, int bubbleWidth)
    {
        if (!hasMessageImage(msg))
            return 0;
        QPixmap pm(msg.contentImagePath);
        if (pm.isNull())
            return 0;
        const int maxW = qMin(360, qMax(1, bubbleWidth - 24));
        if (pm.width() <= maxW)
            return pm.height();
        return qMax(1, qRound(pm.height() * (qreal(maxW) / qreal(pm.width()))));
    }

    static int messageBubbleHeight(const MessageRecord& msg, int bubbleWidth, const QFont& font)
    {
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;
        QTextDocument doc;
        doc.setDefaultFont(font);
        doc.setTextWidth(qMax(1, bubbleWidth - 24));
        doc.setPlainText(displayText);
        const int textH = qMax(QFontMetrics(font).height(), qCeil(doc.size().height()));
        int h = 8 + textH + 10;
        const int imageH = messageImageHeight(msg, bubbleWidth);
        if (imageH > 0)
            h += imageH + 6;
        h += 12;
        return qMax(36, h);
    }

    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    QPixmap m_customerAvatarPixmap;
};

class ModernMessageItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ModernMessageItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setSelfProfile(const QString& displayName, const QPixmap& avatar)
    {
        m_selfDisplayName = displayName;
        m_selfAvatarPixmap = avatar;
    }

    void setCustomerAvatar(const QPixmap& avatar)
    {
        m_customerAvatarPixmap = avatar;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.data(MessageListModel::IsSeparatorRole).toBool())
            return { qMax(320, option.rect.width()), 48 };

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const QFont bodyFont = bodyTextFont(option.font);
        const QFont statusFont = statusTextFont(option.font);
        const int bubbleMax = qMin(360, qMax(180, option.rect.width() - 170));
        const int bubbleWidth = messageBubbleWidth(msg, bubbleMax, bodyFont);
        const int h = messageMetaHeight(msg) + messageBubbleHeight(msg, bubbleWidth, bodyFont, statusFont) + 12;
        return { qMax(320, option.rect.width()), qMax(60, h) };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if (index.data(MessageListModel::IsSeparatorRole).toBool()) {
            paintDateSeparator(painter, option, index.data(MessageListModel::SeparatorDateRole).toDate());
            painter->restore();
            return;
        }

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const bool outgoing = msg.direction == QLatin1String("out");
        const QRect rowRect = option.rect.adjusted(24, 3, -24, -3);
        const int avatarSide = 40;
        const int gap = 12;
        const int bubbleMax = qMin(360, qMax(180, rowRect.width() - avatarSide - gap - 96));
        const QFont bodyFont = bodyTextFont(option.font);
        const QFont metaFont = metaTextFont(option.font);
        const QFont statusFont = statusTextFont(option.font);
        const int bubbleW = messageBubbleWidth(msg, bubbleMax, bodyFont);
        const int metaH = messageMetaHeight(msg);
        const int bubbleH = messageBubbleHeight(msg, bubbleW, bodyFont, statusFont);
        const int totalH = metaH + bubbleH;
        const int avatarX = outgoing ? rowRect.right() - avatarSide : rowRect.left();
        const int bubbleX = outgoing ? avatarX - gap - bubbleW : avatarX + avatarSide + gap;
        const int topY = rowRect.top() + qMax(0, (rowRect.height() - totalH) / 2);

        paintAvatar(painter, QRect(avatarX, topY + metaH, avatarSide, avatarSide), outgoing, option.font);
        paintMeta(painter, QRect(bubbleX, topY, bubbleW, metaH), msg, outgoing, metaFont);

        const QRect bubbleRect(bubbleX, topY + metaH, bubbleW, bubbleH);
        painter->setPen(outgoing ? QColor(QStringLiteral("#20B8E8")) : QColor(QStringLiteral("#E5E7EB")));
        painter->setBrush(outgoing ? QColor(QStringLiteral("#20B8E8")) : QColor(QStringLiteral("#FFFFFF")));
        painter->drawRoundedRect(bubbleRect, 13, 13);

        paintMessageContent(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);

        if (outgoing)
            paintSendStatus(painter, bubbleRect, bubbleW, msg, statusFont);

        painter->restore();
    }

private:
    static QFont bodyTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(10.5, f.pointSizeF() + 1.0));
        else
            f.setPixelSize(qMax(14, f.pixelSize() > 0 ? f.pixelSize() + 1 : 14));
        return f;
    }

    static QFont metaTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(8.5, f.pointSizeF() - 0.5));
        else
            f.setPixelSize(qMax(11, f.pixelSize() > 0 ? f.pixelSize() - 1 : 11));
        return f;
    }

    static QFont statusTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(8.5, f.pointSizeF() - 0.5));
        else
            f.setPixelSize(qMax(11, f.pixelSize() > 0 ? f.pixelSize() - 1 : 11));
        return f;
    }

    static int messageMetaHeight(const MessageRecord& msg)
    {
        return (!msg.senderName.isEmpty() || msg.createdAt.isValid() || !msg.originalTimestamp.isEmpty()) ? 18 : 0;
    }

    static QString normalizedContentType(const MessageRecord& msg)
    {
        return msg.contentType.trimmed().toLower();
    }

    static bool isImageLikeMessage(const MessageRecord& msg)
    {
        const QString type = normalizedContentType(msg);
        return type == QLatin1String("image") || type == QLatin1String("emoji");
    }

    static bool isEmojiMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("emoji");
    }

    static bool isFileMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("file");
    }

    static bool isVideoMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("video");
    }

    static bool isMediaCardMessage(const MessageRecord& msg)
    {
        return isFileMessage(msg) || isVideoMessage(msg);
    }

    static QStringList contentParts(const MessageRecord& msg)
    {
        return msg.content.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    }

    static QString mediaTitle(const MessageRecord& msg)
    {
        const QStringList parts = contentParts(msg);
        if (isFileMessage(msg) && parts.size() >= 2)
            return parts.at(1);
        if (isVideoMessage(msg))
            return QStringLiteral("视频");
        const QFileInfo info(msg.contentImagePath);
        if (info.exists() && !info.fileName().isEmpty())
            return info.fileName();
        if (!msg.content.trimmed().isEmpty())
            return msg.content.trimmed();
        return isFileMessage(msg) ? QStringLiteral("文件") : QStringLiteral("视频");
    }

    static QString mediaDetail(const MessageRecord& msg)
    {
        const QStringList parts = contentParts(msg);
        if (isFileMessage(msg) && parts.size() >= 3) {
            QStringList detail = parts.mid(2);
            return detail.join(QStringLiteral(" "));
        }
        if (isVideoMessage(msg) && parts.size() >= 2)
            return parts.mid(1).join(QStringLiteral(" "));
        const QFileInfo info(msg.contentImagePath);
        if (info.exists() && info.isFile()) {
            const qint64 bytes = info.size();
            if (bytes >= 1024 * 1024)
                return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
            if (bytes >= 1024)
                return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
        }
        return msg.content.trimmed();
    }

    static QPixmap loadPreviewPixmap(const MessageRecord& msg)
    {
        if (msg.contentImagePath.isEmpty() || !QFile::exists(msg.contentImagePath))
            return {};
        QPixmap pm(msg.contentImagePath);
        return pm.isNull() ? QPixmap() : pm;
    }

    static QSize messageImageSize(const MessageRecord& msg, int bubbleWidth)
    {
        if (!isImageLikeMessage(msg))
            return {};
        QPixmap pm = loadPreviewPixmap(msg);
        if (pm.isNull())
            return {};
        const int maxW = isEmojiMessage(msg) ? qMin(180, qMax(1, bubbleWidth - 24))
                                             : qMin(336, qMax(1, bubbleWidth - 24));
        const int maxH = isEmojiMessage(msg) ? 180 : 260;
        const QSize bounded = pm.size().scaled(maxW, maxH, Qt::KeepAspectRatio);
        return { qMax(1, bounded.width()), qMax(1, bounded.height()) };
    }

    static QString messageDisplayText(const MessageRecord& msg)
    {
        if (isMediaCardMessage(msg))
            return {};
        if (isImageLikeMessage(msg) && !loadPreviewPixmap(msg).isNull())
            return {};
        if (!msg.content.trimmed().isEmpty())
            return msg.content;
        if (msg.contentImagePath.isEmpty())
            return QStringLiteral("暂未识别到文本");
        if (!QFile::exists(msg.contentImagePath))
            return QStringLiteral("图片文件不存在");
        QPixmap pm(msg.contentImagePath);
        if (pm.isNull())
            return QStringLiteral("图片加载失败");
        return QStringLiteral("图片内容待识别");
    }

    static bool messageDisplayTextIsHint(const MessageRecord& msg)
    {
        return !isMediaCardMessage(msg) && msg.content.trimmed().isEmpty();
    }

    static int textHeightForWidth(const QString& text, int width, const QFont& font)
    {
        QFontMetrics fm(font);
        const QRect bounds = fm.boundingRect(QRect(0, 0, qMax(1, width), 10000),
                                             Qt::TextWordWrap | Qt::TextExpandTabs,
                                             text);
        return qMax(fm.height(), bounds.height());
    }

    static int textNaturalWidth(const QString& text, int width, const QFont& font)
    {
        QFontMetrics fm(font);
        const QRect bounds = fm.boundingRect(QRect(0, 0, qMax(1, width), 10000),
                                             Qt::TextWordWrap | Qt::TextExpandTabs,
                                             text);
        return qBound(28, bounds.width() + 2, qMax(28, width));
    }

    static int messageBubbleWidth(const MessageRecord& msg, int bubbleMax, const QFont& bodyFont)
    {
        if (isFileMessage(msg))
            return qMin(bubbleMax, qMax(240, qMin(320, bubbleMax)));
        if (isVideoMessage(msg))
            return qMin(bubbleMax, qMax(220, qMin(300, bubbleMax)));

        const int innerMax = qMax(1, bubbleMax - 24);
        const QString displayText = messageDisplayText(msg);
        const QSize imageSize = messageImageSize(msg, bubbleMax);
        const int naturalTextW = textNaturalWidth(displayText, innerMax, bodyFont);
        const int minBubbleW = messageDisplayTextIsHint(msg) ? 112 : 84;
        return qMin(bubbleMax, qMax(minBubbleW, qMax(naturalTextW, imageSize.width()) + 24));
    }

    static int messageBubbleHeight(const MessageRecord& msg, int bubbleWidth,
                                   const QFont& bodyFont, const QFont& statusFont)
    {
        const QString displayText = messageDisplayText(msg);
        const QSize imageSize = messageImageSize(msg, bubbleWidth);
        if (isFileMessage(msg))
            return (msg.direction == QLatin1String("out") ? 84 : 76)
                   + messageStatusBlockHeight(msg, statusFont);
        if (isVideoMessage(msg)) {
            const QPixmap preview = loadPreviewPixmap(msg);
            const int previewH = preview.isNull() ? 112 : 150;
            return previewH + 44 + messageStatusBlockHeight(msg, statusFont);
        }

        const int textH = displayText.isEmpty() ? 0 : textHeightForWidth(displayText, bubbleWidth - 24, bodyFont);
        int h = 8;
        if (imageSize.height() > 0)
            h += imageSize.height() + (displayText.isEmpty() ? 0 : 6);
        h += textH;
        if (msg.direction == QLatin1String("out"))
            h += messageStatusBlockHeight(msg, statusFont);
        h += 7;
        return qMax(msg.direction == QLatin1String("out") ? 46 : 40, h);
    }

    static int messageStatusBlockHeight(const MessageRecord& msg, const QFont& statusFont)
    {
        if (msg.direction != QLatin1String("out"))
            return 0;
        const int lineH = QFontMetrics(statusFont).height();
        return lineH + 5 + ((msg.syncStatus == 12 && !msg.errorReason.isEmpty()) ? lineH + 3 : 0);
    }

    void paintDateSeparator(QPainter* painter, const QStyleOptionViewItem& option, const QDate& date) const
    {
        const QString text = date == QDate::currentDate()
                                 ? QStringLiteral("今天")
                                 : date.toString(QStringLiteral("yyyy/MM/dd"));
        QFontMetrics fm(option.font);
        const QSize textSize(fm.horizontalAdvance(text) + 24, 28);
        const QRect pill(QPoint(option.rect.center().x() - textSize.width() / 2,
                                option.rect.center().y() - textSize.height() / 2),
                         textSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#E5E7EB")));
        painter->drawRoundedRect(pill, 14, 14);
        painter->setPen(QColor(QStringLiteral("#6B7280")));
        painter->drawText(pill, Qt::AlignCenter, text);
    }

    void paintAvatar(QPainter* painter, const QRect& avatarRect, bool outgoing, const QFont& baseFont) const
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#20B8E8")));
        painter->drawEllipse(avatarRect);
        const QPixmap& avatarPixmap = outgoing ? m_selfAvatarPixmap : m_customerAvatarPixmap;
        if (!avatarPixmap.isNull())
            painter->drawPixmap(avatarRect, avatarPixmap);
        QFont avatarFont(baseFont);
        avatarFont.setBold(true);
        painter->setFont(avatarFont);
        painter->setPen(Qt::white);
        painter->drawText(avatarRect, Qt::AlignCenter, outgoing ? QStringLiteral("我") : QStringLiteral("客"));
    }

    void paintMeta(QPainter* painter, const QRect& metaRect, const MessageRecord& msg,
                   bool outgoing, const QFont& metaFont) const
    {
        if (metaRect.height() <= 0)
            return;

        painter->setFont(metaFont);
        const QFontMetrics fm(metaFont);
        QString timeStr;
        if (msg.createdAt.isValid())
            timeStr = msg.createdAt.toString(QStringLiteral("HH:mm"));
        else if (!msg.originalTimestamp.isEmpty())
            timeStr = msg.originalTimestamp;

        QString name = outgoing ? m_selfDisplayName.trimmed() : msg.senderName.trimmed();
        if (name.isEmpty())
            name = outgoing ? QStringLiteral("我") : QStringLiteral("客户");

        painter->setPen(QColor(QStringLiteral("#6B7280")));
        if (outgoing) {
            const int nameW = qMin(160, fm.horizontalAdvance(name) + 4);
            const int timeW = timeStr.isEmpty() ? 0 : qMin(92, fm.horizontalAdvance(timeStr) + 4);
            if (!timeStr.isEmpty()) {
                painter->drawText(QRect(metaRect.right() - nameW - timeW - 8, metaRect.top(),
                                        timeW, metaRect.height()),
                                  Qt::AlignRight | Qt::AlignVCenter, timeStr);
            }
            painter->drawText(QRect(metaRect.right() - nameW, metaRect.top(), nameW, metaRect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, name);
            return;
        }

        QString meta = name;
        if (!timeStr.isEmpty())
            meta += QStringLiteral("  ") + timeStr;
        painter->drawText(metaRect.adjusted(0, 0, 120, 0), Qt::AlignLeft | Qt::AlignVCenter, meta);
    }

    void paintMessageContent(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                             const MessageRecord& msg, bool outgoing,
                             const QFont& bodyFont, const QFont& statusFont) const
    {
        if (isFileMessage(msg)) {
            paintFileCard(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);
            return;
        }
        if (isVideoMessage(msg)) {
            paintVideoCard(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);
            return;
        }

        int contentY = bubbleRect.top() + 8;
        const QSize imageSize = messageImageSize(msg, bubbleW);
        if (imageSize.height() > 0) {
            QPixmap pm(msg.contentImagePath);
            pm = pm.scaled(imageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(QRect(bubbleRect.left() + 12, contentY, pm.width(), pm.height()), pm);
            contentY += pm.height() + 6;
        }

        const int statusReserve = outgoing ? messageStatusBlockHeight(msg, statusFont) + 3 : 7;
        const QRect textRect(bubbleRect.left() + 12, contentY,
                             bubbleW - 24, qMax(1, bubbleRect.bottom() - contentY - statusReserve));
        painter->setFont(bodyFont);
        if (messageDisplayTextIsHint(msg))
            painter->setPen(outgoing ? QColor(QStringLiteral("#E0F2FE")) : QColor(QStringLiteral("#9CA3AF")));
        else
            painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextExpandTabs,
                          messageDisplayText(msg));
    }

    void paintFileCard(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                       const MessageRecord& msg, bool outgoing,
                       const QFont& bodyFont, const QFont& statusFont) const
    {
        const QRect contentRect = bubbleRect.adjusted(12, 10, -12, -(outgoing ? messageStatusBlockHeight(msg, statusFont) + 7 : 10));
        const QRect iconRect(contentRect.left(), contentRect.top() + 4, 38, 44);

        painter->setPen(Qt::NoPen);
        painter->setBrush(outgoing ? QColor(QStringLiteral("#DFF7FF")) : QColor(QStringLiteral("#E0F2FE")));
        painter->drawRoundedRect(iconRect, 6, 6);
        painter->setBrush(outgoing ? QColor(QStringLiteral("#0EA5E9")) : QColor(QStringLiteral("#0284C7")));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 10, iconRect.width() - 16, 4));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 20, iconRect.width() - 16, 4));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 30, iconRect.width() - 22, 4));

        const QRect textRect(iconRect.right() + 12, contentRect.top(),
                             qMax(1, contentRect.right() - iconRect.right() - 12),
                             contentRect.height());
        QFont titleFont(bodyFont);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        const QFontMetrics titleFm(titleFont);
        painter->drawText(QRect(textRect.left(), textRect.top() + 2, textRect.width(), titleFm.height() + 2),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          titleFm.elidedText(mediaTitle(msg), Qt::ElideMiddle, textRect.width()));

        const QString detail = mediaDetail(msg);
        if (!detail.isEmpty()) {
            painter->setFont(statusFont);
            painter->setPen(outgoing ? QColor(QStringLiteral("#E0F2FE")) : QColor(QStringLiteral("#6B7280")));
            const QFontMetrics detailFm(statusFont);
            painter->drawText(QRect(textRect.left(), textRect.top() + titleFm.height() + 8,
                                    textRect.width(), detailFm.height() + 2),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              detailFm.elidedText(detail, Qt::ElideRight, textRect.width()));
        }
    }

    void paintVideoCard(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                        const MessageRecord& msg, bool outgoing,
                        const QFont& bodyFont, const QFont& statusFont) const
    {
        const int statusReserve = outgoing ? messageStatusBlockHeight(msg, statusFont) : 0;
        const QRect previewRect = bubbleRect.adjusted(10, 10, -10, -(44 + statusReserve));
        QPixmap preview = loadPreviewPixmap(msg);

        painter->setPen(Qt::NoPen);
        if (!preview.isNull()) {
            preview = preview.scaled(previewRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QRect src((preview.width() - previewRect.width()) / 2,
                            (preview.height() - previewRect.height()) / 2,
                            previewRect.width(),
                            previewRect.height());
            painter->drawPixmap(previewRect, preview, src);
            painter->fillRect(previewRect, QColor(17, 24, 39, 80));
        } else {
            painter->setBrush(outgoing ? QColor(QStringLiteral("#DFF7FF")) : QColor(QStringLiteral("#F3F4F6")));
            painter->drawRoundedRect(previewRect, 8, 8);
        }

        const QPoint center = previewRect.center();
        QPolygon play;
        play << QPoint(center.x() - 8, center.y() - 12)
             << QPoint(center.x() - 8, center.y() + 12)
             << QPoint(center.x() + 14, center.y());
        painter->setBrush(!preview.isNull() || outgoing ? QColor(QStringLiteral("#FFFFFF"))
                                                         : QColor(QStringLiteral("#0EA5E9")));
        painter->drawPolygon(play);

        const QRect textRect(bubbleRect.left() + 12, previewRect.bottom() + 6,
                             bubbleW - 24, 32);
        QFont titleFont(bodyFont);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        const QFontMetrics titleFm(titleFont);
        const QString detail = mediaDetail(msg);
        QString title = mediaTitle(msg);
        if (!detail.isEmpty() && detail != title)
            title += QStringLiteral("  ") + detail;
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          titleFm.elidedText(title, Qt::ElideRight, textRect.width()));
    }

    void paintSendStatus(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                         const MessageRecord& msg, const QFont& statusFont) const
    {
        QString statusText;
        QColor statusColor;

        switch (msg.syncStatus) {
        case 10:
            statusText = QStringLiteral("发送中...");
            statusColor = QColor(QStringLiteral("#FBBF24"));
            break;
        case 11:
            statusText = QStringLiteral("已发送");
            statusColor = QColor(QStringLiteral("#E0F2FE"));
            break;
        case 12:
            statusText = QStringLiteral("发送失败");
            statusColor = QColor(QStringLiteral("#FCA5A5"));
            break;
        default:
            statusText = QStringLiteral("已发送");
            statusColor = QColor(QStringLiteral("#E0F2FE"));
            break;
        }

        painter->setFont(statusFont);
        const int statusH = QFontMetrics(statusFont).height();
        const int blockH = messageStatusBlockHeight(msg, statusFont);
        painter->setPen(statusColor);
        const int statusTop = bubbleRect.bottom() - blockH + 4;
        painter->drawText(QRect(bubbleRect.left() + 12, statusTop, bubbleW - 24, statusH),
                          Qt::AlignLeft | Qt::AlignVCenter, statusText);
        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
            painter->setPen(QColor(QStringLiteral("#DC2626")));
            painter->drawText(QRect(bubbleRect.left() + 12, statusTop + statusH + 2, bubbleW - 24, statusH),
                              Qt::AlignLeft | Qt::AlignTop, msg.errorReason);
        }
    }

    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    QPixmap m_customerAvatarPixmap;
};

class MessageListView final : public QListView
{
public:
    using QListView::QListView;

    void setBottomReserve(int px)
    {
        setViewportMargins(0, 0, 0, qMax(0, px));
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

bool pythonServiceStartupBackfillEnabled()
{
    QSettings settings = AppSettings::create();
    return settings.value(QStringLiteral("pythonService/startupBackfillEnabled"), false).toBool();
}

bool isImageFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png")
        || suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("webp")
        || suffix == QLatin1String("bmp")
        || suffix == QLatin1String("gif");
}

bool openLocalFileWithDefaultApp(QWidget* parent, const QString& path, const QString& failureTitle)
{
    if (path.trimmed().isEmpty() || !QFile::exists(path)) {
        showAggregateWarning(parent, failureTitle, QStringLiteral("文件不存在或已被移动。"));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        showAggregateWarning(parent, failureTitle, QStringLiteral("无法打开该文件，请检查系统默认打开程序。"));
        return false;
    }
    return true;
}

class ImagePreviewDialog final : public QDialog
{
public:
    explicit ImagePreviewDialog(const QString& imagePath, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_imagePath(imagePath)
    {
        const QString fileName = QFileInfo(imagePath).fileName();
        setWindowTitle(fileName.isEmpty() ? QStringLiteral("图片预览") : fileName);
        resize(860, 620);
        setMinimumSize(420, 320);

        m_original = QPixmap(imagePath);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);

        auto* toolbar = new QHBoxLayout();
        toolbar->setContentsMargins(0, 0, 0, 0);
        toolbar->setSpacing(8);

        auto* fitBtn = new QToolButton(this);
        fitBtn->setText(QStringLiteral("适配"));
        auto* actualBtn = new QToolButton(this);
        actualBtn->setText(QStringLiteral("1:1"));
        auto* zoomOutBtn = new QToolButton(this);
        zoomOutBtn->setText(QStringLiteral("-"));
        auto* zoomInBtn = new QToolButton(this);
        zoomInBtn->setText(QStringLiteral("+"));
        auto* openBtn = new QToolButton(this);
        openBtn->setText(QStringLiteral("系统打开"));
        auto* closeBtn = new QToolButton(this);
        closeBtn->setText(QStringLiteral("关闭"));

        toolbar->addWidget(fitBtn);
        toolbar->addWidget(actualBtn);
        toolbar->addWidget(zoomOutBtn);
        toolbar->addWidget(zoomInBtn);
        toolbar->addStretch(1);
        toolbar->addWidget(openBtn);
        toolbar->addWidget(closeBtn);
        root->addLayout(toolbar);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(false);
        m_scrollArea->setAlignment(Qt::AlignCenter);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_imageLabel = new QLabel(m_scrollArea);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_scrollArea->setWidget(m_imageLabel);
        root->addWidget(m_scrollArea, 1);

        connect(fitBtn, &QToolButton::clicked, this, [this]() { fitToWindow(); });
        connect(actualBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = 1.0;
            updatePixmap();
        });
        connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = qMax<qreal>(0.1, m_scale * 0.8);
            updatePixmap();
        });
        connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = qMin<qreal>(8.0, m_scale * 1.25);
            updatePixmap();
        });
        connect(openBtn, &QToolButton::clicked, this, [this]() {
            openLocalFileWithDefaultApp(this, m_imagePath, QStringLiteral("打开图片失败"));
        });
        connect(closeBtn, &QToolButton::clicked, this, &QDialog::accept);

        if (m_original.isNull()) {
            m_imageLabel->setText(QStringLiteral("图片不可预览"));
            m_imageLabel->setMinimumSize(360, 220);
        } else {
            m_fitMode = true;
            QTimer::singleShot(0, this, [this]() { fitToWindow(); });
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QDialog::resizeEvent(event);
        if (m_fitMode)
            fitToWindow();
    }

private:
    void fitToWindow()
    {
        if (m_original.isNull() || !m_scrollArea)
            return;
        m_fitMode = true;
        const QSize viewport = m_scrollArea->viewport()->size();
        const QSize fitted = m_original.size().scaled(qMax(1, viewport.width() - 8),
                                                      qMax(1, viewport.height() - 8),
                                                      Qt::KeepAspectRatio);
        m_scale = qMin(qreal(fitted.width()) / qreal(qMax(1, m_original.width())),
                       qreal(fitted.height()) / qreal(qMax(1, m_original.height())));
        updatePixmap();
    }

    void updatePixmap()
    {
        if (m_original.isNull() || !m_imageLabel)
            return;
        const QSize target(qMax(1, qRound(m_original.width() * m_scale)),
                           qMax(1, qRound(m_original.height() * m_scale)));
        m_imageLabel->setPixmap(m_original.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_imageLabel->resize(target);
    }

    QString m_imagePath;
    QPixmap m_original;
    QLabel* m_imageLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    qreal m_scale = 1.0;
    bool m_fitMode = false;
};

QString mediaTimeText(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

class VideoPreviewDialog final : public QDialog
{
public:
    explicit VideoPreviewDialog(const QString& videoPath, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_videoPath(videoPath)
    {
        const QString fileName = QFileInfo(videoPath).fileName();
        setWindowTitle(fileName.isEmpty() ? QStringLiteral("视频预览") : fileName);
        resize(860, 620);
        setMinimumSize(420, 320);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);

        m_videoWidget = new QVideoWidget(this);
        m_videoWidget->setMinimumSize(360, 220);
        root->addWidget(m_videoWidget, 1);

        auto* controls = new QHBoxLayout();
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(8);

        m_playButton = new QToolButton(this);
        m_playButton->setText(QStringLiteral("播放"));
        m_positionSlider = new QSlider(Qt::Horizontal, this);
        m_positionSlider->setRange(0, 0);
        m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), this);
        auto* openButton = new QToolButton(this);
        openButton->setText(QStringLiteral("系统打开"));
        auto* closeButton = new QToolButton(this);
        closeButton->setText(QStringLiteral("关闭"));

        controls->addWidget(m_playButton);
        controls->addWidget(m_positionSlider, 1);
        controls->addWidget(m_timeLabel);
        controls->addWidget(openButton);
        controls->addWidget(closeButton);
        root->addLayout(controls);

        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_audioOutput->setVolume(0.7);
        m_player->setAudioOutput(m_audioOutput);
        m_player->setVideoOutput(m_videoWidget);
        m_player->setSource(QUrl::fromLocalFile(videoPath));

        connect(m_playButton, &QToolButton::clicked, this, [this]() {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            else
                m_player->play();
        });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            m_playButton->setText(state == QMediaPlayer::PlayingState ? QStringLiteral("暂停") : QStringLiteral("播放"));
        });
        connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
            m_durationMs = qMax<qint64>(0, duration);
            m_positionSlider->setRange(0, qMax(0, int(m_durationMs / 1000)));
            updateTimeLabel(m_player->position());
        });
        connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
            if (!m_sliderPressed)
                m_positionSlider->setValue(qBound(0, int(position / 1000), m_positionSlider->maximum()));
            updateTimeLabel(position);
        });
        connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() { m_sliderPressed = true; });
        connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
            m_sliderPressed = false;
            m_player->setPosition(qint64(m_positionSlider->value()) * 1000);
        });
        connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
            updateTimeLabel(qint64(value) * 1000);
        });
        connect(openButton, &QToolButton::clicked, this, [this]() {
            openLocalFileWithDefaultApp(this, m_videoPath, QStringLiteral("打开视频失败"));
        });
        connect(closeButton, &QToolButton::clicked, this, &QDialog::accept);
    }

    ~VideoPreviewDialog() override
    {
        if (m_player)
            m_player->stop();
    }

private:
    void updateTimeLabel(qint64 position)
    {
        if (m_timeLabel)
            m_timeLabel->setText(QStringLiteral("%1 / %2").arg(mediaTimeText(position), mediaTimeText(m_durationMs)));
    }

    QString m_videoPath;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QToolButton* m_playButton = nullptr;
    QSlider* m_positionSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    qint64 m_durationMs = 0;
    bool m_sliderPressed = false;
};

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
    m_conversationListModel = new ConversationListModel(this);
    m_messageListModel = new MessageListModel(this);
    setupUI();
    m_conversationService = new ConversationAppService();
    m_aiChatService = new AiChatAppService(this);
    setupStyles();
    loadSelfBubbleIdentity();
    connectSignals();
    refreshConversationList();
    restoreLastSelectedConversation();
    QTimer::singleShot(0, this, [this]() {
        auto& ipc = Ipc::IpcService::instance();
        m_pythonServiceAvailable = ipc.isServiceAvailable();
        refreshPlatformListenStateFromService();
        if (m_pythonServiceAvailable && pythonServiceStartupBackfillEnabled())
            backfillFromPythonService();
    });
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
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate))
        delegate->setSelfProfile(m_selfDisplayName, m_selfAvatarPixmap);

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
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate))
        delegate->setCustomerAvatar(m_customerDefaultAvatarPixmap);
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

void AggregateChatForm::backfillFromPythonService()
{
    if (!pythonServiceStartupBackfillEnabled()) {
        qInfo() << "[AggregateChatForm] python service startup backfill disabled by settings";
        return;
    }
    if (m_pythonBackfillInProgress)
        return;

    m_pythonBackfillInProgress = true;
    ConversationDao conversationDao;
    const QStringList platforms = {
        QStringLiteral("wechat"),
        QStringLiteral("qianniu"),
    };
    for (const QString& platform : platforms) {
        Ipc::ResponseStatus replayStatus = Ipc::ResponseStatus::Error;
        QString replayError;
        const QString replayCursor = conversationDao.rpaReplayCursor(platform);
        const QJsonObject replay = Ipc::IpcService::instance().fetchPlatformReplay(
            platform,
            replayCursor,
            200,
            5000,
            &replayStatus,
            &replayError);
        const int replayedEvents = replayStatus == Ipc::ResponseStatus::Success
            ? Ipc::IpcService::instance().dispatchPlatformReplayEvents(replay)
            : 0;
        const QString nextReplayCursor = replay.value(QStringLiteral("cursor")).toString().trimmed();
        if (replayStatus == Ipc::ResponseStatus::Success && !nextReplayCursor.isEmpty())
            conversationDao.setRpaReplayCursor(platform, nextReplayCursor);
        qInfo() << "[AggregateChatForm] platform replay backfill"
                << "platform=" << platform
                << "cursor=" << replayCursor
                << "status=" << Ipc::toString(replayStatus)
                << "error=" << replayError
                << "events=" << replay.value(QStringLiteral("event_count")).toInt()
                << "dispatched=" << replayedEvents
                << "nextCursor=" << nextReplayCursor;

        Ipc::ResponseStatus snapshotStatus = Ipc::ResponseStatus::Error;
        QString snapshotError;
        const QString cursor = conversationDao.snapshotCursor(platform);
        const QJsonObject snapshot = Ipc::IpcService::instance().fetchCacheSnapshot(
            platform,
            100,
            200,
            cursor,
            5000,
            &snapshotStatus,
            &snapshotError);
        qInfo() << "[AggregateChatForm] cache snapshot backfill"
                << "platform=" << platform
                << "cursor=" << cursor
                << "status=" << Ipc::toString(snapshotStatus)
                << "error=" << snapshotError
                << "conversations=" << snapshot.value(QStringLiteral("conversation_count")).toInt()
                << "messages=" << snapshot.value(QStringLiteral("message_count")).toInt();
        if (snapshotStatus == Ipc::ResponseStatus::Success)
            applyCacheSnapshotToLocalCache(snapshot);
    }
    ConversationManager::instance().reloadFromLocalCache();
    reloadFromLocalCache();
    m_pythonBackfillInProgress = false;
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
    installRightBarToggleEventFilter(m_messageView);
    if (m_messageView)
        installRightBarToggleEventFilter(m_messageView->viewport());
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
    if (!m_chatInputOverlayHost || !m_messageView || !m_chatInputPanel)
        return;
    const int w = m_chatInputOverlayHost->width();
    const int h = m_chatInputOverlayHost->height();
    if (w <= 0 || h <= 0)
        return;

    m_chatInputPanel->setFixedWidth(w);
    m_chatInputPanel->adjustSize();
    int ih = m_chatInputPanel->sizeHint().height();
    ih = qBound(72, ih, qMax(72, h - 16));

    m_messageView->setGeometry(0, 0, w, h);
    m_chatInputPanel->setGeometry(0, h - ih, w, ih);
    m_chatInputPanel->raise();
    updateMessageListBottomReserve(ih);
    m_chatInputOverlayHost->update();
}

void AggregateChatForm::updateMessageListBottomReserve(int overlayBottomPx)
{
    if (!m_messageView)
        return;
    const int extra = qMax(0, overlayBottomPx);
    static_cast<MessageListView*>(m_messageView)->setBottomReserve(extra);
}

void AggregateChatForm::connectSignals()
{
    auto& mgr = ConversationManager::instance();

    connect(&mgr, &ConversationManager::conversationListChanged,
            this, &AggregateChatForm::onConversationListChanged);
    connect(&mgr, &ConversationManager::unifiedMessageReceived,
            this, &AggregateChatForm::onUnifiedMessageReceived);
    connect(&mgr, &ConversationManager::unifiedConversationUpdated,
            this, &AggregateChatForm::onUnifiedConversationUpdated);
    connect(&mgr, &ConversationManager::conversationMessagesCleared,
            this, &AggregateChatForm::onConversationMessagesCleared);
    connect(&mgr, &ConversationManager::conversationDeleted,
            this, &AggregateChatForm::onConversationDeleted);
    connect(&mgr, &ConversationManager::messageSendFailed,
            this, [this](int convId, const QString& reason) {
                Q_UNUSED(convId)
                showStatusMessage(QStringLiteral("发送失败: %1").arg(reason), 5000);
            });

    connect(&mgr, &ConversationManager::messageStatusChanged,
            this, &AggregateChatForm::onMessageStatusChanged);

    auto& ipc = Ipc::IpcService::instance();
    connect(&ipc, &Ipc::IpcService::aiSuggestionReceived,
            this, &AggregateChatForm::onIpcAiSuggestionReceived);
    connect(&ipc, &Ipc::IpcService::requestFailed,
            this, &AggregateChatForm::onIpcRequestFailed);
    connect(&ipc, &Ipc::IpcService::serviceStatusChanged, this, [this](bool available) {
        m_pythonServiceAvailable = available;
        refreshPlatformListenStateFromService();
        if (available && pythonServiceStartupBackfillEnabled())
            backfillFromPythonService();
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

    m_btnSimulateMessage = new QToolButton(bar);
    m_btnSimulateMessage->setObjectName(QStringLiteral("aggregateToolBarButton"));
    m_btnSimulateMessage->setText(QStringLiteral("测"));
    m_btnSimulateMessage->setToolTip(QStringLiteral("模拟一条随机平台买家消息"));
    m_btnSimulateMessage->setAccessibleName(QStringLiteral("模拟买家消息"));
    m_btnSimulateMessage->setFixedSize(40, 40);
    m_btnSimulateMessage->setAutoRaise(true);
    m_btnSimulateMessage->setCursor(Qt::PointingHandCursor);
    connect(m_btnSimulateMessage, &QToolButton::clicked,
            this, &AggregateChatForm::onSimulateMessageClicked);
    lay->addWidget(m_btnSimulateMessage, 0, Qt::AlignHCenter);

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

void AggregateChatForm::onSimulateMessageClicked()
{
    auto* router = ConversationManager::instance().router();
    if (!router) {
        showStatusMessage(QStringLiteral("模拟失败：消息路由未初始化"), 4000);
        return;
    }

    auto* adapter = qobject_cast<SimPlatformAdapter*>(router->adapter(QStringLiteral("simulator")));
    if (!adapter) {
        showStatusMessage(QStringLiteral("模拟失败：模拟平台未就绪"), 4000);
        return;
    }

    adapter->simulateRandomPlatformIncomingMessage();
    showStatusMessage(QStringLiteral("已模拟一条买家消息"), 2500);
}

QStringList AggregateChatForm::selectedPlatformListenTargets() const
{
    QStringList platforms;
    if (m_chkListenWechat && m_chkListenWechat->isChecked())
        platforms.append(QStringLiteral("wechat"));
    if (m_chkListenQianniu && m_chkListenQianniu->isChecked())
        platforms.append(QStringLiteral("qianniu"));
    return platforms;
}

void AggregateChatForm::updatePlatformListenStatusLabel()
{
    if (!m_platformListenStatusLabel)
        return;

    if (!m_pythonServiceAvailable) {
        m_platformListenStatusLabel->setText(QStringLiteral("Python 服务未连接"));
        return;
    }

    QStringList listening;
    if (m_serviceListeningPlatforms.contains(QStringLiteral("wechat"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("wechat"))) {
        listening.append(QStringLiteral("微信"));
    }
    if (m_serviceListeningPlatforms.contains(QStringLiteral("qianniu"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("qianniu"))) {
        listening.append(QStringLiteral("千牛"));
    }
    if (m_registeredListenPlatforms.isEmpty()) {
        m_platformListenStatusLabel->setText(QStringLiteral("暂无已注册平台"));
        return;
    }
    m_platformListenStatusLabel->setText(
        listening.isEmpty()
            ? QStringLiteral("未监听平台")
            : QStringLiteral("监听中：%1").arg(listening.join(QStringLiteral("、"))));
}

void AggregateChatForm::setPlatformListenControlsEnabled(bool enabled)
{
    if (m_chkListenWechat)
        m_chkListenWechat->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("wechat")));
    if (m_chkListenQianniu)
        m_chkListenQianniu->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("qianniu")));
    if (m_btnStartPlatformListening)
        m_btnStartPlatformListening->setEnabled(enabled);
    if (m_btnStopPlatformListening)
        m_btnStopPlatformListening->setEnabled(enabled);
}

void AggregateChatForm::refreshPlatformListenStateFromService()
{
    auto& ipc = Ipc::IpcService::instance();
    m_pythonServiceAvailable = ipc.isServiceAvailable();
    if (!m_pythonServiceAvailable) {
        m_registeredListenPlatforms.clear();
        m_serviceListeningPlatforms.clear();
        setPlatformListenControlsEnabled(false);
        updatePlatformListenStatusLabel();
        return;
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    const QJsonObject response = ipc.fetchPlatformStatuses(3000, &status, &error);
    if (status == Ipc::ResponseStatus::Success
        && response.value(QStringLiteral("status")).toString() == QLatin1String("success")) {
        m_registeredListenPlatforms.clear();
        m_serviceListeningPlatforms.clear();
        const QJsonArray platforms = response.value(QStringLiteral("platforms")).toArray();
        for (const QJsonValue& value : platforms) {
            const QJsonObject item = value.toObject();
            const QString platform = item.value(QStringLiteral("platform")).toString().trimmed().toLower();
            if (platform.isEmpty())
                continue;
            if (item.value(QStringLiteral("registered")).toBool(false))
                m_registeredListenPlatforms.insert(platform);
            if (item.value(QStringLiteral("listening")).toBool(false))
                m_serviceListeningPlatforms.insert(platform);
        }
    } else {
        qWarning() << "[AggregateChatForm] fetch platform statuses failed"
                   << Ipc::toString(status) << error;
        m_registeredListenPlatforms = { QStringLiteral("wechat"), QStringLiteral("qianniu") };
        m_serviceListeningPlatforms.clear();
    }

    setPlatformListenControlsEnabled(true);
    updatePlatformListenStatusLabel();
}

void AggregateChatForm::onStartPlatformListeningClicked()
{
    const QStringList platforms = selectedPlatformListenTargets();
    if (platforms.isEmpty()) {
        showStatusMessage(QStringLiteral("请先选择要监听的平台"), 3000);
        return;
    }

    QStringList started;
    for (const QString& platform : platforms) {
        if (ConversationManager::instance().startPlatformListening(platform))
            started.append(platform == QLatin1String("wechat") ? QStringLiteral("微信") : QStringLiteral("千牛"));
    }
    refreshPlatformListenStateFromService();
    if (started.isEmpty()) {
        showStatusMessage(QStringLiteral("平台监听启动失败，请检查 Python 服务"), 5000);
        return;
    }
    showStatusMessage(QStringLiteral("已请求监听：%1").arg(started.join(QStringLiteral("、"))), 4000);
}

void AggregateChatForm::onStopPlatformListeningClicked()
{
    const QStringList platforms = selectedPlatformListenTargets();
    if (platforms.isEmpty()) {
        showStatusMessage(QStringLiteral("请先选择要停止监听的平台"), 3000);
        return;
    }

    QStringList stopped;
    for (const QString& platform : platforms) {
        if (ConversationManager::instance().stopPlatformListening(platform))
            stopped.append(platform == QLatin1String("wechat") ? QStringLiteral("微信") : QStringLiteral("千牛"));
    }
    refreshPlatformListenStateFromService();
    if (stopped.isEmpty()) {
        showStatusMessage(QStringLiteral("平台监听停止失败"), 5000);
        return;
    }
    showStatusMessage(QStringLiteral("已请求停止：%1").arg(stopped.join(QStringLiteral("、"))), 4000);
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

    auto* listenBox = new QFrame(panel);
    listenBox->setObjectName(QStringLiteral("aggregatePlatformListenBox"));
    listenBox->setFrameShape(QFrame::NoFrame);
    auto* listenLayout = new QVBoxLayout(listenBox);
    listenLayout->setContentsMargins(10, 8, 10, 8);
    listenLayout->setSpacing(6);

    auto* listenTitle = new QLabel(QStringLiteral("平台监听"), listenBox);
    listenTitle->setObjectName(QStringLiteral("aggregateModeLabel"));
    listenLayout->addWidget(listenTitle);

    auto* listenCheckRow = new QWidget(listenBox);
    auto* listenCheckLayout = new QHBoxLayout(listenCheckRow);
    listenCheckLayout->setContentsMargins(0, 0, 0, 0);
    listenCheckLayout->setSpacing(10);
    m_chkListenWechat = new QCheckBox(QStringLiteral("微信"), listenCheckRow);
    m_chkListenWechat->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    m_chkListenQianniu = new QCheckBox(QStringLiteral("千牛"), listenCheckRow);
    m_chkListenQianniu->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    listenCheckLayout->addWidget(m_chkListenWechat);
    listenCheckLayout->addWidget(m_chkListenQianniu);
    listenCheckLayout->addStretch(1);
    listenLayout->addWidget(listenCheckRow);

    auto* listenButtonRow = new QWidget(listenBox);
    auto* listenButtonLayout = new QHBoxLayout(listenButtonRow);
    listenButtonLayout->setContentsMargins(0, 0, 0, 0);
    listenButtonLayout->setSpacing(8);
    m_btnStartPlatformListening = new QPushButton(QStringLiteral("开始监听"), listenButtonRow);
    m_btnStartPlatformListening->setObjectName(QStringLiteral("aggregatePlatformListenStartButton"));
    m_btnStartPlatformListening->setCursor(Qt::PointingHandCursor);
    m_btnStopPlatformListening = new QPushButton(QStringLiteral("停止监听"), listenButtonRow);
    m_btnStopPlatformListening->setObjectName(QStringLiteral("aggregatePlatformListenStopButton"));
    m_btnStopPlatformListening->setCursor(Qt::PointingHandCursor);
    listenButtonLayout->addWidget(m_btnStartPlatformListening);
    listenButtonLayout->addWidget(m_btnStopPlatformListening);
    listenLayout->addWidget(listenButtonRow);

    m_platformListenStatusLabel = new QLabel(QStringLiteral("未监听平台"), listenBox);
    m_platformListenStatusLabel->setObjectName(QStringLiteral("aggregatePlatformListenStatus"));
    m_platformListenStatusLabel->setWordWrap(true);
    listenLayout->addWidget(m_platformListenStatusLabel);
    layout->addWidget(listenBox);

    connect(m_btnStartPlatformListening, &QPushButton::clicked,
            this, &AggregateChatForm::onStartPlatformListeningClicked);
    connect(m_btnStopPlatformListening, &QPushButton::clicked,
            this, &AggregateChatForm::onStopPlatformListeningClicked);
    setPlatformListenControlsEnabled(false);
    updatePlatformListenStatusLabel();

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
    m_conversationList = new QListView(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_conversationList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conversationList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conversationList->setUniformItemSizes(false);
    m_conversationList->setMouseTracking(true);
    m_conversationList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_conversationList->setModel(m_conversationListModel);
    m_conversationList->setItemDelegate(new ConversationItemDelegate(m_conversationList));
    connect(m_conversationList, &QListView::clicked,
            this, &AggregateChatForm::onConversationIndexClicked);
    connect(m_conversationList, &QListView::customContextMenuRequested,
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
    m_messageView = new MessageListView(m_chatInputOverlayHost);
    m_messageView->setObjectName("messageScroll");
    m_messageView->setFrameShape(QFrame::NoFrame);
    m_messageView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageView->setFocusPolicy(Qt::NoFocus);
    m_messageView->setUniformItemSizes(false);
    m_messageView->setMouseTracking(true);
    m_messageView->setModel(m_messageListModel);
    m_messageItemDelegate = new ModernMessageItemDelegate(m_messageView);
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate)) {
        delegate->setSelfProfile(m_selfDisplayName, m_selfAvatarPixmap);
        delegate->setCustomerAvatar(m_customerDefaultAvatarPixmap);
    }
    m_messageView->setItemDelegate(m_messageItemDelegate);
    if (QWidget* vp = m_messageView->viewport())
        vp->setObjectName(QStringLiteral("messageScrollViewport"));
    if (QWidget* vp = m_messageView->viewport())
        vp->setAutoFillBackground(false);
    connect(m_messageView, &QListView::clicked, this, [this](const QModelIndex& index) {
        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        openMessageMedia(msg);
    });

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

    m_inputEdit = new QPlainTextEdit(inputInner);
    m_inputEdit->setObjectName("messageInput");
    m_inputEdit->setPlaceholderText(QString());
    m_inputEdit->setToolTip(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_inputEdit->setMaximumHeight(100);
    inputLayout->addWidget(m_inputEdit);

    m_draftSaveTimer = new QTimer(this);
    m_draftSaveTimer->setSingleShot(true);
    m_draftSaveTimer->setInterval(600);
    connect(m_draftSaveTimer, &QTimer::timeout, this, &AggregateChatForm::persistCurrentDraft);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);
    btnRow->addWidget(m_btnAiModelPick, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel = new QLabel(inputInner);
    m_statusLabel->setObjectName(QStringLiteral("aggregateInlineStatus"));
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setMaximumWidth(320);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_statusLabel->setVisible(false);
    btnRow->addWidget(m_statusLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    btnRow->addStretch(1);
    m_btnAiGenerate = new QPushButton(QStringLiteral("生成本条回复"), inputInner);
    m_btnAiGenerate->setObjectName(QStringLiteral("simulateButton"));
    m_btnAiGenerate->setCursor(Qt::PointingHandCursor);
    m_btnAiGenerate->setFixedSize(120, 36);
    m_btnAiGenerate->setVisible(false);
    connect(m_btnAiGenerate, &QPushButton::clicked, this, &AggregateChatForm::onGenerateAiDraftClicked);
    m_btnSend = new QPushButton(QStringLiteral("发送"), inputInner);
    m_btnSend->setObjectName("sendButton");
    m_btnSend->setCursor(Qt::PointingHandCursor);
    m_btnSend->setFixedSize(80, 36);
    connect(m_btnSend, &QPushButton::clicked, this, &AggregateChatForm::onSendClicked);
    btnRow->addWidget(m_btnAiGenerate);
    btnRow->addWidget(m_btnSend);
    inputLayout->addLayout(btnRow);

    inputOuter->addWidget(inputInner, 0);

    m_chatInputOverlayHost->installEventFilter(this);
    connect(m_inputEdit->document(), &QTextDocument::contentsChanged, this, [this]() {
        QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
        if (!m_restoringDraft && m_currentConvId > 0 && m_draftSaveTimer)
            m_draftSaveTimer->start();
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

void AggregateChatForm::syncConversationItemVisualState()
{
    if (!m_conversationList)
        return;
    m_conversationList->viewport()->update();
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
    if (m_messageView) {
        applySolidBackground(m_messageView, centerBg);
        applySolidBackground(m_messageView->viewport(), centerBg);
    }
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

void AggregateChatForm::onMessageStatusChanged(int conversationId, int messageId,
                                               Models::MessageStatus newStatus,
                                               const QString& errorReason)
{
    if (conversationId != m_currentConvId)
        return;

    if (!m_messageListModel)
        return;

    if (m_messageListModel->updateMessageStatus(messageId, newStatus, errorReason)) {
        m_currentMessageSignature = m_messageListModel->signature();
        qDebug() << "[AggregateChatForm] 消息状态已更新 msgId=" << messageId
                 << "status=" << Models::toString(newStatus);

        if (newStatus == Models::MessageStatus::Failed && !errorReason.isEmpty()) {
            showStatusMessage(QStringLiteral("发送失败: %1").arg(errorReason), 5000);
        }
    }
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

void AggregateChatForm::persistCurrentDraft()
{
    if (m_currentConvId <= 0 || !m_inputEdit)
        return;

    ConversationDao dao;
    dao.saveCachedDraft(m_currentConvId, m_inputEdit->toPlainText());
}

void AggregateChatForm::restoreDraftForConversation(int conversationId)
{
    if (!m_inputEdit)
        return;

    ConversationDao dao;
    const QString draft = conversationId > 0 ? dao.cachedDraftForConversation(conversationId) : QString();
    m_restoringDraft = true;
    {
        const QSignalBlocker blocker(m_inputEdit->document());
        m_inputEdit->setPlainText(draft);
    }
    m_restoringDraft = false;
    QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
}

void AggregateChatForm::restoreLastSelectedConversation()
{
    if (!m_conversationListModel || m_conversationListModel->rowCount() <= 0)
        return;

    ConversationDao dao;
    int conversationId = dao.lastSelectedCachedConversationId();
    if (conversationId <= 0 || !m_conversationListModel->containsConversation(conversationId))
        conversationId = m_conversationListModel->conversationIdAt(0);

    if (conversationId > 0)
        showConversation(conversationId);
}

void AggregateChatForm::refreshConversationList()
{
    if (!m_conversationListModel)
        m_conversationListModel = new ConversationListModel(this);

    auto& mgr = ConversationManager::instance();
    MessageDao msgDao;
    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_conversationListModel->setFilters(static_cast<int>(m_currentTab),
                                        static_cast<int>(m_platformFilter),
                                        keyword,
                                        m_pendingStickyConvId);
    m_conversationListModel->setSourceConversations(mgr.allConversations(),
                                                    msgDao.lastCachedDirectionsByConversation());
    renderConversationListFromModel();
    return;
}

void AggregateChatForm::reloadFromLocalCache()
{
    refreshConversationList();

    if (m_currentConvId > 0
        && m_conversationListModel
        && m_conversationListModel->containsConversation(m_currentConvId)) {
        const auto messages = ConversationManager::instance().messages(m_currentConvId);
        if (!m_messageListModel)
            m_messageListModel = new MessageListModel(this);
        m_messageListModel->setConversationMessages(m_currentConvId, messages);
        renderConversationMessagesFromModel();
        m_currentMessageSignature = m_messageListModel->signature();

        const auto conv = m_conversationService
                              ? m_conversationService->conversationById(m_currentConvId)
                              : std::optional<ConversationInfo>();
        if (conv) {
            if (m_chatHeader)
                m_chatHeader->setText(QStringLiteral("%1 (%2)").arg(conv->customerName, conv->platform));
            updateCustomerInfo(*conv);
        }
        scheduleScrollChatToBottom();
        qInfo() << "[AggregateChatForm] reloaded current conversation from local cache:"
                << m_currentConvId << "messages=" << messages.size();
        return;
    }

    restoreLastSelectedConversation();
    if (m_currentConvId <= 0) {
        showCenterEmptyState();
        showRightEmptyState();
    }
    qInfo() << "[AggregateChatForm] reloaded conversation cache from local cache";
}

void AggregateChatForm::reloadFromDatabase()
{
    reloadFromLocalCache();
}

void AggregateChatForm::applyCacheSnapshotToLocalCache(const QJsonObject& snapshot)
{
    if (snapshot.value(QStringLiteral("status")).toString() != QLatin1String("success"))
        return;

    const QString platform = snapshot.value(QStringLiteral("platform")).toString().trimmed().toLower();
    const QString nextCursor = snapshot.value(QStringLiteral("snapshot_cursor")).toString().trimmed();
    const bool fullSnapshot = snapshot.value(QStringLiteral("full_snapshot")).toBool(false);
    const QJsonArray conversations = snapshot.value(QStringLiteral("conversations")).toArray();
    if (conversations.isEmpty()) {
        if (fullSnapshot && !platform.isEmpty()) {
            const int removed = ConversationDao().deleteMissingSnapshotCacheConversations(
                platform,
                {});
            qInfo() << "[AggregateChatForm] cache snapshot empty full sync"
                    << "platform=" << platform
                    << "removedConversations=" << removed;
        }
        if (!platform.isEmpty() && !nextCursor.isEmpty())
            ConversationDao().setSnapshotCursor(platform, nextCursor);
        return;
    }

    ConversationDao conversationDao;
    MessageDao messageDao;
    QSet<QString> keepConversationIds;
    int appliedConversations = 0;
    int appliedMessages = 0;
    int removedConversations = 0;
    int removedMessages = 0;
    for (const QJsonValue& value : conversations) {
        const QJsonObject conversation = value.toObject();
        if (conversation.isEmpty())
            continue;
        const QString platformConversationId =
            conversation.value(QStringLiteral("platform_conversation_id")).toString().trimmed();
        if (!platformConversationId.isEmpty())
            keepConversationIds.insert(platformConversationId);
        const int conversationId = conversationDao.upsertSnapshotCacheConversation(conversation);
        if (conversationId <= 0)
            continue;
        ++appliedConversations;

        QSet<QString> keepPlatformMessageIds;
        QSet<QString> keepClientMessageIds;
        const QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue& messageValue : messages) {
            const QJsonObject message = messageValue.toObject();
            if (message.isEmpty())
                continue;
            const QString platformMessageId = message.value(QStringLiteral("platform_msg_id")).toString().trimmed();
            const QString clientMessageId = message.value(QStringLiteral("client_message_id")).toString().trimmed();
            if (!platformMessageId.isEmpty())
                keepPlatformMessageIds.insert(platformMessageId);
            if (!clientMessageId.isEmpty())
                keepClientMessageIds.insert(clientMessageId);
        }
        removedMessages += messageDao.deleteMissingSnapshotCacheMessages(
            conversationId,
            keepPlatformMessageIds,
            keepClientMessageIds);
        const QString conversationPlatform = conversation.value(QStringLiteral("platform")).toString().trimmed().toLower();
        const QString accountId = conversation.value(QStringLiteral("account_id")).toString().trimmed();
        const QString displayName = conversation.value(QStringLiteral("customer_name")).toString().trimmed();
        for (const QJsonValue& messageValue : messages) {
            const QJsonObject message = messageValue.toObject();
            if (message.isEmpty())
                continue;
            const int messageId = messageDao.upsertSnapshotCacheMessage(conversationId, message);
            if (messageId > 0) {
                ++appliedMessages;
                if (conversationPlatform == QLatin1String("wechat")) {
                    WechatMessageDao wechatDao;
                    wechatDao.createMessageExtension(
                        messageId,
                        conversationId,
                        accountId,
                        platformConversationId,
                        displayName,
                        message.value(QStringLiteral("platform_message_id")).toString(
                            message.value(QStringLiteral("platform_msg_id")).toString()),
                        message);
                } else if (conversationPlatform == QLatin1String("qianniu")) {
                    QianniuConversationDao qianniuDao;
                    qianniuDao.createMessageExtension(
                        messageId,
                        conversationId,
                        accountId,
                        platformConversationId,
                        displayName,
                        message.value(QStringLiteral("platform_message_id")).toString(
                            message.value(QStringLiteral("platform_msg_id")).toString()),
                        message);
                }
            }
        }
    }

    if (fullSnapshot && !platform.isEmpty()) {
        removedConversations = conversationDao.deleteMissingSnapshotCacheConversations(
            platform,
            keepConversationIds);
    }
    if (!platform.isEmpty() && !nextCursor.isEmpty())
        conversationDao.setSnapshotCursor(platform, nextCursor);

    qInfo() << "[AggregateChatForm] cache snapshot applied"
            << "platform=" << platform
            << "fullSnapshot=" << fullSnapshot
            << "conversations=" << appliedConversations
            << "messages=" << appliedMessages
            << "removedConversations=" << removedConversations
            << "removedMessages=" << removedMessages
            << "nextCursor=" << nextCursor;
}

#if 0
    auto& mgr = ConversationManager::instance();
    const auto convs = mgr.allConversations();
    MessageDao msgDao;
    const QHash<int, QString> lastDirs = msgDao.lastCachedDirectionsByConversation();

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
                wantPlat = QStringLiteral("wechat");
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
#endif

void AggregateChatForm::renderConversationListFromModel()
{
    if (!m_conversationList || !m_leftStack || !m_conversationListModel)
        return;

    const int count = m_conversationListModel->rowCount();
    m_leftStack->setCurrentIndex(count > 0 ? 0 : 1);

    if (m_currentConvId > 0 && !m_conversationListModel->containsConversation(m_currentConvId)) {
        const int lost = m_currentConvId;
        persistCurrentDraft();
        m_currentConvId = -1;
        if (m_pendingStickyConvId == lost)
            m_pendingStickyConvId = -1;
        ConversationManager::instance().selectConversation(-1);
        abortAggregateAiRequest();
        showCenterEmptyState();
        showRightEmptyState();
    }

    m_conversationListModel->setSelectedConversationId(m_currentConvId);
    if (m_currentConvId > 0 && count > 0) {
        const QModelIndex idx = m_conversationListModel->indexForConversationId(m_currentConvId);
        if (idx.isValid()) {
            m_conversationList->setCurrentIndex(idx);
            m_conversationList->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    }
    syncConversationItemVisualState();
}

void AggregateChatForm::showConversation(int conversationId)
{
    abortAggregateAiRequest();

    const int prevId = m_currentConvId;
    if (prevId > 0 && prevId != conversationId)
        persistCurrentDraft();
    if (prevId > 0 && prevId != conversationId && m_pendingStickyConvId == prevId)
        m_pendingStickyConvId = -1;

    m_currentConvId = conversationId;
    auto& mgr = ConversationManager::instance();
    mgr.selectConversation(conversationId);

    auto messages = mgr.messages(conversationId);
    if (!m_messageListModel)
        m_messageListModel = new MessageListModel(this);
    m_messageListModel->setConversationMessages(conversationId, messages);
    renderConversationMessagesFromModel();
    m_currentMessageSignature = m_messageListModel->signature();

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
    restoreDraftForConversation(conversationId);
    m_inputEdit->setFocus();

    scheduleScrollChatToBottom();
    updateAggregateAiControlsVisibility();

    tryAggregateAutoReply(conversationId, QStringLiteral("T1"));
}

void AggregateChatForm::renderConversationMessages(const QVector<MessageRecord>& messages)
{
    if (!m_messageListModel)
        return;
    m_messageListModel->setConversationMessages(m_currentConvId, messages);
}

void AggregateChatForm::renderConversationMessagesFromModel()
{
    if (!m_messageView || !m_messageListModel)
        return;
    m_messageView->setModel(m_messageListModel);
    m_messageView->viewport()->update();
}

void AggregateChatForm::appendMessageBubble(const MessageRecord& msg)
{
    if (m_messageListModel)
        m_messageListModel->appendMessage(msg);
    renderConversationMessagesFromModel();
    if (m_messageListModel)
        m_currentMessageSignature = m_messageListModel->signature();
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
    if (m_currentConvId <= 0 || !m_messageView)
        return;

    auto messages = ConversationManager::instance().messages(m_currentConvId);
    const QString newSignature = buildMessageSignature(messages);
    if (newSignature == m_currentMessageSignature)
        return;

    auto* sb = m_messageView->verticalScrollBar();
    const bool wasNearBottom = !sb || sb->value() >= sb->maximum() - 24;
    m_messageListModel->setConversationMessages(m_currentConvId, messages);
    renderConversationMessagesFromModel();
    m_currentMessageSignature = m_messageListModel->signature();
    if (wasNearBottom)
        scheduleScrollChatToBottom();
}

void AggregateChatForm::scrollToBottom()
{
    if (!m_messageView)
        return;
    auto* sb = m_messageView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AggregateChatForm::scheduleScrollChatToBottom()
{
    QTimer::singleShot(100, this, [this]() {
        if (!m_messageView)
            return;
        m_messageView->viewport()->update();
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
    if (m_currentTab != tab && m_pendingStickyConvId > 0)
        m_pendingStickyConvId = -1;
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

void AggregateChatForm::onConversationIndexClicked(const QModelIndex& index)
{
    if (!index.isValid() || !m_conversationListModel)
        return;
    const int convId = m_conversationListModel->data(index, ConversationListModel::ConversationIdRole).toInt();
    if (convId <= 0)
        return;
    showConversation(convId);
    refreshConversationList();
}

void AggregateChatForm::onConversationListContextMenu(const QPoint& pos)
{
    if (!m_conversationList)
        return;
    const QModelIndex index = m_conversationList->indexAt(pos);
    if (!index.isValid())
        return;
    const int convId = m_conversationListModel
                            ? m_conversationListModel->data(index, ConversationListModel::ConversationIdRole).toInt()
                            : 0;
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

        ConversationDao convDao;
        const auto conv = convDao.findById(convId);
        if (conv && !applyServiceConversationMutation(*conv, false))
            return;

        if (!ConversationManager::instance().clearConversationMessages(convId)) {
            QMessageBox warnBox(this);
            warnBox.setIcon(QMessageBox::Warning);
            warnBox.setWindowTitle(QStringLiteral("错误"));
            warnBox.setText(QStringLiteral("清空失败，请查看日志或确认数据库已升级。"));
            warnBox.setStandardButtons(QMessageBox::Ok);
            warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
            warnBox.exec();
            return;
        }

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

        ConversationDao convDao;
        const auto conv = convDao.findById(convId);
        if (conv && !applyServiceConversationMutation(*conv, true))
            return;

        ConversationManager::instance().deleteConversation(convId);
    }
}

void AggregateChatForm::onSendClicked()
{
    QElapsedTimer timer;
    timer.start();
    if (m_currentConvId <= 0) return;
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // 发送后先留在「待处理」，直到客服切换会话或主动切换 tab。
    m_pendingStickyConvId = m_currentConvId;
    m_inputEdit->clear();
    ConversationDao().clearCachedDraft(m_currentConvId);
    ConversationManager::instance().sendMessage(m_currentConvId, text);
    qInfo() << "[AggregateChatForm] send click timing"
            << "conversationId=" << m_currentConvId
            << "textLength=" << text.size()
            << "elapsedMs=" << timer.elapsed();
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

void AggregateChatForm::onUnifiedMessageReceived(int conversationId, const Models::Message& message)
{
    QElapsedTimer timer;
    timer.start();
    const MessageRecord record = messageRecordFromUnified(message);
    if (message.direction == Models::MessageDirection::Outbound)
        onSentOk(conversationId, record);
    else
        onNewMessage(conversationId, record);
    qInfo() << "[AggregateChatForm] unified message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << message.id
            << "direction=" << Models::toString(message.direction)
            << "platformMessageId=" << message.platformMessageId
            << "clientMessageId=" << message.clientMessageId
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::onUnifiedConversationUpdated(const Models::Conversation& conversation)
{
    if (conversation.id == m_currentConvId) {
        const ConversationInfo info = conversationInfoFromUnified(conversation);
        updateCustomerInfo(info);
        if (m_chatHeader)
            m_chatHeader->setText(QStringLiteral("%1 (%2)").arg(info.customerName, info.platform));
    }
    refreshConversationList();
}

void AggregateChatForm::onConversationMessagesCleared(int conversationId)
{
    if (m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    if (conversationId == m_currentConvId) {
        m_lastBubbleDate = QDate();
        renderConversationMessages({});
        m_currentMessageSignature = buildMessageSignature({});
        resetSendTimelineForConversation();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("已清空聊天记录"), 3000);
}

void AggregateChatForm::onConversationDeleted(int conversationId)
{
    if (m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    if (conversationId == m_currentConvId) {
        m_currentConvId = -1;
        m_lastBubbleDate = QDate();
        renderConversationMessages({});
        m_currentMessageSignature = buildMessageSignature({});
        showCenterEmptyState();
        showRightEmptyState();
        if (m_inputEdit)
            m_inputEdit->clear();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("已删除会话"), 3000);
}

void AggregateChatForm::onNewMessage(int conversationId, const MessageRecord& msg)
{
    QElapsedTimer timer;
    timer.start();
    if (msg.direction == QLatin1String("in") && m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    if (m_currentConvId <= 0) {
        showConversation(conversationId);
    } else if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("新消息: %1").arg(msg.content.left(30)), 3000);

    if (conversationId == m_currentConvId && msg.direction == QLatin1String("in"))
        tryAggregateAutoReply(conversationId, QStringLiteral("T2"));
    qInfo() << "[AggregateChatForm] inbound message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << msg.id
            << "direction=" << msg.direction
            << "platformMsgId=" << msg.platformMsgId
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::onSentOk(int conversationId, const MessageRecord& msg)
{
    QElapsedTimer timer;
    timer.start();
    if (conversationId == m_currentConvId && msg.direction == QLatin1String("out"))
        m_pendingStickyConvId = conversationId;
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
    qInfo() << "[AggregateChatForm] outbound message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << msg.id
            << "direction=" << msg.direction
            << "clientMessageId=" << msg.clientMessageId
            << "elapsedMs=" << timer.elapsed();
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

void AggregateChatForm::openMessageMedia(const MessageRecord& msg)
{
    const QString type = msg.contentType.trimmed().toLower();
    if (type != QLatin1String("image")
        && type != QLatin1String("emoji")
        && type != QLatin1String("file")
        && type != QLatin1String("video")) {
        return;
    }

    const QString path = msg.contentImagePath.trimmed();
    if (path.isEmpty() || !QFile::exists(path)) {
        showAggregateWarning(this, QStringLiteral("媒体不可用"), QStringLiteral("本地媒体文件不存在或已被移动。"));
        return;
    }

    const bool imagePath = isImageFilePath(path);
    if (type == QLatin1String("image") || type == QLatin1String("emoji")) {
        if (!imagePath) {
            showAggregateWarning(this, QStringLiteral("图片不可预览"), QStringLiteral("该消息没有可预览的图片文件。"));
            return;
        }
        ImagePreviewDialog dlg(path, this);
        dlg.exec();
        return;
    }

    if (type == QLatin1String("file")) {
        if (imagePath) {
            ImagePreviewDialog dlg(path, this);
            dlg.setWindowTitle(QStringLiteral("文件截图证据"));
            dlg.exec();
            showStatusMessage(QStringLiteral("当前仅有文件消息截图证据，未获取到原始文件"), 5000);
            return;
        }
        openLocalFileWithDefaultApp(this, path, QStringLiteral("打开文件失败"));
        return;
    }

    if (type == QLatin1String("video")) {
        if (imagePath) {
            ImagePreviewDialog dlg(path, this);
            dlg.setWindowTitle(QStringLiteral("视频截图证据"));
            dlg.exec();
            showStatusMessage(QStringLiteral("当前仅有视频消息截图证据，未获取到可播放视频"), 5000);
            return;
        }
        VideoPreviewDialog dlg(path, this);
        dlg.exec();
    }
}

bool AggregateChatForm::applyServiceConversationMutation(const ConversationInfo& conv, bool deleteConversation)
{
    auto& ipc = Ipc::IpcService::instance();
    QString serviceError;
    if (!ipc.connectToConfiguredService(&serviceError)) {
        qInfo() << "[AggregateChatForm] service mutation skipped: Python service unavailable"
                << "conversationId=" << conv.id
                << "error=" << serviceError;
        return true;
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    const QJsonObject response = deleteConversation
        ? ipc.deleteConversationOnService(
              conv.platform,
              conv.accountId,
              conv.platformConversationId,
              5000,
              &status,
              &error)
        : ipc.clearConversationMessages(
              conv.platform,
              conv.accountId,
              conv.platformConversationId,
              5000,
              &status,
              &error);
    const bool ok = status == Ipc::ResponseStatus::Success
        && response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("success");
    if (ok)
        return true;

    QString detail = error;
    if (detail.isEmpty())
        detail = response.value(QStringLiteral("error")).toString();
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    if (detail == QLatin1String("observer_active")) {
        detail = QStringLiteral("平台监听中，无法清空/删除。请先停止监听后再操作。");
    } else if (detail == QLatin1String("recent_observation")) {
        const int quietSeconds = qMax(1, int(result.value(QStringLiteral("quiet_seconds_required")).toDouble(3.0)));
        detail = QStringLiteral("刚检测到平台消息观察事件。请停止监听并等待约 %1 秒后再操作。").arg(quietSeconds);
    }
    if (detail.isEmpty())
        detail = QStringLiteral("unknown_error");

    QMessageBox warnBox(this);
    warnBox.setIcon(QMessageBox::Warning);
    warnBox.setWindowTitle(QStringLiteral("Python 服务端"));
    warnBox.setText(deleteConversation
                        ? QStringLiteral("服务端删除会话失败，已取消本地删除。")
                        : QStringLiteral("服务端清空聊天记录失败，已取消本地清空。"));
    warnBox.setInformativeText(detail);
    warnBox.setStandardButtons(QMessageBox::Ok);
    warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
    warnBox.exec();
    return false;
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
    if (!m_aggregateAiIpcRequestId.isEmpty()) {
        Ipc::IpcService::instance().cancelRequest(m_aggregateAiIpcRequestId);
        m_aggregateAiIpcRequestId.clear();
    }
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
    if (m_currentConvId <= 0 || m_aggregateAiGenerating)
        return;
    if (!m_aiChatService) {
        showStatusMessage(QStringLiteral("AI 服务未初始化"), 4000);
        return;
    }

    AggregateAiBuiltRequest built =
        m_aiChatService->buildAggregateReplyRequest(m_currentConvId, m_aggregateAiSessionModelKey);
    if (!handleAggregateBuildFailure(this, built, false, nullptr))
        return;

    m_aggregateAiBaseline = m_inputEdit ? m_inputEdit->toPlainText() : QString();
    m_aggregateAiAccumulated.clear();
    m_aggregateAiIpcRequestId.clear();

    built.request.extraRootFields.insert(QStringLiteral("max_tokens"), 512);
    clearStreamingSession(m_aggregateAiSession);
    m_aggregateAiSession = m_aiChatService->createSession(built.config, built.request, this);
    connect(m_aggregateAiSession, &IAiStreamingSession::delta, this, &AggregateChatForm::onAggregateAiStreamDelta);
    connect(m_aggregateAiSession, &IAiStreamingSession::completed, this, &AggregateChatForm::onAggregateAiCompleted);
    connect(m_aggregateAiSession, &IAiStreamingSession::failed, this, &AggregateChatForm::onAggregateAiFailed);
    setAggregateAiBusy(true);
    m_aggregateAiSession->start();
}

void AggregateChatForm::onIpcAiSuggestionReceived(const Ipc::AiSuggestionResponse& response)
{
    if (response.requestId != m_aggregateAiIpcRequestId)
        return;

    m_aggregateAiIpcRequestId.clear();
    setAggregateAiBusy(false);

    if (response.status != Ipc::ResponseStatus::Success) {
        showStatusMessage(QStringLiteral("AI 建议失败：%1").arg(response.errorMessage.left(120)), 6000);
        return;
    }
    if (response.suggestions.isEmpty()) {
        showStatusMessage(QStringLiteral("AI 未返回可用建议"), 5000);
        return;
    }

    const QString suggestion = response.suggestions.first().trimmed();
    if (suggestion.isEmpty()) {
        showStatusMessage(QStringLiteral("AI 返回了空建议"), 5000);
        return;
    }

    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + suggestion);
    persistCurrentDraft();
    showStatusMessage(QStringLiteral("AI 草稿已生成，可直接发送或继续修改"), 4000);
}

void AggregateChatForm::onIpcRequestFailed(const QString& requestId, const QString& reason)
{
    if (requestId != m_aggregateAiIpcRequestId)
        return;

    m_aggregateAiIpcRequestId.clear();
    setAggregateAiBusy(false);
    showStatusMessage(QStringLiteral("AI 建议失败：%1").arg(reason.left(120)), 6000);
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
    if (m_aggregateAiAccumulated.trimmed().isEmpty()) {
        showStatusMessage(QStringLiteral("AI 未返回可用草稿"), 5000);
        return;
    }
    persistCurrentDraft();
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
    persistCurrentDraft();
    setAggregateAiBusy(false);
    showStatusMessage(QStringLiteral("AI 草稿生成失败：%1").arg(reason.left(120)), 6000);
}
