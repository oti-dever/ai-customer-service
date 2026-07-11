#include "aicustomerservicebackendwindow.h"
#include "aiproviderconfigpage.h"
#include "robotsandboxdialog.h"
#include "sidebartocdelegate.h"

#include "../utils/applystyle.h"
#include "../utils/appsettings.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../ipc/ipcservice.h"

#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QElapsedTimer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QStyleFactory>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <cmath>
#include <functional>
#include <initializer_list>
#include <tuple>
#include <utility>
#include <memory>

#include <QVector>

namespace {

/** 与 m_stack 添加顺序一致 */
constexpr int kStackDashboard = 0;
constexpr int kStackRobotStoreConfig = 1;
constexpr int kStackProductKnowledge = 2;
constexpr int kStackApiModel = 3;
constexpr int kStackGeneralSettings = 4;

QTreeWidgetItem* findNavItemByStackIndex(QTreeWidgetItem* node, int stackIdx)
{
    if (!node)
        return nullptr;
    if (node->childCount() == 0) {
        const QVariant v = node->data(0, Qt::UserRole);
        if (v.isValid() && v.toInt() == stackIdx)
            return node;
        return nullptr;
    }
    for (int i = 0; i < node->childCount(); ++i) {
        if (QTreeWidgetItem* hit = findNavItemByStackIndex(node->child(i), stackIdx))
            return hit;
    }
    return nullptr;
}

QTreeWidgetItem* findNavItemByStackIndex(QTreeWidget* tree, int stackIdx)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* hit = findNavItemByStackIndex(tree->topLevelItem(i), stackIdx))
            return hit;
    }
    return nullptr;
}

/** 右侧内容区与数据概览页背景（浅灰，与占位卡片示意一致） */
static constexpr char kAiBackendContentBg[] = "#F4F4F5";

/** 数据概览：流量监控占位（自绘折线 + 渐变填充） */
class AiBackendDashTrafficChartCard final : public QFrame
{
public:
    explicit AiBackendDashTrafficChartCard(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("aiBackendDashTrafficCard"));
        setMinimumHeight(270);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
        setAttribute(Qt::WA_StyledBackground, false);
        auto* sh = new QGraphicsDropShadowEffect(this);
        sh->setBlurRadius(16);
        sh->setOffset(0, 2);
        sh->setColor(QColor(15, 23, 42, 28));
        setGraphicsEffect(sh);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect br = rect().adjusted(10, 10, -10, -10);
        p.setPen(QPen(QColor(QStringLiteral("#E5E7EB")), 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(br, 12, 12);

        QRect c = br.adjusted(16, 14, -16, -14);
        QFont titleF = p.font();
        titleF.setBold(true);
        titleF.setPointSizeF(10.5);
        p.setFont(titleF);
        p.setPen(QColor(QStringLiteral("#0f172a")));
        p.drawText(c.left(), c.top() + 18, QStringLiteral("流量监控"));

        QFont subF = p.font();
        subF.setBold(false);
        subF.setPointSizeF(9.0);
        p.setFont(subF);
        p.setPen(QColor(QStringLiteral("#64748b")));
        p.drawText(c.left(), c.top() + 38, QStringLiteral("今日 AI 接待咨询量分布"));

        const int legendW = 88;
        const QRect legendR(c.right() - legendW, c.top() + 4, legendW, 20);
        p.setBrush(QColor(QStringLiteral("#7C3AED")));
        p.setPen(Qt::NoPen);
        p.drawEllipse(legendR.left(), legendR.center().y() - 4, 8, 8);
        p.setPen(QColor(QStringLiteral("#64748b")));
        p.drawText(legendR.left() + 14, legendR.top(), legendR.width() - 14, legendR.height(),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("咨询量"));

        QRect plot = c;
        plot.setTop(c.top() + 52);
        plot.setBottom(c.bottom() - 8);
        const int axisLeft = 34;
        const int axisBottom = 22;
        QRect plotArea(plot.left() + axisLeft, plot.top(), plot.width() - axisLeft - 4,
                         plot.height() - axisBottom);

        p.setPen(QPen(QColor(QStringLiteral("#E2E8F0")), 1, Qt::DashLine));
        for (int i = 0; i <= 4; ++i) {
            const int y = plotArea.top() + (plotArea.height() * i) / 4;
            p.drawLine(plotArea.left(), y, plotArea.right(), y);
        }

        p.setPen(QColor(QStringLiteral("#94a3b8")));
        p.setFont(subF);
        const int yVals[] = {320, 240, 160, 80, 0};
        for (int i = 0; i <= 4; ++i) {
            const int y = plotArea.top() + (plotArea.height() * i) / 4;
            p.drawText(plot.left() + 4, y - 10, axisLeft - 8, 20, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(yVals[i]));
        }
        const QString xs[] = {QStringLiteral("00:00"), QStringLiteral("04:00"), QStringLiteral("08:00"),
                              QStringLiteral("12:00"), QStringLiteral("16:00"), QStringLiteral("20:00"),
                              QStringLiteral("23:59")};
        const int nx = 7;
        for (int i = 0; i < nx; ++i) {
            const int x = plotArea.left() + (plotArea.width() * i) / (nx - 1);
            p.drawText(x - 28, plotArea.bottom() + 4, 56, 18, Qt::AlignHCenter | Qt::AlignTop, xs[i]);
        }

        QPolygonF poly;
        const int n = 32;
        for (int i = 0; i <= n; ++i) {
            const qreal t = qreal(i) / qreal(n);
            const qreal x = plotArea.left() + t * plotArea.width();
            const qreal bellArg = (t - 0.62) * 7.0;
            const qreal bell = std::exp(-bellArg * bellArg);
            const qreal y = plotArea.bottom() - (0.12 + 0.78 * bell) * plotArea.height();
            poly << QPointF(x, y);
        }

        QPainterPath fillPath;
        fillPath.moveTo(poly.first());
        for (int i = 1; i < poly.size(); ++i)
            fillPath.lineTo(poly.at(i));
        fillPath.lineTo(plotArea.right(), plotArea.bottom());
        fillPath.lineTo(plotArea.left(), plotArea.bottom());
        fillPath.closeSubpath();

        QLinearGradient grad(plotArea.left(), plotArea.top(), plotArea.left(), plotArea.bottom());
        grad.setColorAt(0, QColor(124, 58, 237, 55));
        grad.setColorAt(1, QColor(124, 58, 237, 0));
        p.fillPath(fillPath, grad);

        p.setPen(QPen(QColor(QStringLiteral("#7C3AED")), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);

        p.setPen(QColor(QStringLiteral("#CBD5E1")));
        p.drawLine(plotArea.left(), plotArea.bottom(), plotArea.right(), plotArea.bottom());
    }
};

/** 数据概览：热门咨询分类占位（自绘条形） */
class AiBackendDashCategoryCard final : public QFrame
{
public:
    explicit AiBackendDashCategoryCard(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("aiBackendDashCategoryCard"));
        setMinimumHeight(270);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
        setAttribute(Qt::WA_StyledBackground, false);
        auto* sh = new QGraphicsDropShadowEffect(this);
        sh->setBlurRadius(16);
        sh->setOffset(0, 2);
        sh->setColor(QColor(15, 23, 42, 28));
        setGraphicsEffect(sh);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect br = rect().adjusted(10, 10, -10, -10);
        p.setPen(QPen(QColor(QStringLiteral("#E5E7EB")), 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(br, 12, 12);

        QRect c = br.adjusted(16, 14, -16, -14);
        QFont titleF = p.font();
        titleF.setBold(true);
        titleF.setPointSizeF(10.5);
        p.setFont(titleF);
        p.setPen(QColor(QStringLiteral("#0f172a")));
        p.drawText(c.left(), c.top() + 18, QStringLiteral("热门咨询分类"));

        struct Row {
            const char* name;
            double pct;
            QColor col;
        };
        static const Row rows[] = {
            {"订单查询", 0.42, QColor(QStringLiteral("#8B5CF6"))},
            {"退货退款", 0.28, QColor(QStringLiteral("#F43F5E"))},
            {"活动咨询", 0.18, QColor(QStringLiteral("#F97316"))},
            {"支付问题", 0.12, QColor(QStringLiteral("#22C55E"))},
        };

        const int rowH = 36;
        int y0 = c.top() + 44;
        QFont rowF = p.font();
        rowF.setBold(false);
        rowF.setPointSizeF(9.5);
        p.setFont(rowF);

        for (int i = 0; i < 4; ++i) {
            const int y = y0 + i * rowH;
            p.setPen(QColor(QStringLiteral("#0f172a")));
            p.drawText(c.left(), y, 72, rowH - 6, Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromUtf8(rows[i].name));

            const int trackX = c.left() + 80;
            const int trackW = c.right() - trackX - 44;
            const int trackY = y + 8;
            const int trackH = 10;
            QRect track(trackX, trackY, trackW, trackH);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#F1F5F9")));
            p.drawRoundedRect(track, 5, 5);

            QRect fill(track);
            fill.setWidth(qMax(8, int(trackW * rows[i].pct)));
            p.setBrush(rows[i].col);
            p.drawRoundedRect(fill, 5, 5);

            p.setPen(QColor(QStringLiteral("#64748b")));
            const QString pctTxt = QStringLiteral("%1%").arg(int(rows[i].pct * 100 + 0.5));
            p.drawText(trackX + trackW + 8, y, 40, rowH - 6, Qt::AlignVCenter | Qt::AlignRight, pctTxt);
        }

        const int btnH = 40;
        QRect btn(c.left() + 8, c.bottom() - btnH - 4, c.width() - 16, btnH);
        p.setPen(QPen(QColor(QStringLiteral("#C7D2FE")), 1));
        p.setBrush(QColor(QStringLiteral("#EEF2FF")));
        p.drawRoundedRect(btn, 10, 10);
        QFont btnF = p.font();
        btnF.setPointSizeF(10.0);
        btnF.setBold(true);
        p.setFont(btnF);
        p.setPen(QColor(QStringLiteral("#4F46E5")));
        p.drawText(btn, Qt::AlignCenter, QStringLiteral("查看完整报告"));
    }
};

QFrame* makeMetricCard(const QString& title,
                       const QString& value,
                       const QString& changeText,
                       bool changePositive)
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("aiBackendMetricCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setBlurRadius(16);
    cardShadow->setOffset(0, 2);
    cardShadow->setColor(QColor(15, 23, 42, 28));
    card->setGraphicsEffect(cardShadow);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(8);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addStretch(1);
    auto* tag = new QLabel(QStringLiteral("今日"), card);
    tag->setObjectName(QStringLiteral("aiBackendMetricTag"));
    top->addWidget(tag, 0, Qt::AlignRight);
    v->addLayout(top);

    auto* titleL = new QLabel(title, card);
    titleL->setObjectName(QStringLiteral("aiBackendMetricTitle"));
    v->addWidget(titleL);
    auto* valueL = new QLabel(value, card);
    valueL->setObjectName(QStringLiteral("aiBackendMetricValue"));
    v->addWidget(valueL);

    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 0, 0);
    auto* hint = new QLabel(QStringLiteral("实时数据更新中"), card);
    hint->setObjectName(QStringLiteral("aiBackendMetricHint"));
    auto* chg = new QLabel(changeText, card);
    chg->setObjectName(changePositive ? QStringLiteral("aiBackendMetricUp")
                                       : QStringLiteral("aiBackendMetricDown"));
    bottom->addWidget(hint);
    bottom->addStretch(1);
    bottom->addWidget(chg);
    v->addLayout(bottom);

    return card;
}

QWidget* makePlaceholderPage(const QString& name)
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* t = new QLabel(QStringLiteral("「%1」\n\n功能开发中").arg(name), w);
    t->setAlignment(Qt::AlignCenter);
    t->setObjectName(QStringLiteral("aiBackendPlaceholderText"));
    l->addWidget(t);
    return w;
}

static void styleBackendDataTable(QTableWidget* table)
{
    table->setObjectName(QStringLiteral("aiBackendDataTable"));
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(54);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setTextElideMode(Qt::ElideNone);
    /* 若为 true，最后一列会吃掉几乎全部剩余宽度，含 cellWidget 的列在 ResizeToContents 下又偏窄，易重叠 */
    table->horizontalHeader()->setStretchLastSection(false);
}

static QWidget* wrapNameIdCell(const QString& name, const QString& id, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 8, 6, 8);
    v->setSpacing(2);
    auto* nameL = new QLabel(name, w);
    nameL->setObjectName(QStringLiteral("aiBackendTableCellTitle"));
    auto* idL = new QLabel(id, w);
    idL->setObjectName(QStringLiteral("aiBackendTableCellMuted"));
    v->addWidget(nameL);
    v->addWidget(idL);
    return w;
}

static QLabel* makePillLabel(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(objectName);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

static QWidget* wrapCenterInCell(QWidget* inner, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(4, 4, 4, 4);
    h->addStretch(1);
    h->addWidget(inner, 0, Qt::AlignVCenter);
    h->addStretch(1);
    return w;
}

/** 列表行内与列标题顶对齐，水平居中（模型、状态胶囊） */
static QWidget* wrapTopHCenterInCell(QWidget* inner, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(2, 0, 2, 0);
    v->setSpacing(0);
    v->addWidget(inner, 0, Qt::AlignHCenter | Qt::AlignTop);
    v->addStretch(1);
    return w;
}

static QWidget* wrapPillRow(QWidget* parent, const std::initializer_list<std::pair<QString, QString>>& pills)
{
    auto* w = new QWidget(parent);
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(6, 8, 6, 8);
    outer->setSpacing(6);
    constexpr int kPerRow = 2;
    QHBoxLayout* row = nullptr;
    int col = 0;
    for (const auto& pr : pills) {
        if (col == 0) {
            row = new QHBoxLayout;
            row->setSpacing(6);
            row->setContentsMargins(0, 0, 0, 0);
        }
        row->addWidget(makePillLabel(pr.first, pr.second, w), 0, Qt::AlignVCenter);
        ++col;
        if (col >= kPerRow) {
            row->addStretch(1);
            outer->addLayout(row);
            col = 0;
            row = nullptr;
        }
    }
    if (row) {
        row->addStretch(1);
        outer->addLayout(row);
    }
    return w;
}

static QWidget* wrapMultilineStores(const QString& text, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 8, 6, 8);
    v->setSpacing(4);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        auto* l = new QLabel(line, w);
        l->setObjectName(QStringLiteral("aiBackendTableCellBody"));
        l->setWordWrap(true);
        v->addWidget(l);
    }
    v->addStretch(1);
    return w;
}

static QWidget* wrapActionLinks(QWidget* parent, const std::initializer_list<std::tuple<QString, QString>>& items)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(4, 8, 4, 8);
    h->setSpacing(14);
    for (const auto& tup : items) {
        auto* a = new QLabel(std::get<0>(tup), w);
        a->setObjectName(std::get<1>(tup));
        a->setCursor(Qt::PointingHandCursor);
        h->addWidget(a, 0, Qt::AlignVCenter);
    }
    h->addStretch(1);
    return w;
}

/** QTableWidget 的 ResizeToContents 对仅有 cellWidget 的列往往过窄；据此用 sizeHint 估宽，再让指定列 Stretch 吃剩余空间 */
static QVector<int> backendTableColumnMinWidths(QTableWidget* table)
{
    QHeaderView* h = table->horizontalHeader();
    const int cols = table->columnCount();
    const int rows = table->rowCount();
    QVector<int> minW(cols, qMax(48, h->minimumSectionSize()));
    const QFontMetrics hdrFm(h->font());
    for (int c = 0; c < cols; ++c) {
        int hdrW = 56;
        if (QTableWidgetItem* hi = table->horizontalHeaderItem(c))
            hdrW = hdrFm.horizontalAdvance(hi->text()) + 32;
        minW[c] = qMax(minW[c], hdrW);
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (QWidget* cw = table->cellWidget(r, c)) {
                cw->adjustSize();
                const int hint = cw->sizeHint().width() + 28;
                minW[c] = qMax(minW[c], hint);
            } else if (QTableWidgetItem* it = table->item(r, c)) {
                const QSize sh = it->sizeHint();
                int hint = sh.width() > 0 ? sh.width() : 0;
                const QFontMetrics fm(it->font().resolve(table->font()));
                const int textW = fm.horizontalAdvance(it->text());
                hint = qMax(hint, textW);
                minW[c] = qMax(minW[c], hint + 28);
            }
        }
    }
    for (int c = 0; c < cols; ++c)
        minW[c] = qMin(minW[c], 720);
    return minW;
}

static void applyBackendTableColumnSizing(QTableWidget* table, std::initializer_list<int> stretchColumns)
{
    if (!table)
        return;
    auto isStretch = [stretchColumns](int c) -> bool {
        for (int x : stretchColumns) {
            if (x == c)
                return true;
        }
        return false;
    };
    QHeaderView* hdr = table->horizontalHeader();
    hdr->setStretchLastSection(false);
    const QVector<int> minW = backendTableColumnMinWidths(table);
    const int cols = table->columnCount();
    for (int c = 0; c < cols; ++c) {
        if (isStretch(c))
            hdr->setSectionResizeMode(c, QHeaderView::Stretch);
        else {
            hdr->setSectionResizeMode(c, QHeaderView::Fixed);
            table->setColumnWidth(c, minW[c]);
        }
    }
    table->resizeRowsToContents();
}

/** 店铺机器人配置：用「列标题 + 自定义行」替代 QTableWidget，便于留白、无竖线、与网页风列表一致 */
static QWidget* buildRobotConfigListHeader(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(20, 20, 20, 10);
    h->setSpacing(20);
    auto addHdr = [&](const QString& t, int stretch) {
        auto* lab = new QLabel(t, w);
        lab->setObjectName(QStringLiteral("aiBackendRobotListColHeader"));
        lab->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lab->setWordWrap(false);
        h->addWidget(lab, stretch, Qt::AlignTop);
    };
    addHdr(QStringLiteral("机器人名称 / ID"), 2);
    addHdr(QStringLiteral("模型配置"), 1);
    addHdr(QStringLiteral("知识库 / 策略"), 3);
    addHdr(QStringLiteral("生效店铺"), 2);
    addHdr(QStringLiteral("状态"), 1);
    addHdr(QStringLiteral("操作"), 1);
    return w;
}

static QFrame* makeRobotListHorizontalRule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("aiBackendRobotListRule"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

static QWidget* buildRobotConfigDataRow(
    QWidget* parent,
    const QString& name,
    const QString& id,
    const QString& modelText,
    const std::initializer_list<std::pair<QString, QString>>& pills,
    const QString& storesText,
    bool online,
    const std::initializer_list<std::tuple<QString, QString>>& actions)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("aiBackendRobotListRow"));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(20, 18, 20, 18);
    h->setSpacing(20);

    const auto addCell = [&](QWidget* cell, int stretch) {
        h->addWidget(cell, stretch, Qt::AlignTop);
    };

    addCell(wrapNameIdCell(name, id, row), 2);
    addCell(wrapTopHCenterInCell(makePillLabel(modelText, QStringLiteral("aiBackendTagModel"), row), row), 1);
    addCell(wrapPillRow(row, pills), 3);
    addCell(wrapMultilineStores(storesText, row), 2);

    const QString statusText = online ? QStringLiteral("● 在线") : QStringLiteral("● 离线");
    const QString statusObj = online ? QStringLiteral("aiBackendStatusOnline") : QStringLiteral("aiBackendStatusOffline");
    addCell(wrapTopHCenterInCell(makePillLabel(statusText, statusObj, row), row), 1);
    addCell(wrapActionLinks(row, actions), 1);
    return row;
}

static QTableWidgetItem* makeKnowledgeCellItem(const QString& text);
static bool isKnowledgeBaseEnabled(const QJsonObject& base);

static constexpr char kAiBackendRobotsSettingsKey[] = "ai/robots/list";
static constexpr char kAiBackendPlatformRobotBindingsGroup[] = "ai/platformRobotBindings";

static QString compactRobotId()
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    id.remove(QLatin1Char('-'));
    return QStringLiteral("rob_%1").arg(id.left(12));
}

static QJsonArray loadRobotConfigs()
{
    QSettings settings = AppSettings::create();
    const QString raw = settings.value(QString::fromLatin1(kAiBackendRobotsSettingsKey)).toString();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray())
        return {};
    return doc.array();
}

static void saveRobotConfigs(const QJsonArray& robots)
{
    QSettings settings = AppSettings::create();
    settings.setValue(QString::fromLatin1(kAiBackendRobotsSettingsKey),
                      QString::fromUtf8(QJsonDocument(robots).toJson(QJsonDocument::Compact)));
}

static bool robotConfigEnabled(const QJsonObject& robot)
{
    const QJsonValue enabled = robot.value(QStringLiteral("enabled"));
    if (enabled.isBool())
        return enabled.toBool();
    return enabled.toInt(1) != 0;
}

static QStringList jsonStringList(const QJsonArray& values)
{
    QStringList out;
    out.reserve(values.size());
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

static QJsonArray stringListToJsonArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& value : values) {
        const QString text = value.trimmed();
        if (!text.isEmpty())
            arr.append(text);
    }
    return arr;
}

static QString robotModelDisplayName(const QString& sessionModelKey)
{
    const AiPresetDefinition def = aiPresetDefinition(sessionModelKey);
    AiConfigLoadOptions options;
    options.allowAggregateFallback = true;
    options.allowGeneralFallback = true;
    const AiProviderConfig config = loadAiProviderConfig(sessionModelKey, options);
    const QString model = config.model.trimmed();
    if (!model.isEmpty())
        return QStringLiteral("%1（%2）").arg(def.label, model);
    return def.label.trimmed().isEmpty() ? sessionModelKey : def.label;
}

static QString robotKnowledgeDisplayName(const QJsonObject& robot)
{
    QStringList names = jsonStringList(robot.value(QStringLiteral("knowledge_base_names")).toArray());
    if (names.isEmpty())
        names = jsonStringList(robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    return names.isEmpty() ? QStringLiteral("未绑定") : names.join(QStringLiteral("、"));
}

static QString robotStrategyDisplayName(const QJsonObject& robot)
{
    const QString tone = robot.value(QStringLiteral("reply_tone"))
                             .toString(QStringLiteral("亲切温和、不失热情"))
                             .trimmed();
    const QString addresses = robot.value(QStringLiteral("common_address_terms"))
                                  .toString(QStringLiteral("亲、宝子"))
                                  .trimmed();
    const bool allowImages = robot.value(QStringLiteral("allow_auto_send_images")).toBool(false);
    const bool allowMulti = robot.value(QStringLiteral("allow_auto_send_multi_messages")).toBool(false);
    const int maxMessages = qBound(1,
                                   robot.value(QStringLiteral("max_auto_send_messages"))
                                       .toInt(allowMulti ? 2 : 1),
                                   3);
    return QStringLiteral("回复语气：%1\n常用称呼：%2\n自动发图：%3\n自动多条消息：%4\n最多自动发送：%5 条")
        .arg(tone.isEmpty() ? QStringLiteral("默认") : tone,
             addresses.isEmpty() ? QStringLiteral("不指定") : addresses,
             allowImages ? QStringLiteral("允许") : QStringLiteral("不允许"),
             allowMulti ? QStringLiteral("允许") : QStringLiteral("不允许"))
        .arg(maxMessages);
}

static QString robotDisplayText(const QString& text, const QString& fallback)
{
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

static QString robotPlatformDisplayName(const QString& platform)
{
    const QString value = platform.trimmed().toLower();
    if (value == QLatin1String("wechat"))
        return QStringLiteral("微信");
    if (value == QLatin1String("qianniu"))
        return QStringLiteral("千牛");
    if (value == QLatin1String("pdd_web") || value == QLatin1String("pdd"))
        return QStringLiteral("拼多多");
    if (value == QLatin1String("douyin") || value == QLatin1String("doudian"))
        return QStringLiteral("抖店");
    if (value == QLatin1String("qq"))
        return QStringLiteral("QQ");
    return platform;
}

static QStringList platformsBoundToRobot(const QString& robotId)
{
    QStringList platforms;
    const QString wanted = robotId.trimmed();
    if (wanted.isEmpty())
        return platforms;
    QSettings settings = AppSettings::create();
    settings.beginGroup(QString::fromLatin1(kAiBackendPlatformRobotBindingsGroup));
    const QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        if (settings.value(key).toString().trimmed() == wanted)
            platforms.append(robotPlatformDisplayName(key));
    }
    settings.endGroup();
    platforms.removeDuplicates();
    platforms.sort(Qt::CaseInsensitive);
    return platforms;
}

static QStringList invalidPlatformRobotBindings(const QSet<QString>& existingRobotIds)
{
    QStringList invalidBindings;
    QSettings settings = AppSettings::create();
    settings.beginGroup(QString::fromLatin1(kAiBackendPlatformRobotBindingsGroup));
    const QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        const QString robotId = settings.value(key).toString().trimmed();
        if (!robotId.isEmpty() && !existingRobotIds.contains(robotId)) {
            invalidBindings.append(QStringLiteral("%1 -> %2")
                                       .arg(robotPlatformDisplayName(key), robotId));
        }
    }
    settings.endGroup();
    invalidBindings.sort(Qt::CaseInsensitive);
    return invalidBindings;
}

static QSet<QString> availableRobotModelKeys()
{
    QSet<QString> keys;
    for (const AiPresetDefinition& def : aiPresetDefinitions()) {
        if (def.available && !def.sessionModelKey.trimmed().isEmpty())
            keys.insert(def.sessionModelKey.trimmed());
    }
    return keys;
}

static QSet<QString> robotKnowledgeIdSet(const QJsonObject& robot)
{
    QSet<QString> ids;
    for (const QString& id : jsonStringList(robot.value(QStringLiteral("knowledge_base_ids")).toArray()))
        ids.insert(id);
    return ids;
}

static QSet<QString> availableKnowledgeBaseIds(bool* known = nullptr)
{
    if (known)
        *known = false;
    if (!Ipc::IpcService::instance().isServiceAvailable())
        return {};

    QString error;
    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    const QJsonObject response = Ipc::IpcService::instance().fetchKnowledgeBases(1500, &status, &error);
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        return {};
    }

    QSet<QString> ids;
    const QJsonArray bases = response.value(QStringLiteral("bases")).toArray();
    for (const QJsonValue& value : bases) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty())
            ids.insert(id);
    }
    if (known)
        *known = true;
    return ids;
}

static QJsonObject cloneRobotConfig(const QJsonObject& robot)
{
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonObject copy = robot;
    const QString name = robot.value(QStringLiteral("robot_name")).toString(QStringLiteral("未命名机器人")).trimmed();
    copy.insert(QStringLiteral("robot_id"), compactRobotId());
    copy.insert(QStringLiteral("robot_name"),
                name.isEmpty() ? QStringLiteral("未命名机器人 副本") : QStringLiteral("%1 副本").arg(name));
    copy.insert(QStringLiteral("enabled"), false);
    copy.insert(QStringLiteral("created_at"), now);
    copy.insert(QStringLiteral("updated_at"), now);
    return copy;
}

static bool robotMatchesFilters(const QJsonObject& robot,
                                const QString& searchText,
                                const QString& modelFilter,
                                const QString& statusFilter)
{
    const QString modelKey = robot.value(QStringLiteral("model_config_id")).toString().trimmed();
    if (!modelFilter.trimmed().isEmpty() && modelKey != modelFilter.trimmed())
        return false;

    const bool enabled = robotConfigEnabled(robot);
    if (statusFilter == QLatin1String("enabled") && !enabled)
        return false;
    if (statusFilter == QLatin1String("disabled") && enabled)
        return false;
    if (statusFilter == QLatin1String("bound")
        && platformsBoundToRobot(robot.value(QStringLiteral("robot_id")).toString()).isEmpty()) {
        return false;
    }

    const QString query = searchText.trimmed();
    if (query.isEmpty())
        return true;

    QStringList haystack;
    haystack << robot.value(QStringLiteral("robot_id")).toString()
             << robot.value(QStringLiteral("robot_name")).toString()
             << modelKey
             << robotModelDisplayName(modelKey)
             << robotKnowledgeDisplayName(robot)
             << robotStrategyDisplayName(robot)
             << robot.value(QStringLiteral("shop_names")).toString()
             << platformsBoundToRobot(robot.value(QStringLiteral("robot_id")).toString()).join(QStringLiteral(" "));
    return haystack.join(QStringLiteral("\n")).contains(query, Qt::CaseInsensitive);
}

static QWidget* makeRobotTableActionCell(QTableWidget* table,
                                         const QJsonObject& robot,
                                         const std::weak_ptr<std::function<void()>>& refreshFn,
                                         QWidget* owner);

class RobotConfigDialog final : public QDialog
{
public:
    explicit RobotConfigDialog(QWidget* parent = nullptr, const QJsonObject& robot = {})
        : QDialog(parent)
        , m_robot(robot)
        , m_robotId(robot.value(QStringLiteral("robot_id")).toString())
    {
        setObjectName(QStringLiteral("aiBackendRobotConfigDialog"));
        setAttribute(Qt::WA_StyledBackground, true);
        setWindowTitle(m_robotId.isEmpty() ? QStringLiteral("新建机器人") : QStringLiteral("编辑机器人"));
        setMinimumSize(760, 620);
        resize(840, 680);
        setModal(true);
        setStyleSheet(QStringLiteral(R"QSS(
QDialog#aiBackendRobotConfigDialog {
  background: #f4f6f8;
}
QLabel#aiBackendRobotDialogTitle {
  color: #0f172a;
  font-size: 22px;
  font-weight: 700;
  background: transparent;
}
QLabel#aiBackendRobotDialogSubtitle,
QLabel#aiBackendRobotFieldHint,
QLabel#aiBackendRobotStatus {
  color: #64748b;
  font-size: 13px;
  background: transparent;
}
QFrame#aiBackendRobotConfigCard {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
}
QLabel#aiBackendRobotFieldLabel {
  color: #334155;
  font-size: 13px;
  font-weight: 600;
  background: transparent;
}
QLineEdit#aiBackendRobotConfigField,
QComboBox#aiBackendRobotConfigField,
QSpinBox#aiBackendRobotConfigField {
  background: #f8fafc;
  border: 1px solid #dbe3ee;
  border-radius: 8px;
  color: #0f172a;
  padding: 8px 10px;
  min-height: 24px;
  font-size: 13px;
}
QLineEdit#aiBackendRobotConfigField:focus,
QComboBox#aiBackendRobotConfigField:focus,
QSpinBox#aiBackendRobotConfigField:focus {
  background: #ffffff;
  border: 1px solid #2563eb;
}
QPlainTextEdit#aiBackendRobotShopField {
  background: #f8fafc;
  border: 1px solid #dbe3ee;
  border-radius: 8px;
  color: #0f172a;
  padding: 8px 10px;
  font-size: 13px;
}
QPlainTextEdit#aiBackendRobotShopField:focus {
  background: #ffffff;
  border: 1px solid #2563eb;
}
QCheckBox#aiBackendRobotEnabledCheck,
QCheckBox#aiBackendRobotStrategyCheck,
QCheckBox#aiBackendRobotKnowledgeCheck {
  color: #334155;
  font-size: 13px;
  background: transparent;
  spacing: 8px;
}
QPushButton#aiBackendBluePrimaryBtn {
  background: #2563eb;
  color: #ffffff;
  border: none;
  border-radius: 8px;
  padding: 9px 18px;
  font-size: 13px;
  font-weight: 600;
  min-width: 78px;
}
QPushButton#aiBackendBluePrimaryBtn:hover { background: #1d4ed8; }
QPushButton#aiBackendSecondaryBtn {
  background: #ffffff;
  color: #334155;
  border: 1px solid #cbd5e1;
  border-radius: 8px;
  padding: 9px 16px;
  font-size: 13px;
  font-weight: 600;
  min-width: 76px;
}
QPushButton#aiBackendSecondaryBtn:hover { background: #f8fafc; border-color: #94a3b8; }
QScrollArea#aiBackendRobotKnowledgeScroll {
  background: #f8fafc;
  border: 1px solid #dbe3ee;
  border-radius: 8px;
}
)QSS"));

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(22, 20, 22, 20);
        outer->setSpacing(14);

        auto* title = new QLabel(m_robotId.isEmpty() ? QStringLiteral("新建机器人")
                                                     : QStringLiteral("编辑机器人"),
                                 this);
        title->setObjectName(QStringLiteral("aiBackendRobotDialogTitle"));
        auto* subtitle = new QLabel(
            QStringLiteral("机器人用于声明模型、知识库和适用店铺。平台绑定机器人后，会按机器人配置生成客服回复。"),
            this);
        subtitle->setObjectName(QStringLiteral("aiBackendRobotDialogSubtitle"));
        subtitle->setWordWrap(true);
        outer->addWidget(title);
        outer->addWidget(subtitle);

        auto* card = new QFrame(this);
        card->setObjectName(QStringLiteral("aiBackendRobotConfigCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto* form = new QVBoxLayout(card);
        form->setContentsMargins(18, 16, 18, 16);
        form->setSpacing(14);

        auto makeField = [](const QString& labelText,
                            const QString& hintText,
                            QWidget* control,
                            QWidget* parent) -> QWidget* {
            auto* wrap = new QWidget(parent);
            auto* lay = new QVBoxLayout(wrap);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(6);
            auto* label = new QLabel(labelText, wrap);
            label->setObjectName(QStringLiteral("aiBackendRobotFieldLabel"));
            lay->addWidget(label);
            lay->addWidget(control);
            if (!hintText.trimmed().isEmpty()) {
                auto* hint = new QLabel(hintText, wrap);
                hint->setObjectName(QStringLiteral("aiBackendRobotFieldHint"));
                hint->setWordWrap(true);
                lay->addWidget(hint);
            }
            return wrap;
        };

        auto* topRow = new QHBoxLayout;
        topRow->setSpacing(14);
        m_nameEdit = new QLineEdit(card);
        m_nameEdit->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
        m_nameEdit->setPlaceholderText(QStringLiteral("例如：拼多多键盘售前机器人"));
        m_nameEdit->setText(robot.value(QStringLiteral("robot_name")).toString());
        topRow->addWidget(makeField(QStringLiteral("机器人名称"),
                                    QStringLiteral("用户可编辑，用于后台列表和平台绑定时辨识。"),
                                    m_nameEdit,
                                    card),
                          1);

        m_modelCombo = new QComboBox(card);
        m_modelCombo->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
        fillModelCombo(robot.value(QStringLiteral("model_config_id")).toString());
        topRow->addWidget(makeField(QStringLiteral("模型配置"),
                                    QStringLiteral("来源于“API 配置/模型”中已支持的模型预设。"),
                                    m_modelCombo,
                                    card),
                          1);
        form->addLayout(topRow);

        m_shopsEdit = new QPlainTextEdit(card);
        m_shopsEdit->setObjectName(QStringLiteral("aiBackendRobotShopField"));
        m_shopsEdit->setPlaceholderText(QStringLiteral("例如：官方旗舰店\n数码精品店"));
        m_shopsEdit->setPlainText(robot.value(QStringLiteral("shop_names")).toString());
        m_shopsEdit->setFixedHeight(86);
        form->addWidget(makeField(QStringLiteral("适用店铺"),
                                  QStringLiteral("首版仅作为人工说明，真正生效关系以平台按钮绑定机器人为准。"),
                                  m_shopsEdit,
                                  card));

        auto* strategyRow = new QHBoxLayout;
        strategyRow->setSpacing(14);
        m_toneCombo = new QComboBox(card);
        m_toneCombo->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
        m_toneCombo->setEditable(true);
        const QString currentTone = robot.value(QStringLiteral("reply_tone"))
                                        .toString(QStringLiteral("亲切温和、不失热情"))
                                        .trimmed();
        const QStringList toneOptions = {
            QStringLiteral("亲切温和、不失热情"),
            QStringLiteral("专业简洁"),
            QStringLiteral("活泼热情"),
            QStringLiteral("稳重耐心"),
        };
        for (const QString& tone : toneOptions)
            m_toneCombo->addItem(tone, tone);
        const int toneIndex = m_toneCombo->findText(currentTone);
        if (toneIndex >= 0)
            m_toneCombo->setCurrentIndex(toneIndex);
        else if (!currentTone.isEmpty())
            m_toneCombo->setEditText(currentTone);
        strategyRow->addWidget(makeField(QStringLiteral("回复语气"),
                                         QStringLiteral("会写入 AI 提示词，用于约束客服回复风格。"),
                                         m_toneCombo,
                                         card),
                               1);

        m_addressEdit = new QLineEdit(card);
        m_addressEdit->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
        m_addressEdit->setPlaceholderText(QStringLiteral("例如：亲、宝子"));
        m_addressEdit->setText(robot.value(QStringLiteral("common_address_terms"))
                                   .toString(QStringLiteral("亲、宝子")));
        strategyRow->addWidget(makeField(QStringLiteral("常用称呼"),
                                         QStringLiteral("多个称呼可用顿号或逗号分隔；模型会自然使用，不会强制每句都带。"),
                                         m_addressEdit,
                                         card),
                               1);
        form->addLayout(strategyRow);

        auto* autoPolicyRow = new QHBoxLayout;
        autoPolicyRow->setSpacing(18);
        m_autoSendImagesCheck = new QCheckBox(QStringLiteral("允许自动回复发送建议附图"), card);
        m_autoSendImagesCheck->setObjectName(QStringLiteral("aiBackendRobotStrategyCheck"));
        m_autoSendImagesCheck->setChecked(robot.value(QStringLiteral("allow_auto_send_images")).toBool(false));
        m_autoSendImagesCheck->setToolTip(QStringLiteral("关闭时，自动回复只发送文字；手动生成仍会显示建议附图供人工确认。"));
        autoPolicyRow->addWidget(m_autoSendImagesCheck, 0);
        m_autoSendMultiMessagesCheck = new QCheckBox(QStringLiteral("允许自动发送多条消息"), card);
        m_autoSendMultiMessagesCheck->setObjectName(QStringLiteral("aiBackendRobotStrategyCheck"));
        m_autoSendMultiMessagesCheck->setChecked(
            robot.value(QStringLiteral("allow_auto_send_multi_messages")).toBool(false));
        m_autoSendMultiMessagesCheck->setToolTip(QStringLiteral("关闭时，自动回复按单条文字消息生成和发送。"));
        autoPolicyRow->addWidget(m_autoSendMultiMessagesCheck, 0);
        m_maxAutoMessagesSpin = new QSpinBox(card);
        m_maxAutoMessagesSpin->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
        m_maxAutoMessagesSpin->setRange(1, 3);
        m_maxAutoMessagesSpin->setValue(qBound(1,
                                               robot.value(QStringLiteral("max_auto_send_messages"))
                                                   .toInt(m_autoSendMultiMessagesCheck->isChecked() ? 2 : 1),
                                               3));
        m_maxAutoMessagesSpin->setEnabled(m_autoSendMultiMessagesCheck->isChecked());
        m_maxAutoMessagesSpin->setToolTip(QStringLiteral("自动回复一次最多拆成几条文字消息，建议不超过 2 条。"));
        autoPolicyRow->addWidget(makeField(QStringLiteral("最多自动发送"),
                                           QStringLiteral("仅在允许自动发送多条消息时生效。"),
                                           m_maxAutoMessagesSpin,
                                           card),
                                 0);
        autoPolicyRow->addStretch(1);
        form->addLayout(autoPolicyRow);

        auto* knowledgeWrap = new QWidget(card);
        auto* knowledgeLay = new QVBoxLayout(knowledgeWrap);
        knowledgeLay->setContentsMargins(0, 0, 0, 0);
        knowledgeLay->setSpacing(8);
        m_knowledgeStatus = new QLabel(knowledgeWrap);
        m_knowledgeStatus->setObjectName(QStringLiteral("aiBackendRobotStatus"));
        m_knowledgeStatus->setWordWrap(true);
        knowledgeLay->addWidget(m_knowledgeStatus);

        auto* knowledgeScroll = new QScrollArea(knowledgeWrap);
        knowledgeScroll->setObjectName(QStringLiteral("aiBackendRobotKnowledgeScroll"));
        knowledgeScroll->setWidgetResizable(true);
        knowledgeScroll->setFrameShape(QFrame::NoFrame);
        knowledgeScroll->setMinimumHeight(150);
        auto* knowledgeBody = new QWidget(knowledgeScroll);
        m_knowledgeListLay = new QVBoxLayout(knowledgeBody);
        m_knowledgeListLay->setContentsMargins(12, 10, 12, 10);
        m_knowledgeListLay->setSpacing(8);
        knowledgeScroll->setWidget(knowledgeBody);
        knowledgeLay->addWidget(knowledgeScroll, 1);
        form->addWidget(makeField(QStringLiteral("知识库"),
                                  QStringLiteral("可选一个或多个知识库；不选择时，机器人生成回复但跳过知识库检索。"),
                                  knowledgeWrap,
                                  card));

        m_enabledCheck = new QCheckBox(QStringLiteral("启用该机器人"), card);
        m_enabledCheck->setObjectName(QStringLiteral("aiBackendRobotEnabledCheck"));
        m_enabledCheck->setChecked(m_robotId.isEmpty() ? true : robotConfigEnabled(robot));
        form->addWidget(m_enabledCheck);

        outer->addWidget(card, 1);

        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(0, 0, 0, 0);
        actions->addStretch(1);
        auto* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
        cancelBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setFocusPolicy(Qt::NoFocus);
        actions->addWidget(cancelBtn);
        auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
        saveBtn->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
        saveBtn->setCursor(Qt::PointingHandCursor);
        saveBtn->setFocusPolicy(Qt::NoFocus);
        actions->addWidget(saveBtn);
        outer->addLayout(actions);

        loadKnowledgeChecks(robot);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_autoSendMultiMessagesCheck, &QCheckBox::toggled, this, [this](bool checked) {
            if (m_maxAutoMessagesSpin)
                m_maxAutoMessagesSpin->setEnabled(checked);
            if (checked && m_maxAutoMessagesSpin && m_maxAutoMessagesSpin->value() < 2)
                m_maxAutoMessagesSpin->setValue(2);
        });
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            if (saveRobot())
                accept();
        });
    }

    QJsonObject savedRobot() const { return m_savedRobot; }

private:
    void fillModelCombo(const QString& selectedKey)
    {
        int selectedIndex = -1;
        for (const AiPresetDefinition& def : aiPresetDefinitions()) {
            if (!def.available)
                continue;
            const QString label = robotModelDisplayName(def.sessionModelKey);
            m_modelCombo->addItem(label, def.sessionModelKey);
            if (def.sessionModelKey == selectedKey)
                selectedIndex = m_modelCombo->count() - 1;
        }
        if (selectedIndex >= 0)
            m_modelCombo->setCurrentIndex(selectedIndex);
        else if (m_modelCombo->count() > 0)
            m_modelCombo->setCurrentIndex(0);
    }

    void addKnowledgeCheck(const QString& baseId,
                           const QString& name,
                           bool checked,
                           bool enabled = true)
    {
        if (baseId.trimmed().isEmpty())
            return;
        auto* cb = new QCheckBox(name.trimmed().isEmpty() ? baseId : name.trimmed(), this);
        cb->setObjectName(QStringLiteral("aiBackendRobotKnowledgeCheck"));
        cb->setProperty("baseId", baseId.trimmed());
        cb->setProperty("baseName", name.trimmed().isEmpty() ? baseId.trimmed() : name.trimmed());
        cb->setChecked(checked);
        cb->setEnabled(enabled);
        m_knowledgeChecks.append(cb);
        m_knowledgeListLay->addWidget(cb);
    }

    void loadKnowledgeChecks(const QJsonObject& robot)
    {
        const QStringList selectedIds = jsonStringList(robot.value(QStringLiteral("knowledge_base_ids")).toArray());
        const QStringList selectedNames = jsonStringList(robot.value(QStringLiteral("knowledge_base_names")).toArray());

        QString error;
        QJsonArray bases;
        bool serviceOk = false;
        if (Ipc::IpcService::instance().isServiceAvailable()) {
            Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
            const QJsonObject response = Ipc::IpcService::instance().fetchKnowledgeBases(5000, &status, &error);
            if (status == Ipc::ResponseStatus::Success
                && response.value(QStringLiteral("status")).toString(QStringLiteral("success")) != QLatin1String("error")) {
                bases = response.value(QStringLiteral("bases")).toArray();
                serviceOk = true;
            } else {
                error = response.value(QStringLiteral("detail")).toString(
                    response.value(QStringLiteral("error")).toString(error));
            }
        } else {
            error = QStringLiteral("Python 服务未启动");
        }

        QStringList seenIds;
        for (const QJsonValue& value : bases) {
            const QJsonObject base = value.toObject();
            const QString id = base.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty())
                continue;
            QString name = base.value(QStringLiteral("name")).toString().trimmed();
            if (!isKnowledgeBaseEnabled(base))
                name += QStringLiteral("（停用）");
            addKnowledgeCheck(id, name, selectedIds.contains(id), true);
            seenIds.append(id);
        }

        for (int i = 0; i < selectedIds.size(); ++i) {
            const QString id = selectedIds.at(i);
            if (seenIds.contains(id))
                continue;
            const QString name = i < selectedNames.size() ? selectedNames.at(i) : id;
            addKnowledgeCheck(id, QStringLiteral("%1（当前不可用）").arg(name), true, true);
        }

        if (m_knowledgeChecks.isEmpty()) {
            auto* empty = new QLabel(serviceOk ? QStringLiteral("暂无知识库。可先保存机器人，后续创建知识库后再回来绑定。")
                                               : QStringLiteral("无法读取知识库列表：%1。可先保存不绑定知识库的机器人。").arg(error.left(120)),
                                    this);
            empty->setObjectName(QStringLiteral("aiBackendRobotStatus"));
            empty->setWordWrap(true);
            m_knowledgeListLay->addWidget(empty);
        }
        m_knowledgeListLay->addStretch(1);

        if (serviceOk)
            m_knowledgeStatus->setText(QStringLiteral("已加载 %1 个知识库。").arg(bases.size()));
        else
            m_knowledgeStatus->setText(QStringLiteral("知识库列表暂不可用：%1").arg(error.left(120)));
    }

    bool saveRobot()
    {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("机器人配置"), QStringLiteral("请填写机器人名称。"));
            return false;
        }
        if (m_modelCombo->currentData().toString().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("机器人配置"), QStringLiteral("请选择模型配置。"));
            return false;
        }

        QStringList baseIds;
        QStringList baseNames;
        for (QCheckBox* cb : std::as_const(m_knowledgeChecks)) {
            if (!cb || !cb->isChecked())
                continue;
            baseIds.append(cb->property("baseId").toString());
            baseNames.append(cb->property("baseName").toString());
        }

        const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
        QJsonObject out = m_robot;
        if (m_robotId.trimmed().isEmpty()) {
            m_robotId = compactRobotId();
            out.insert(QStringLiteral("robot_id"), m_robotId);
            out.insert(QStringLiteral("created_at"), now);
        }
        out.insert(QStringLiteral("robot_name"), name);
        out.insert(QStringLiteral("model_config_id"), m_modelCombo->currentData().toString().trimmed());
        out.insert(QStringLiteral("knowledge_base_ids"), stringListToJsonArray(baseIds));
        out.insert(QStringLiteral("knowledge_base_names"), stringListToJsonArray(baseNames));
        out.insert(QStringLiteral("shop_names"), m_shopsEdit->toPlainText().trimmed());
        out.insert(QStringLiteral("reply_tone"), m_toneCombo->currentText().trimmed());
        out.insert(QStringLiteral("common_address_terms"), m_addressEdit->text().trimmed());
        out.insert(QStringLiteral("allow_auto_send_images"), m_autoSendImagesCheck->isChecked());
        out.insert(QStringLiteral("allow_auto_send_multi_messages"),
                   m_autoSendMultiMessagesCheck->isChecked());
        out.insert(QStringLiteral("max_auto_send_messages"),
                   m_autoSendMultiMessagesCheck->isChecked()
                       ? qBound(1, m_maxAutoMessagesSpin->value(), 3)
                       : 1);
        out.insert(QStringLiteral("enabled"), m_enabledCheck->isChecked());
        out.insert(QStringLiteral("updated_at"), now);
        m_savedRobot = out;
        return true;
    }

    QJsonObject m_robot;
    QJsonObject m_savedRobot;
    QString m_robotId;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QPlainTextEdit* m_shopsEdit = nullptr;
    QComboBox* m_toneCombo = nullptr;
    QLineEdit* m_addressEdit = nullptr;
    QCheckBox* m_autoSendImagesCheck = nullptr;
    QCheckBox* m_autoSendMultiMessagesCheck = nullptr;
    QSpinBox* m_maxAutoMessagesSpin = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QLabel* m_knowledgeStatus = nullptr;
    QVBoxLayout* m_knowledgeListLay = nullptr;
    QVector<QCheckBox*> m_knowledgeChecks;
};

static void upsertRobotConfig(const QJsonObject& robot)
{
    const QString robotId = robot.value(QStringLiteral("robot_id")).toString().trimmed();
    if (robotId.isEmpty())
        return;
    QJsonArray robots = loadRobotConfigs();
    bool updated = false;
    for (int i = 0; i < robots.size(); ++i) {
        if (robots.at(i).toObject().value(QStringLiteral("robot_id")).toString() == robotId) {
            robots.replace(i, robot);
            updated = true;
            break;
        }
    }
    if (!updated)
        robots.append(robot);
    saveRobotConfigs(robots);
}

static void removeRobotConfig(const QString& robotId)
{
    QJsonArray robots = loadRobotConfigs();
    QJsonArray kept;
    for (const QJsonValue& value : robots) {
        const QJsonObject robot = value.toObject();
        if (robot.value(QStringLiteral("robot_id")).toString() != robotId)
            kept.append(robot);
    }
    saveRobotConfigs(kept);
}

static void setRobotConfigEnabled(const QString& robotId, bool enabled)
{
    QJsonArray robots = loadRobotConfigs();
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (int i = 0; i < robots.size(); ++i) {
        QJsonObject robot = robots.at(i).toObject();
        if (robot.value(QStringLiteral("robot_id")).toString() != robotId)
            continue;
        robot.insert(QStringLiteral("enabled"), enabled);
        robot.insert(QStringLiteral("updated_at"), now);
        robots.replace(i, robot);
        break;
    }
    saveRobotConfigs(robots);
}

static void populateRobotConfigTable(QTableWidget* table,
                                     QLabel* statusLabel,
                                     const std::weak_ptr<std::function<void()>>& refreshFn,
                                     QWidget* owner,
                                     const QString& searchText = QString(),
                                     const QString& modelFilter = QString(),
                                     const QString& statusFilter = QString())
{
    if (!table)
        return;
    const QJsonArray robots = loadRobotConfigs();
    QSet<QString> existingRobotIds;
    for (const QJsonValue& value : robots) {
        const QString id = value.toObject().value(QStringLiteral("robot_id")).toString().trimmed();
        if (!id.isEmpty())
            existingRobotIds.insert(id);
    }
    const QStringList invalidBindings = invalidPlatformRobotBindings(existingRobotIds);
    QJsonArray filteredRobots;
    for (const QJsonValue& value : robots) {
        const QJsonObject robot = value.toObject();
        if (robotMatchesFilters(robot, searchText, modelFilter, statusFilter))
            filteredRobots.append(robot);
    }

    const QSet<QString> availableModels = availableRobotModelKeys();
    bool knowledgeIdsKnown = false;
    const QSet<QString> availableKbIds = availableKnowledgeBaseIds(&knowledgeIdsKnown);

    table->setRowCount(filteredRobots.size());
    for (int row = 0; row < filteredRobots.size(); ++row) {
        const QJsonObject robot = filteredRobots.at(row).toObject();
        const QString id = robot.value(QStringLiteral("robot_id")).toString();
        const QString name = robotDisplayText(robot.value(QStringLiteral("robot_name")).toString(),
                                              QStringLiteral("未命名机器人"));
        const QString modelKey = robot.value(QStringLiteral("model_config_id")).toString();
        const QString modelName = robotModelDisplayName(modelKey);
        const QString knowledge = robotKnowledgeDisplayName(robot);
        const QString shops = robotDisplayText(robot.value(QStringLiteral("shop_names")).toString(),
                                               QStringLiteral("未填写"));
        const bool enabled = robotConfigEnabled(robot);
        const QStringList boundPlatforms = platformsBoundToRobot(id);
        QStringList warnings;
        if (modelKey.trimmed().isEmpty() || !availableModels.contains(modelKey.trimmed()))
            warnings.append(QStringLiteral("模型配置不可用"));
        if (knowledgeIdsKnown) {
            QStringList missingKbIds;
            for (const QString& kbId : robotKnowledgeIdSet(robot)) {
                if (!availableKbIds.contains(kbId))
                    missingKbIds.append(kbId);
            }
            if (!missingKbIds.isEmpty())
                warnings.append(QStringLiteral("知识库不存在：%1").arg(missingKbIds.join(QStringLiteral("、"))));
        }
        QString statusText = enabled ? QStringLiteral("启用") : QStringLiteral("停用");
        if (!warnings.isEmpty())
            statusText += QStringLiteral(" / 配置异常");

        table->setItem(row, 0, makeKnowledgeCellItem(QStringLiteral("%1 / %2").arg(name, id)));
        table->setItem(row, 1, makeKnowledgeCellItem(modelName));
        table->setItem(row, 2, makeKnowledgeCellItem(knowledge));
        table->setItem(row, 3, makeKnowledgeCellItem(shops));
        table->setItem(row, 4, makeKnowledgeCellItem(statusText));
        QStringList nameTips{QStringLiteral("%1\nID：%2").arg(name, id)};
        if (!boundPlatforms.isEmpty())
            nameTips.append(QStringLiteral("已绑定平台：%1").arg(boundPlatforms.join(QStringLiteral("、"))));
        table->item(row, 0)->setToolTip(nameTips.join(QStringLiteral("\n")));
        table->item(row, 1)->setToolTip(modelKey);
        table->item(row, 2)->setToolTip(knowledge);
        table->item(row, 3)->setToolTip(shops);
        const QString strategyTip = robotStrategyDisplayName(robot);
        if (!warnings.isEmpty()) {
            table->item(row, 4)->setForeground(QColor(QStringLiteral("#dc2626")));
            table->item(row, 4)->setToolTip(warnings.join(QStringLiteral("\n")) + QStringLiteral("\n\n") + strategyTip);
        } else if (!boundPlatforms.isEmpty()) {
            table->item(row, 4)->setToolTip(QStringLiteral("已绑定平台：%1\n\n%2")
                                                .arg(boundPlatforms.join(QStringLiteral("、")), strategyTip));
        } else {
            table->item(row, 4)->setToolTip(strategyTip);
        }
        table->setCellWidget(row, 5, makeRobotTableActionCell(table, robot, refreshFn, owner));
    }

    auto* header = table->horizontalHeader();
    header->setMinimumSectionSize(80);
    for (int c = 0; c < table->columnCount(); ++c)
        header->setSectionResizeMode(c, QHeaderView::Interactive);
    table->setColumnWidth(0, 190);
    table->setColumnWidth(1, 180);
    table->setColumnWidth(2, 240);
    table->setColumnWidth(3, 180);
    table->setColumnWidth(4, 140);
    table->setColumnWidth(5, 410);

    if (statusLabel) {
        if (robots.isEmpty()) {
            QString text = QStringLiteral("暂无机器人。点击“新建机器人”创建一套模型与知识库策略。");
            if (!invalidBindings.isEmpty())
                text += QStringLiteral(" 发现失效平台绑定：%1。").arg(invalidBindings.join(QStringLiteral("；")).left(120));
            statusLabel->setText(text);
        } else if (filteredRobots.isEmpty()) {
            QString text = QStringLiteral("没有匹配的机器人。可调整搜索关键词、模型或状态筛选。");
            if (!invalidBindings.isEmpty())
                text += QStringLiteral(" 发现失效平台绑定：%1。").arg(invalidBindings.join(QStringLiteral("；")).left(120));
            statusLabel->setText(text);
        } else {
            const QString validationText = knowledgeIdsKnown
                ? QStringLiteral("已校验知识库引用。")
                : QStringLiteral("Python 服务未启动，暂未校验知识库引用。");
            QString invalidText;
            if (!invalidBindings.isEmpty())
                invalidText = QStringLiteral(" 发现失效平台绑定：%1。").arg(invalidBindings.join(QStringLiteral("；")).left(120));
            statusLabel->setText(QStringLiteral("已加载 %1 个机器人，当前显示 %2 个。支持搜索、筛选、复制，并提示平台绑定和失效配置。%3%4")
                                     .arg(robots.size())
                                     .arg(filteredRobots.size())
                                     .arg(validationText)
                                     .arg(invalidText));
        }
    }
}

static QWidget* makeRobotTableActionCell(QTableWidget* table,
                                         const QJsonObject& robot,
                                         const std::weak_ptr<std::function<void()>>& refreshFn,
                                         QWidget* owner)
{
    const QString robotId = robot.value(QStringLiteral("robot_id")).toString();
    const QString robotName = robot.value(QStringLiteral("robot_name")).toString(QStringLiteral("未命名机器人"));
    const bool enabled = robotConfigEnabled(robot);

    auto* actionCell = new QWidget(table);
    auto* actionLay = new QHBoxLayout(actionCell);
    actionLay->setContentsMargins(4, 4, 4, 4);
    actionLay->setSpacing(8);
    auto* editBtn = new QPushButton(QStringLiteral("编辑"), actionCell);
    editBtn->setObjectName(QStringLiteral("aiBackendTableActionBtn"));
    editBtn->setCursor(Qt::PointingHandCursor);
    editBtn->setFocusPolicy(Qt::NoFocus);
    editBtn->setMinimumWidth(70);
    auto* testBtn = new QPushButton(QStringLiteral("测试回复"), actionCell);
    testBtn->setObjectName(QStringLiteral("aiBackendTableActionPrimaryBtn"));
    testBtn->setCursor(Qt::PointingHandCursor);
    testBtn->setFocusPolicy(Qt::NoFocus);
    testBtn->setMinimumWidth(82);
    auto* copyBtn = new QPushButton(QStringLiteral("复制"), actionCell);
    copyBtn->setObjectName(QStringLiteral("aiBackendTableActionBtn"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFocusPolicy(Qt::NoFocus);
    copyBtn->setMinimumWidth(70);
    auto* toggleBtn = new QPushButton(enabled ? QStringLiteral("停用") : QStringLiteral("启用"), actionCell);
    toggleBtn->setObjectName(enabled ? QStringLiteral("aiBackendTableActionBtn")
                                     : QStringLiteral("aiBackendTableActionPrimaryBtn"));
    toggleBtn->setCursor(Qt::PointingHandCursor);
    toggleBtn->setFocusPolicy(Qt::NoFocus);
    toggleBtn->setMinimumWidth(70);
    auto* deleteBtn = new QPushButton(QStringLiteral("删除"), actionCell);
    deleteBtn->setObjectName(QStringLiteral("aiBackendTableActionDangerBtn"));
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setFocusPolicy(Qt::NoFocus);
    deleteBtn->setMinimumWidth(70);
    actionLay->addWidget(testBtn, 0);
    actionLay->addWidget(editBtn, 0);
    actionLay->addWidget(copyBtn, 0);
    actionLay->addWidget(toggleBtn, 0);
    actionLay->addWidget(deleteBtn, 0);
    actionLay->addStretch(1);

    QObject::connect(testBtn, &QPushButton::clicked, owner, [owner, robot]() {
        RobotSandboxDialog dialog(robot, owner);
        dialog.exec();
    });
    QObject::connect(editBtn, &QPushButton::clicked, owner, [owner, robot, refreshFn]() {
        RobotConfigDialog dialog(owner, robot);
        if (dialog.exec() == QDialog::Accepted) {
            upsertRobotConfig(dialog.savedRobot());
            if (const auto locked = refreshFn.lock(); locked && *locked)
            (*locked)();
        }
    });
    QObject::connect(copyBtn, &QPushButton::clicked, owner, [robot, refreshFn]() {
        upsertRobotConfig(cloneRobotConfig(robot));
        if (const auto locked = refreshFn.lock(); locked && *locked)
            (*locked)();
    });
    QObject::connect(toggleBtn, &QPushButton::clicked, owner, [robotId, enabled, refreshFn]() {
        setRobotConfigEnabled(robotId, !enabled);
        if (const auto locked = refreshFn.lock(); locked && *locked)
            (*locked)();
    });
    QObject::connect(deleteBtn, &QPushButton::clicked, owner, [owner, robotId, robotName, refreshFn]() {
        const QStringList boundPlatforms = platformsBoundToRobot(robotId);
        QString message = QStringLiteral("确定删除“%1”吗？删除后该机器人配置不可恢复。").arg(robotName);
        if (!boundPlatforms.isEmpty()) {
            message += QStringLiteral("\n\n该机器人已绑定平台：%1。\n删除后这些平台会显示机器人失效，需要重新绑定。")
                           .arg(boundPlatforms.join(QStringLiteral("、")));
        }
        const int ret = QMessageBox::question(
            owner,
            QStringLiteral("删除机器人"),
            message,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
        removeRobotConfig(robotId);
        if (const auto locked = refreshFn.lock(); locked && *locked)
            (*locked)();
    });

    return actionCell;
}

static QWidget* wrapListPlainText(const QString& text, bool titleStyle, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 2, 8, 8);
    v->setSpacing(0);
    auto* lab = new QLabel(text, w);
    lab->setObjectName(titleStyle ? QStringLiteral("aiBackendTableCellTitle") : QStringLiteral("aiBackendTableCellBody"));
    lab->setWordWrap(true);
    v->addWidget(lab, 0, Qt::AlignTop | Qt::AlignLeft);
    v->addStretch(1);
    return w;
}

/** 知识库列表：列标题（与机器人列表同视觉体系） */
static QWidget* buildProductKbListHeader(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(20, 20, 20, 10);
    h->setSpacing(20);
    auto addHdr = [&](const QString& t, int stretch) {
        auto* lab = new QLabel(t, w);
        lab->setObjectName(QStringLiteral("aiBackendRobotListColHeader"));
        lab->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lab->setWordWrap(false);
        h->addWidget(lab, stretch, Qt::AlignTop);
    };
    addHdr(QStringLiteral("知识库名称"), 3);
    addHdr(QStringLiteral("绑定店铺"), 2);
    addHdr(QStringLiteral("商品数量"), 1);
    addHdr(QStringLiteral("同步状态"), 1);
    addHdr(QStringLiteral("最后更新"), 1);
    addHdr(QStringLiteral("操作"), 2);
    return w;
}

static QWidget* buildProductKbDataRow(
    QWidget* parent,
    const QString& kbName,
    const QString& storeName,
    const QString& productCount,
    bool syncDone,
    const QString& lastUpdate,
    const std::initializer_list<std::tuple<QString, QString>>& actions)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("aiBackendRobotListRow"));
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(20);

    const auto addCell = [&](QWidget* cell, int stretch) {
        lay->addWidget(cell, stretch, Qt::AlignTop);
    };

    addCell(wrapListPlainText(kbName, true, row), 3);
    addCell(wrapListPlainText(storeName, false, row), 2);
    addCell(wrapListPlainText(productCount, false, row), 1);

    const QString syncText = syncDone ? QStringLiteral("● 已同步") : QStringLiteral("⟳ 同步中");
    const QString syncObj = syncDone ? QStringLiteral("aiBackendSyncDone") : QStringLiteral("aiBackendSyncProgress");
    addCell(wrapTopHCenterInCell(makePillLabel(syncText, syncObj, row), row), 1);

    addCell(wrapListPlainText(lastUpdate, false, row), 1);
    addCell(wrapActionLinks(row, actions), 2);
    return row;
}

static QWidget* buildRobotStoreConfigPage(std::function<void()>* refreshOut = nullptr)
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("aiBackendSubPage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 28);
    outer->setSpacing(16);

    auto* titleRow = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("店铺机器人配置"), page);
    title->setObjectName(QStringLiteral("aiBackendDashTitle"));
    auto* sub = new QLabel(
        QStringLiteral("管理不同店铺或场景使用的 AI 客服机器人。机器人会声明模型、知识库和启用状态，后续可绑定到聚合平台。"), page);
    sub->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    sub->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    titleRow->addLayout(titleCol, 1);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新列表"), page);
    refreshBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(refreshBtn, 0, Qt::AlignTop);
    auto* newBtn = new QPushButton(QStringLiteral("+ 新建机器人"), page);
    newBtn->setObjectName(QStringLiteral("aiBackendPurplePrimaryBtn"));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(newBtn, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("aiBackendDataCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    auto* statusLabel = new QLabel(
        QStringLiteral("阶段一支持机器人配置 CRUD。平台绑定和生成回复链路接入会在下一阶段实现。"),
        card);
    statusLabel->setObjectName(QStringLiteral("aiBackendKnowledgeStatus"));
    statusLabel->setWordWrap(true);
    cardLay->addWidget(statusLabel, 0);

    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(10);
    auto* searchEdit = new QLineEdit(card);
    searchEdit->setObjectName(QStringLiteral("aiBackendKnowledgeDirectoryEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("搜索机器人名称、ID、模型、知识库、店铺或绑定平台"));
    searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(searchEdit, 1);

    auto* modelFilterCombo = new QComboBox(card);
    modelFilterCombo->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
    modelFilterCombo->addItem(QStringLiteral("全部模型"), QString());
    for (const AiPresetDefinition& def : aiPresetDefinitions()) {
        if (!def.available || def.sessionModelKey.trimmed().isEmpty())
            continue;
        modelFilterCombo->addItem(def.label.trimmed().isEmpty() ? def.sessionModelKey : def.label,
                                  def.sessionModelKey);
    }
    modelFilterCombo->setMinimumWidth(150);
    filterRow->addWidget(modelFilterCombo, 0);

    auto* statusFilterCombo = new QComboBox(card);
    statusFilterCombo->setObjectName(QStringLiteral("aiBackendRobotConfigField"));
    statusFilterCombo->addItem(QStringLiteral("全部状态"), QString());
    statusFilterCombo->addItem(QStringLiteral("启用"), QStringLiteral("enabled"));
    statusFilterCombo->addItem(QStringLiteral("停用"), QStringLiteral("disabled"));
    statusFilterCombo->addItem(QStringLiteral("已绑定平台"), QStringLiteral("bound"));
    statusFilterCombo->setMinimumWidth(120);
    filterRow->addWidget(statusFilterCombo, 0);
    cardLay->addLayout(filterRow);

    auto* table = new QTableWidget(card);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({
        QStringLiteral("机器人名称 / ID"),
        QStringLiteral("模型配置"),
        QStringLiteral("知识库"),
        QStringLiteral("适用店铺"),
        QStringLiteral("状态"),
        QStringLiteral("操作"),
    });
    styleBackendDataTable(table);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setDefaultSectionSize(58);
    cardLay->addWidget(table, 1);
    outer->addWidget(card, 1);

    auto* banner = new QFrame(page);
    banner->setObjectName(QStringLiteral("aiBackendInfoBanner"));
    auto* bh = new QHBoxLayout(banner);
    bh->setContentsMargins(16, 14, 16, 14);
    bh->setSpacing(12);
    auto* icon = new QLabel(QStringLiteral("i"), banner);
    icon->setObjectName(QStringLiteral("aiBackendInfoBannerIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(28, 28);
    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(6);
    auto* bt = new QLabel(QStringLiteral("关于机器人配置"), banner);
    bt->setObjectName(QStringLiteral("aiBackendInfoBannerTitle"));
    auto* bd = new QLabel(
        QStringLiteral("机器人是一套客服回复策略：包含模型、知识库、适用店铺和启用状态。阶段一只保存后台配置，不会影响当前聚合界面的回复生成。"),
        banner);
    bd->setObjectName(QStringLiteral("aiBackendInfoBannerBody"));
    bd->setWordWrap(true);
    textCol->addWidget(bt);
    textCol->addWidget(bd);
    bh->addWidget(icon, 0, Qt::AlignTop);
    bh->addLayout(textCol, 1);
    outer->addWidget(banner, 0);
    outer->addStretch(0);

    auto refreshRobots = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> refreshWeak(refreshRobots);
    *refreshRobots = [table, statusLabel, refreshWeak, page, searchEdit, modelFilterCombo, statusFilterCombo]() {
        populateRobotConfigTable(table,
                                 statusLabel,
                                 refreshWeak,
                                 page,
                                 searchEdit->text(),
                                 modelFilterCombo->currentData().toString(),
                                 statusFilterCombo->currentData().toString());
    };

    if (refreshOut) {
        *refreshOut = [refreshWeak]() {
            if (const auto locked = refreshWeak.lock(); locked && *locked)
                (*locked)();
        };
    }

    QObject::connect(refreshBtn, &QPushButton::clicked, page, [refreshRobots]() {
        if (refreshRobots && *refreshRobots)
            (*refreshRobots)();
    });
    QObject::connect(searchEdit, &QLineEdit::textChanged, page, [refreshRobots]() {
        if (refreshRobots && *refreshRobots)
            (*refreshRobots)();
    });
    QObject::connect(modelFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [refreshRobots]() {
        if (refreshRobots && *refreshRobots)
            (*refreshRobots)();
    });
    QObject::connect(statusFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [refreshRobots]() {
        if (refreshRobots && *refreshRobots)
            (*refreshRobots)();
    });
    QObject::connect(newBtn, &QPushButton::clicked, page, [page, refreshRobots]() {
        RobotConfigDialog dialog(page);
        if (dialog.exec() == QDialog::Accepted) {
            upsertRobotConfig(dialog.savedRobot());
            if (refreshRobots && *refreshRobots)
                (*refreshRobots)();
        }
    });

    if (refreshRobots && *refreshRobots)
        (*refreshRobots)();
    return page;
}

static QTableWidgetItem* makeKnowledgeCellItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setToolTip(text);
    item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    return item;
}

static QString joinKnowledgeStringArray(const QJsonArray& values)
{
    QStringList out;
    out.reserve(values.size());
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out.join(QStringLiteral("、"));
}

static QString formatKnowledgeImportStatus(const QJsonObject& base)
{
    const QString status = base.value(QStringLiteral("last_import_status")).toString().trimmed();
    if (status.isEmpty())
        return QStringLiteral("未导入");
    const QString at = base.value(QStringLiteral("last_import_at")).toString().trimmed();
    const int docCount = base.value(QStringLiteral("last_import_document_count")).toInt();
    const int imageCount = base.value(QStringLiteral("last_import_image_count")).toInt();
    const QString prefix = status == QLatin1String("success")
        ? QStringLiteral("成功")
        : status == QLatin1String("partial")
            ? QStringLiteral("部分失败")
            : status == QLatin1String("running")
                ? QStringLiteral("导入中")
                : status == QLatin1String("error")
                    ? QStringLiteral("失败")
                    : status;
    QString text = prefix;
    if (docCount > 0 || imageCount > 0)
        text += QStringLiteral("：文档 %1，图片 %2").arg(docCount).arg(imageCount);
    if (!at.isEmpty())
        text += QStringLiteral(" / %1").arg(at);
    return text;
}

static void showKnowledgeImagePreview(QWidget* parent, const QString& imagePath, const QString& title)
{
    const QString path = imagePath.trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::warning(parent, QStringLiteral("图片预览"), QStringLiteral("图片文件不存在或已移动。"));
        return;
    }
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        QMessageBox::warning(parent, QStringLiteral("图片预览"), QStringLiteral("图片加载失败。"));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title.trimmed().isEmpty() ? QStringLiteral("图片预览") : title.trimmed());
    dialog.resize(780, 620);
    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* imageLabel = new QLabel(scroll);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(520, 420);
    imageLabel->setPixmap(pixmap.scaled(720, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    scroll->setWidget(imageLabel);
    outer->addWidget(scroll, 1);

    auto* info = new QLabel(QDir::toNativeSeparators(path), &dialog);
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(info, 0);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* openBtn = new QPushButton(QStringLiteral("打开原图"), &dialog);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), &dialog);
    buttons->addWidget(openBtn);
    buttons->addWidget(closeBtn);
    outer->addLayout(buttons);

    QObject::connect(openBtn, &QPushButton::clicked, &dialog, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

static void populateKnowledgeDocumentTable(QTableWidget* table, const QJsonArray& documents)
{
    if (!table)
        return;
    table->setRowCount(documents.size());
    for (int row = 0; row < documents.size(); ++row) {
        const QJsonObject doc = documents.at(row).toObject();
        const QString title = doc.value(QStringLiteral("title")).toString(
            doc.value(QStringLiteral("original_filename")).toString());
        const QString filename = doc.value(QStringLiteral("original_filename")).toString(title);
        const QString displayTitle = title.trimmed().isEmpty() ? filename : title;
        const QString fileType = doc.value(QStringLiteral("file_type")).toString().toUpper();
        const QString status = doc.value(QStringLiteral("status")).toString();
        const QString errorMessage = doc.value(QStringLiteral("error_message")).toString();
        const int chunkCount = doc.value(QStringLiteral("chunk_count")).toInt();
        const QString updatedAt = doc.value(QStringLiteral("updated_at")).toString();
        const QString path = doc.value(QStringLiteral("file_path")).toString();

        table->setItem(row, 0, makeKnowledgeCellItem(displayTitle));
        table->setItem(row, 1, makeKnowledgeCellItem(fileType.isEmpty() ? QStringLiteral("-") : fileType));
        table->setItem(row, 2, makeKnowledgeCellItem(status.isEmpty() ? QStringLiteral("-") : status));
        table->setItem(row, 3, makeKnowledgeCellItem(QString::number(chunkCount)));
        table->setItem(row, 4, makeKnowledgeCellItem(updatedAt));
        table->item(row, 0)->setToolTip(path.isEmpty() ? filename : path);
        if (!errorMessage.trimmed().isEmpty())
            table->item(row, 2)->setToolTip(errorMessage);
    }
    applyBackendTableColumnSizing(table, {0});
}

static void populateKnowledgeImageTable(QTableWidget* table, const QJsonArray& images)
{
    if (!table)
        return;
    table->setRowCount(images.size());
    for (int row = 0; row < images.size(); ++row) {
        const QJsonObject image = images.at(row).toObject();
        const QString title = image.value(QStringLiteral("title")).toString(
            image.value(QStringLiteral("original_filename")).toString());
        const QString filename = image.value(QStringLiteral("original_filename")).toString(title);
        const QString displayTitle = title.trimmed().isEmpty() ? filename : title;
        const QString fileType = image.value(QStringLiteral("file_type")).toString().toUpper();
        const QString analysisStatus = image.value(QStringLiteral("analysis_status")).toString();
        const QString assetType = image.value(QStringLiteral("asset_type")).toString();
        const QString tags = joinKnowledgeStringArray(image.value(QStringLiteral("tags")).toArray());
        const QString riskTags = joinKnowledgeStringArray(image.value(QStringLiteral("risk_tags")).toArray());
        const QString updatedAt = image.value(QStringLiteral("updated_at")).toString();
        const QString path = image.value(QStringLiteral("file_path")).toString();
        const QString summary = image.value(QStringLiteral("summary")).toString();
        const QJsonObject rawAnalysis = image.value(QStringLiteral("raw_analysis")).toObject();
        const QString errorMessage = rawAnalysis.value(QStringLiteral("error")).toString();

        table->setItem(row, 0, makeKnowledgeCellItem(displayTitle));
        table->setItem(row, 1, makeKnowledgeCellItem(fileType.isEmpty() ? QStringLiteral("-") : fileType));
        table->setItem(row, 2, makeKnowledgeCellItem(analysisStatus.isEmpty() ? QStringLiteral("-") : analysisStatus));
        table->setItem(row, 3, makeKnowledgeCellItem(assetType.isEmpty() ? QStringLiteral("-") : assetType));
        table->setItem(row, 4, makeKnowledgeCellItem(tags.isEmpty() ? QStringLiteral("-") : tags));
        table->setItem(row, 5, makeKnowledgeCellItem(riskTags.isEmpty() ? QStringLiteral("-") : riskTags));
        table->setItem(row, 6, makeKnowledgeCellItem(updatedAt));
        table->item(row, 0)->setToolTip(path.isEmpty() ? filename : path);
        if (!summary.trimmed().isEmpty())
            table->item(row, 3)->setToolTip(summary);
        if (!errorMessage.trimmed().isEmpty())
            table->item(row, 2)->setToolTip(errorMessage);

        auto* actionCell = new QWidget(table);
        auto* actionLay = new QHBoxLayout(actionCell);
        actionLay->setContentsMargins(4, 4, 4, 4);
        actionLay->setSpacing(6);
        auto* previewBtn = new QPushButton(QStringLiteral("预览"), actionCell);
        previewBtn->setObjectName(QStringLiteral("aiBackendTableActionBtn"));
        previewBtn->setCursor(Qt::PointingHandCursor);
        previewBtn->setFocusPolicy(Qt::NoFocus);
        previewBtn->setEnabled(!path.trimmed().isEmpty());
        previewBtn->setMinimumWidth(70);
        actionLay->addWidget(previewBtn, 0);
        actionLay->addStretch(1);
        table->setCellWidget(row, 7, actionCell);
        QObject::connect(previewBtn, &QPushButton::clicked, table, [table, path, displayTitle]() {
            showKnowledgeImagePreview(table, path, displayTitle);
        });
    }
    applyBackendTableColumnSizing(table, {0, 4});
}

static void refreshKnowledgeTables(QTableWidget* documentTable,
                                   QTableWidget* imageTable,
                                   QLabel* statusLabel,
                                   const QString& baseId = QString())
{
    if (!documentTable || !imageTable || !statusLabel)
        return;

    QString error;
    if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
        documentTable->setRowCount(0);
        imageTable->setRowCount(0);
        statusLabel->setText(QStringLiteral("Python 服务不可用，无法读取知识库素材：%1").arg(error.left(120)));
        return;
    }

    Ipc::ResponseStatus docStatus = Ipc::ResponseStatus::Error;
    const QJsonObject docResponse = Ipc::IpcService::instance().fetchKnowledgeDocuments(
        baseId, 5000, &docStatus, &error);
    if (docStatus != Ipc::ResponseStatus::Success
        || docResponse.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        documentTable->setRowCount(0);
        imageTable->setRowCount(0);
        const QString detail = docResponse.value(QStringLiteral("detail")).toString(
            docResponse.value(QStringLiteral("error")).toString(error));
        statusLabel->setText(QStringLiteral("读取知识库文档失败：%1").arg(detail.left(120)));
        return;
    }

    Ipc::ResponseStatus imageStatus = Ipc::ResponseStatus::Error;
    QString imageError;
    const QJsonObject imageResponse = Ipc::IpcService::instance().fetchKnowledgeImages(
        baseId, 5000, &imageStatus, &imageError);
    if (imageStatus != Ipc::ResponseStatus::Success
        || imageResponse.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        documentTable->setRowCount(0);
        imageTable->setRowCount(0);
        const QString detail = imageResponse.value(QStringLiteral("detail")).toString(
            imageResponse.value(QStringLiteral("error")).toString(imageError));
        statusLabel->setText(QStringLiteral("读取图片素材失败：%1").arg(detail.left(120)));
        return;
    }

    const QJsonArray documents = docResponse.value(QStringLiteral("documents")).toArray();
    const QJsonArray images = imageResponse.value(QStringLiteral("images")).toArray();
    populateKnowledgeDocumentTable(documentTable, documents);
    populateKnowledgeImageTable(imageTable, images);
    statusLabel->setText(QStringLiteral("已加载 %1 个文档，%2 个图片素材。").arg(documents.size()).arg(images.size()));
}

static void finishKnowledgeImportUi(const QJsonObject& response,
                                    QTableWidget* documentTable,
                                    QTableWidget* imageTable,
                                    QLabel* statusLabel,
                                    const QString& baseId = QString())
{
    const QJsonArray documents = response.value(QStringLiteral("documents")).toArray();
    const QJsonArray images = response.value(QStringLiteral("images")).toArray();
    int failedDocCount = 0;
    for (const QJsonValue& value : documents) {
        if (value.toObject().value(QStringLiteral("status")).toString() == QLatin1String("failed"))
            ++failedDocCount;
    }
    int failedImageCount = 0;
    for (const QJsonValue& value : images) {
        const QString state = value.toObject().value(QStringLiteral("analysis_status")).toString();
        if (state == QLatin1String("error"))
            ++failedImageCount;
    }
    const QString message = (failedDocCount > 0 || failedImageCount > 0)
        ? QStringLiteral("导入完成：文档 %1 个（失败 %2 个），图片素材 %3 个（失败 %4 个）。失败原因可查看列表状态提示。")
              .arg(documents.size())
              .arg(failedDocCount)
              .arg(images.size())
              .arg(failedImageCount)
        : QStringLiteral("导入完成：共处理 %1 个文档，%2 个图片素材。")
              .arg(documents.size())
              .arg(images.size());
    refreshKnowledgeTables(documentTable, imageTable, statusLabel, baseId);
    statusLabel->setText(message);
}

static bool isKnowledgeBaseEnabled(const QJsonObject& base)
{
    const QJsonValue value = base.value(QStringLiteral("enabled"));
    if (value.isBool())
        return value.toBool();
    return value.toInt(1) != 0;
}

static QString displayKnowledgeText(const QString& text, const QString& fallback = QStringLiteral("-"))
{
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

class KnowledgeBaseConfigDialog final : public QDialog
{
public:
    explicit KnowledgeBaseConfigDialog(QWidget* parent = nullptr, const QJsonObject& base = {})
        : QDialog(parent)
        , m_base(base)
        , m_baseId(base.value(QStringLiteral("id")).toString())
    {
        setObjectName(QStringLiteral("aiBackendKnowledgeConfigDialog"));
        setAttribute(Qt::WA_StyledBackground, true);
        setWindowTitle(m_baseId.isEmpty() ? QStringLiteral("新建知识库") : QStringLiteral("知识库配置"));
        setMinimumSize(920, 720);
        resize(1000, 780);
        setModal(true);
        setStyleSheet(QStringLiteral(R"QSS(
QDialog#aiBackendKnowledgeConfigDialog {
  background: #f4f6f8;
}
QLabel#aiBackendKnowledgeDialogTitle {
  color: #0f172a;
  font-size: 22px;
  font-weight: 700;
  background: transparent;
}
QLabel#aiBackendKnowledgeDialogSubtitle {
  color: #64748b;
  font-size: 13px;
  background: transparent;
}
QFrame#aiBackendKnowledgeConfigCard {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
}
QLabel#aiBackendKnowledgeFieldLabel {
  color: #334155;
  font-size: 13px;
  font-weight: 600;
  background: transparent;
}
QLabel#aiBackendKnowledgeFieldHint {
  color: #94a3b8;
  font-size: 12px;
  background: transparent;
}
QLineEdit#aiBackendKnowledgeConfigField {
  background: #f8fafc;
  border: 1px solid #dbe3ee;
  border-radius: 8px;
  color: #0f172a;
  padding: 8px 10px;
  min-height: 24px;
  font-size: 13px;
  selection-background-color: #bfdbfe;
  selection-color: #0f172a;
}
QLineEdit#aiBackendKnowledgeConfigField:focus {
  background: #ffffff;
  border: 1px solid #2563eb;
}
QCheckBox#aiBackendKnowledgeEnabledCheck {
  color: #334155;
  font-size: 13px;
  background: transparent;
  spacing: 8px;
}
QCheckBox#aiBackendKnowledgeEnabledCheck::indicator {
  width: 16px;
  height: 16px;
}
QPushButton#aiBackendBluePrimaryBtn {
  background: #2563eb;
  color: #ffffff;
  border: none;
  border-radius: 8px;
  padding: 9px 18px;
  font-size: 13px;
  font-weight: 600;
  min-width: 78px;
}
QPushButton#aiBackendBluePrimaryBtn:hover { background: #1d4ed8; }
QPushButton#aiBackendBluePrimaryBtn:disabled { background: #93c5fd; color: #eff6ff; }
QPushButton#aiBackendSecondaryBtn {
  background: #ffffff;
  color: #334155;
  border: 1px solid #cbd5e1;
  border-radius: 8px;
  padding: 9px 16px;
  font-size: 13px;
  font-weight: 600;
  min-width: 76px;
}
QPushButton#aiBackendSecondaryBtn:hover { background: #f8fafc; border-color: #94a3b8; }
QPushButton#aiBackendTableActionBtn {
  background: #ffffff;
  color: #0f172a;
  border: 1px solid #cbd5e1;
  border-radius: 8px;
  padding: 6px 10px;
  font-size: 13px;
  font-weight: 600;
  min-width: 66px;
  min-height: 20px;
}
QPushButton#aiBackendTableActionBtn:hover { background: #f8fafc; border-color: #94a3b8; }
QPushButton#aiBackendTableActionBtn:disabled { color: #94a3b8; background: #f8fafc; }
QLabel#aiBackendKnowledgeStatus {
  color: #64748b;
  font-size: 13px;
  background: transparent;
}
QTabWidget#aiBackendKnowledgeTabs {
  background: #ffffff;
}
QTabWidget#aiBackendKnowledgeTabs::pane {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  top: -1px;
}
QTabBar::tab {
  background: #eef2f7;
  color: #475569;
  border: 1px solid #dbe3ee;
  border-bottom: none;
  padding: 8px 14px;
  margin-right: 4px;
  border-top-left-radius: 7px;
  border-top-right-radius: 7px;
  font-size: 13px;
  font-weight: 600;
}
QTabBar::tab:selected {
  background: #ffffff;
  color: #1d4ed8;
}
QTableWidget#aiBackendDataTable {
  background: #ffffff;
  border: none;
  gridline-color: transparent;
  alternate-background-color: #f8fafc;
  color: #334155;
}
QTableWidget#aiBackendDataTable::item {
  padding: 6px 8px;
  border: none;
  color: #334155;
  font-size: 13px;
}
QTableWidget#aiBackendDataTable QHeaderView::section {
  background: #f8fafc;
  color: #64748b;
  font-size: 13px;
  font-weight: 600;
  padding: 10px 8px;
  border: none;
  border-bottom: 1px solid #e2e8f0;
}
)QSS"));

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(22, 20, 22, 20);
        outer->setSpacing(14);

        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("aiBackendKnowledgeConfigHeader"));
        auto* headerLay = new QVBoxLayout(header);
        headerLay->setContentsMargins(2, 0, 2, 0);
        headerLay->setSpacing(4);
        auto* titleLabel = new QLabel(m_baseId.isEmpty() ? QStringLiteral("新建知识库")
                                                         : QStringLiteral("编辑知识库"),
                                      header);
        titleLabel->setObjectName(QStringLiteral("aiBackendKnowledgeDialogTitle"));
        auto* subtitleLabel = new QLabel(
            QStringLiteral("配置知识库名称、适用店铺说明和本地素材目录；保存后可导入文档与图片素材。"),
            header);
        subtitleLabel->setObjectName(QStringLiteral("aiBackendKnowledgeDialogSubtitle"));
        subtitleLabel->setWordWrap(true);
        headerLay->addWidget(titleLabel);
        headerLay->addWidget(subtitleLabel);
        outer->addWidget(header, 0);

        auto* formFrame = new QFrame(this);
        formFrame->setObjectName(QStringLiteral("aiBackendKnowledgeConfigCard"));
        formFrame->setAttribute(Qt::WA_StyledBackground, true);
        auto* formLay = new QVBoxLayout(formFrame);
        formLay->setContentsMargins(18, 16, 18, 16);
        formLay->setSpacing(14);

        auto makeField = [](const QString& labelText,
                            const QString& hintText,
                            QWidget* control,
                            QWidget* parent) -> QWidget* {
            auto* wrap = new QWidget(parent);
            wrap->setObjectName(QStringLiteral("aiBackendKnowledgeFieldWrap"));
            auto* lay = new QVBoxLayout(wrap);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(6);
            auto* label = new QLabel(labelText, wrap);
            label->setObjectName(QStringLiteral("aiBackendKnowledgeFieldLabel"));
            lay->addWidget(label);
            lay->addWidget(control);
            if (!hintText.trimmed().isEmpty()) {
                auto* hint = new QLabel(hintText, wrap);
                hint->setObjectName(QStringLiteral("aiBackendKnowledgeFieldHint"));
                hint->setWordWrap(true);
                lay->addWidget(hint);
            }
            return wrap;
        };

        auto* topRow = new QHBoxLayout;
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(14);

        m_nameEdit = new QLineEdit(formFrame);
        m_nameEdit->setObjectName(QStringLiteral("aiBackendKnowledgeConfigField"));
        m_nameEdit->setPlaceholderText(QStringLiteral("例如：键盘商品知识库、常见问答知识库、售后政策知识库"));
        m_nameEdit->setText(base.value(QStringLiteral("name")).toString());
        topRow->addWidget(
            makeField(QStringLiteral("知识库名称"),
                      QStringLiteral("用于后台列表和平台绑定时辨识，建议使用业务入口或店铺名。"),
                      m_nameEdit,
                      formFrame),
            1);

        m_shopsEdit = new QLineEdit(formFrame);
        m_shopsEdit->setObjectName(QStringLiteral("aiBackendKnowledgeConfigField"));
        m_shopsEdit->setPlaceholderText(QStringLiteral("仅用于人工辨识，例如：官方旗舰店、微信私域通用"));
        m_shopsEdit->setText(base.value(QStringLiteral("applicable_shops")).toString());
        topRow->addWidget(
            makeField(QStringLiteral("适用店铺"),
                      QStringLiteral("仅作为人工说明，不参与检索路由过滤。"),
                      m_shopsEdit,
                      formFrame),
            1);
        formLay->addLayout(topRow);

        auto* dirRow = new QWidget(formFrame);
        auto* dirLay = new QHBoxLayout(dirRow);
        dirLay->setContentsMargins(0, 0, 0, 0);
        dirLay->setSpacing(10);
        m_directoryEdit = new QLineEdit(dirRow);
        m_directoryEdit->setObjectName(QStringLiteral("aiBackendKnowledgeConfigField"));
        m_directoryEdit->setPlaceholderText(QStringLiteral("选择本地知识库目录，例如 D:\\ai-customer-service\\docs\\knowledge-base"));
        m_directoryEdit->setText(base.value(QStringLiteral("source_directory")).toString());
        if (m_directoryEdit->text().trimmed().isEmpty()) {
            const QDir defaultKnowledgeDir(QDir::current().filePath(QStringLiteral("docs/knowledge-base")));
            if (defaultKnowledgeDir.exists())
                m_directoryEdit->setText(QDir::toNativeSeparators(defaultKnowledgeDir.absolutePath()));
        }
        dirLay->addWidget(m_directoryEdit, 1);
        auto* chooseBtn = new QPushButton(QStringLiteral("选择目录"), dirRow);
        chooseBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
        chooseBtn->setCursor(Qt::PointingHandCursor);
        chooseBtn->setFocusPolicy(Qt::NoFocus);
        dirLay->addWidget(chooseBtn, 0);
        formLay->addWidget(
            makeField(QStringLiteral("知识库目录"),
                      QStringLiteral("导入/重建时会读取该目录下的文档和图片素材。"),
                      dirRow,
                      formFrame));

        m_enabledCheck = new QCheckBox(QStringLiteral("启用该知识库"), formFrame);
        m_enabledCheck->setObjectName(QStringLiteral("aiBackendKnowledgeEnabledCheck"));
        m_enabledCheck->setChecked(m_baseId.isEmpty() ? true : isKnowledgeBaseEnabled(base));
        formLay->addWidget(m_enabledCheck);

        outer->addWidget(formFrame, 0);

        auto* actionRow = new QHBoxLayout;
        actionRow->setContentsMargins(0, 0, 0, 0);
        actionRow->setSpacing(10);
        m_statusLabel = new QLabel(m_baseId.isEmpty()
                                       ? QStringLiteral("请先保存知识库配置，再导入/重建目录。")
                                       : QStringLiteral("可编辑配置，或导入/重建当前知识库目录。"),
                                   this);
        m_statusLabel->setObjectName(QStringLiteral("aiBackendKnowledgeStatus"));
        m_statusLabel->setWordWrap(true);
        actionRow->addWidget(m_statusLabel, 1);

        auto* refreshBtn = new QPushButton(QStringLiteral("刷新素材"), this);
        refreshBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
        refreshBtn->setCursor(Qt::PointingHandCursor);
        refreshBtn->setFocusPolicy(Qt::NoFocus);
        actionRow->addWidget(refreshBtn, 0);

        m_importBtn = new QPushButton(QStringLiteral("导入/重建"), this);
        m_importBtn->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
        m_importBtn->setCursor(Qt::PointingHandCursor);
        m_importBtn->setFocusPolicy(Qt::NoFocus);
        actionRow->addWidget(m_importBtn, 0);

        auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
        saveBtn->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
        saveBtn->setCursor(Qt::PointingHandCursor);
        saveBtn->setFocusPolicy(Qt::NoFocus);
        actionRow->addWidget(saveBtn, 0);

        auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
        closeBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setFocusPolicy(Qt::NoFocus);
        actionRow->addWidget(closeBtn, 0);
        outer->addLayout(actionRow);

        auto* tabs = new QTabWidget(this);
        tabs->setObjectName(QStringLiteral("aiBackendKnowledgeTabs"));
        m_documentTable = new QTableWidget(tabs);
        m_documentTable->setColumnCount(5);
        m_documentTable->setHorizontalHeaderLabels({
            QStringLiteral("文档"),
            QStringLiteral("类型"),
            QStringLiteral("状态"),
            QStringLiteral("片段数"),
            QStringLiteral("更新时间"),
        });
        styleBackendDataTable(m_documentTable);
        m_documentTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_documentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_documentTable->verticalHeader()->setDefaultSectionSize(54);

        m_imageTable = new QTableWidget(tabs);
        m_imageTable->setColumnCount(8);
        m_imageTable->setHorizontalHeaderLabels({
            QStringLiteral("图片素材"),
            QStringLiteral("类型"),
            QStringLiteral("分析状态"),
            QStringLiteral("图片类型"),
            QStringLiteral("标签"),
            QStringLiteral("风险标签"),
            QStringLiteral("更新时间"),
            QStringLiteral("操作"),
        });
        styleBackendDataTable(m_imageTable);
        m_imageTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_imageTable->verticalHeader()->setDefaultSectionSize(54);

        tabs->addTab(m_documentTable, QStringLiteral("文档知识"));
        tabs->addTab(m_imageTable, QStringLiteral("图片素材"));
        outer->addWidget(tabs, 1);

        connect(chooseBtn, &QPushButton::clicked, this, [this]() {
            const QString startDir = QFileInfo(m_directoryEdit->text().trimmed()).isDir()
                ? m_directoryEdit->text().trimmed()
                : QDir::currentPath();
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择知识库目录"), startDir);
            if (!dir.trimmed().isEmpty())
                m_directoryEdit->setText(QDir::toNativeSeparators(dir));
        });
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            saveConfig(true);
        });
        connect(refreshBtn, &QPushButton::clicked, this, [this]() {
            refreshAssets();
        });
        connect(m_importBtn, &QPushButton::clicked, this, [this]() {
            startImport();
        });
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        if (!m_baseId.isEmpty())
            refreshAssets();
    }

private:
    bool saveConfig(bool showMessage)
    {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("知识库配置"), QStringLiteral("请填写知识库名称。"));
            return false;
        }

        QString error;
        if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
            QMessageBox::warning(this, QStringLiteral("知识库配置"),
                                 QStringLiteral("Python 服务不可用：%1").arg(error.left(160)));
            return false;
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("name"), name);
        payload.insert(QStringLiteral("applicable_shops"), m_shopsEdit->text().trimmed());
        payload.insert(QStringLiteral("source_directory"), m_directoryEdit->text().trimmed());
        payload.insert(QStringLiteral("enabled"), m_enabledCheck->isChecked());
        payload.insert(QStringLiteral("scene"), QStringLiteral("reply_draft"));
        payload.insert(QStringLiteral("shop_id"), QString());

        Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
        QJsonObject response;
        if (m_baseId.isEmpty()) {
            response = Ipc::IpcService::instance().createKnowledgeBase(payload, 5000, &status, &error);
        } else {
            payload.insert(QStringLiteral("id"), m_baseId);
            response = Ipc::IpcService::instance().updateKnowledgeBase(payload, 5000, &status, &error);
        }

        if (status != Ipc::ResponseStatus::Success
            || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
            const QString detail = response.value(QStringLiteral("detail")).toString(
                response.value(QStringLiteral("error")).toString(error));
            QMessageBox::warning(this, QStringLiteral("知识库配置"),
                                 QStringLiteral("保存失败：%1").arg(detail.left(180)));
            return false;
        }

        m_base = response.value(QStringLiteral("base")).toObject();
        m_baseId = m_base.value(QStringLiteral("id")).toString(m_baseId);
        setWindowTitle(QStringLiteral("知识库配置"));
        if (showMessage)
            m_statusLabel->setText(QStringLiteral("配置已保存。"));
        return true;
    }

    void refreshAssets()
    {
        if (m_baseId.trimmed().isEmpty()) {
            m_documentTable->setRowCount(0);
            m_imageTable->setRowCount(0);
            m_statusLabel->setText(QStringLiteral("保存知识库后可查看文档和图片素材。"));
            return;
        }
        refreshKnowledgeTables(m_documentTable, m_imageTable, m_statusLabel, m_baseId);
    }

    void startImport()
    {
        if (!saveConfig(false))
            return;
        const QString directory = m_directoryEdit->text().trimmed();
        if (directory.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("请先选择知识库素材目录。"));
            return;
        }
        if (!QFileInfo(directory).isDir()) {
            m_statusLabel->setText(QStringLiteral("目录不存在：%1").arg(directory));
            return;
        }

        QString error;
        if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
            m_statusLabel->setText(QStringLiteral("Python 服务不可用，无法导入：%1").arg(error.left(120)));
            return;
        }

        m_importBtn->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("正在导入中，可能需要一点时间..."));
        Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
        const QJsonObject response = Ipc::IpcService::instance().importKnowledgeDirectoryForBase(
            directory,
            m_baseId,
            m_nameEdit->text().trimmed(),
            m_shopsEdit->text().trimmed(),
            QStringLiteral("reply_draft"),
            120000,
            true,
            &status,
            &error);

        if (status != Ipc::ResponseStatus::Success
            || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
            m_importBtn->setEnabled(true);
            const QString detail = response.value(QStringLiteral("detail")).toString(
                response.value(QStringLiteral("error")).toString(error));
            m_statusLabel->setText(QStringLiteral("导入失败：%1").arg(detail.left(160)));
            return;
        }

        const QString taskId = response.value(QStringLiteral("task_id")).toString();
        if (taskId.trimmed().isEmpty()) {
            m_importBtn->setEnabled(true);
            finishKnowledgeImportUi(response, m_documentTable, m_imageTable, m_statusLabel, m_baseId);
            return;
        }

        auto* pollTimer = new QTimer(this);
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        pollTimer->setInterval(2000);
        connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, taskId, timer]() {
            QString pollError;
            Ipc::ResponseStatus pollStatus = Ipc::ResponseStatus::Error;
            const QJsonObject task = Ipc::IpcService::instance().fetchKnowledgeImportTask(
                taskId, 120000, &pollStatus, &pollError);
            const QString taskState = task.value(QStringLiteral("task_status")).toString(
                task.value(QStringLiteral("status")).toString());

            if (pollStatus != Ipc::ResponseStatus::Success
                || task.value(QStringLiteral("request_status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
                pollTimer->stop();
                pollTimer->deleteLater();
                m_importBtn->setEnabled(true);
                const QString detail = task.value(QStringLiteral("detail")).toString(
                    task.value(QStringLiteral("error")).toString(pollError));
                m_statusLabel->setText(QStringLiteral("导入失败：%1").arg(detail.left(160)));
                return;
            }

            if (taskState == QLatin1String("queued") || taskState == QLatin1String("running")) {
                m_statusLabel->setText(QStringLiteral("正在导入中，可能需要一点时间...已等待 %1 秒")
                                           .arg(timer->elapsed() / 1000));
                return;
            }

            pollTimer->stop();
            pollTimer->deleteLater();
            m_importBtn->setEnabled(true);
            if (taskState == QLatin1String("success")) {
                finishKnowledgeImportUi(task, m_documentTable, m_imageTable, m_statusLabel, m_baseId);
                return;
            }

            const QString detail = task.value(QStringLiteral("detail")).toString(
                task.value(QStringLiteral("error")).toString(QStringLiteral("unknown_error")));
            m_statusLabel->setText(QStringLiteral("导入失败：%1").arg(detail.left(160)));
        });
        pollTimer->start();
    }

    QJsonObject m_base;
    QString m_baseId;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_shopsEdit = nullptr;
    QLineEdit* m_directoryEdit = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_importBtn = nullptr;
    QTableWidget* m_documentTable = nullptr;
    QTableWidget* m_imageTable = nullptr;
};

static void populateKnowledgeBaseTable(QTableWidget* table,
                                       QLabel* statusLabel,
                                       const QJsonArray& bases,
                                       const std::weak_ptr<std::function<void()>>& refreshFn,
                                       QWidget* owner)
{
    table->setRowCount(bases.size());
    for (int row = 0; row < bases.size(); ++row) {
        const QJsonObject base = bases.at(row).toObject();
        const QString baseId = base.value(QStringLiteral("id")).toString();
        const bool enabled = isKnowledgeBaseEnabled(base);
        const QString name = displayKnowledgeText(base.value(QStringLiteral("name")).toString());
        const QString shops = displayKnowledgeText(base.value(QStringLiteral("applicable_shops")).toString(),
                                                   QStringLiteral("未填写"));
        const QString directory = displayKnowledgeText(base.value(QStringLiteral("source_directory")).toString(),
                                                       QStringLiteral("未配置"));
        const int docCount = base.value(QStringLiteral("document_count")).toInt();
        const int imageCount = base.value(QStringLiteral("image_count")).toInt();
        const QString importStatus = formatKnowledgeImportStatus(base);
        const QString updatedAt = displayKnowledgeText(base.value(QStringLiteral("updated_at")).toString());

        table->setItem(row, 0, makeKnowledgeCellItem(name));
        table->setItem(row, 1, makeKnowledgeCellItem(shops));
        table->setItem(row, 2, makeKnowledgeCellItem(directory));
        table->setItem(row, 3, makeKnowledgeCellItem(QString::number(docCount)));
        table->setItem(row, 4, makeKnowledgeCellItem(QString::number(imageCount)));
        table->setItem(row, 5, makeKnowledgeCellItem(importStatus));
        table->setItem(row, 6, makeKnowledgeCellItem(updatedAt));
        table->setItem(row, 7, makeKnowledgeCellItem(enabled ? QStringLiteral("启用") : QStringLiteral("停用")));
        table->item(row, 0)->setToolTip(name);
        table->item(row, 2)->setToolTip(directory);
        const QString importError = base.value(QStringLiteral("last_import_error")).toString().trimmed();
        if (!importError.isEmpty())
            table->item(row, 5)->setToolTip(QStringLiteral("%1\n%2").arg(importStatus, importError));

        auto* actionCell = new QWidget(table);
        auto* actionLay = new QHBoxLayout(actionCell);
        actionLay->setContentsMargins(4, 4, 4, 4);
        actionLay->setSpacing(8);
        auto* editBtn = new QPushButton(QStringLiteral("编辑"), actionCell);
        editBtn->setObjectName(QStringLiteral("aiBackendTableActionBtn"));
        editBtn->setCursor(Qt::PointingHandCursor);
        editBtn->setFocusPolicy(Qt::NoFocus);
        editBtn->setMinimumWidth(70);
        auto* toggleBtn = new QPushButton(enabled ? QStringLiteral("停用") : QStringLiteral("启用"), actionCell);
        toggleBtn->setObjectName(enabled ? QStringLiteral("aiBackendTableActionBtn")
                                         : QStringLiteral("aiBackendTableActionPrimaryBtn"));
        toggleBtn->setCursor(Qt::PointingHandCursor);
        toggleBtn->setFocusPolicy(Qt::NoFocus);
        toggleBtn->setMinimumWidth(70);
        auto* deleteBtn = new QPushButton(QStringLiteral("删除"), actionCell);
        deleteBtn->setObjectName(QStringLiteral("aiBackendTableActionDangerBtn"));
        deleteBtn->setCursor(Qt::PointingHandCursor);
        deleteBtn->setFocusPolicy(Qt::NoFocus);
        deleteBtn->setMinimumWidth(70);
        actionLay->addWidget(editBtn, 0);
        actionLay->addWidget(toggleBtn, 0);
        actionLay->addWidget(deleteBtn, 0);
        actionLay->addStretch(1);
        table->setCellWidget(row, 8, actionCell);

        QObject::connect(editBtn, &QPushButton::clicked, owner, [owner, base, refreshFn]() {
            KnowledgeBaseConfigDialog dialog(owner, base);
            dialog.exec();
            if (const auto locked = refreshFn.lock(); locked && *locked)
                (*locked)();
        });
        QObject::connect(toggleBtn, &QPushButton::clicked, owner, [owner, baseId, enabled, statusLabel, refreshFn]() {
            QString error;
            if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
                statusLabel->setText(QStringLiteral("Python 服务不可用：%1").arg(error.left(120)));
                return;
            }
            Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
            const QJsonObject response = Ipc::IpcService::instance().setKnowledgeBaseEnabled(
                baseId, !enabled, 5000, &status, &error);
            if (status != Ipc::ResponseStatus::Success
                || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
                const QString detail = response.value(QStringLiteral("detail")).toString(
                    response.value(QStringLiteral("error")).toString(error));
                statusLabel->setText(QStringLiteral("切换状态失败：%1").arg(detail.left(160)));
                return;
            }
            if (const auto locked = refreshFn.lock(); locked && *locked)
                (*locked)();
        });
        QObject::connect(deleteBtn, &QPushButton::clicked, owner, [owner, baseId, name, statusLabel, refreshFn]() {
            if (QMessageBox::question(owner,
                                      QStringLiteral("删除知识库"),
                                      QStringLiteral("确定删除“%1”吗？\n\n删除后会清理该知识库的文档索引、图片素材索引和平台绑定记录，但不会删除本地原始目录文件。").arg(name),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes) {
                return;
            }

            QString error;
            if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
                statusLabel->setText(QStringLiteral("Python 服务不可用：%1").arg(error.left(120)));
                return;
            }
            Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
            const QJsonObject response = Ipc::IpcService::instance().deleteKnowledgeBase(
                baseId, 5000, &status, &error);
            if (status != Ipc::ResponseStatus::Success
                || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
                const QString detail = response.value(QStringLiteral("detail")).toString(
                    response.value(QStringLiteral("error")).toString(error));
                statusLabel->setText(QStringLiteral("删除知识库失败：%1").arg(detail.left(160)));
                return;
            }
            statusLabel->setText(QStringLiteral("知识库已删除。"));
            if (const auto locked = refreshFn.lock(); locked && *locked)
                (*locked)();
        });
    }
    auto* header = table->horizontalHeader();
    header->setMinimumSectionSize(64);
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    header->setSectionResizeMode(3, QHeaderView::Interactive);
    header->setSectionResizeMode(4, QHeaderView::Interactive);
    header->setSectionResizeMode(5, QHeaderView::Interactive);
    header->setSectionResizeMode(6, QHeaderView::Interactive);
    header->setSectionResizeMode(7, QHeaderView::Interactive);
    header->setSectionResizeMode(8, QHeaderView::Interactive);
    table->setColumnWidth(0, 150);
    table->setColumnWidth(1, 150);
    table->setColumnWidth(2, 190);
    table->setColumnWidth(3, 76);
    table->setColumnWidth(4, 76);
    table->setColumnWidth(5, 210);
    table->setColumnWidth(6, 150);
    table->setColumnWidth(7, 76);
    table->setColumnWidth(8, 250);
}

static QWidget* buildProductKnowledgePage(std::function<void()>* refreshOut = nullptr)
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("aiBackendSubPage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 28);
    outer->setSpacing(16);

    auto* titleRow = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("知识库"), page);
    title->setObjectName(QStringLiteral("aiBackendDashTitle"));
    auto* sub = new QLabel(
        QStringLiteral("管理产品资料、常见问答、售后政策和图片素材，用于 AI 回复检索和附图推荐。"), page);
    sub->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    sub->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    titleRow->addLayout(titleCol, 1);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新列表"), page);
    refreshBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(refreshBtn, 0, Qt::AlignTop);
    auto* newBtn = new QPushButton(QStringLiteral("+ 新建知识库"), page);
    newBtn->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(newBtn, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("aiBackendDataCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    auto* statusLabel = new QLabel(QStringLiteral("切换到本页后会自动加载知识库列表，也可以点击“刷新列表”。"), card);
    statusLabel->setObjectName(QStringLiteral("aiBackendKnowledgeStatus"));
    statusLabel->setWordWrap(true);
    cardLay->addWidget(statusLabel, 0);

    auto* table = new QTableWidget(card);
    table->setColumnCount(9);
    table->setHorizontalHeaderLabels({
        QStringLiteral("知识库名称"),
        QStringLiteral("适用店铺"),
        QStringLiteral("知识库目录"),
        QStringLiteral("文档数"),
        QStringLiteral("图片数"),
        QStringLiteral("最近导入"),
        QStringLiteral("最后更新"),
        QStringLiteral("状态"),
        QStringLiteral("操作"),
    });
    styleBackendDataTable(table);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setDefaultSectionSize(54);
    cardLay->addWidget(table, 1);
    outer->addWidget(card, 1);

    auto* banner = new QFrame(page);
    banner->setObjectName(QStringLiteral("aiBackendInfoBanner"));
    auto* bh = new QHBoxLayout(banner);
    bh->setContentsMargins(16, 14, 16, 14);
    bh->setSpacing(12);
    auto* icon = new QLabel(QStringLiteral("i"), banner);
    icon->setObjectName(QStringLiteral("aiBackendInfoBannerIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(28, 28);
    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(6);
    auto* bt = new QLabel(QStringLiteral("关于知识库使用"), banner);
    bt->setObjectName(QStringLiteral("aiBackendInfoBannerTitle"));
    auto* bd = new QLabel(
        QStringLiteral(
            "常见问答无需单独页面，可新建“常见问答知识库”并导入问答列表；当前支持知识库配置、导入/重建、素材预览和平台绑定。"),
        banner);
    bd->setObjectName(QStringLiteral("aiBackendInfoBannerBody"));
    bd->setWordWrap(true);
    textCol->addWidget(bt);
    textCol->addWidget(bd);
    bh->addWidget(icon, 0, Qt::AlignTop);
    bh->addLayout(textCol, 1);
    outer->addWidget(banner, 0);

    outer->addStretch(0);

    auto refreshBases = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> refreshWeak(refreshBases);
    *refreshBases = [page, table, statusLabel, refreshWeak]() {
        if (!Ipc::IpcService::instance().isServiceAvailable()) {
            table->setRowCount(0);
            statusLabel->setText(QStringLiteral("需先启动 Python 服务，然后点击“刷新列表”加载知识库。"));
            return;
        }
        QString error;
        Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
        const QJsonObject response = Ipc::IpcService::instance().fetchKnowledgeBases(5000, &status, &error);
        if (status != Ipc::ResponseStatus::Success
            || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
            table->setRowCount(0);
            const QString detail = response.value(QStringLiteral("detail")).toString(
                response.value(QStringLiteral("error")).toString(error));
            statusLabel->setText(QStringLiteral("读取知识库列表失败：%1").arg(detail.left(160)));
            return;
        }
        const QJsonArray bases = response.value(QStringLiteral("bases")).toArray();
        populateKnowledgeBaseTable(table, statusLabel, bases, refreshWeak, page);
        if (bases.isEmpty())
            statusLabel->setText(QStringLiteral("暂无知识库。点击“新建知识库”添加本地素材目录。"));
        else
            statusLabel->setText(QStringLiteral("已加载 %1 个知识库。未做平台绑定时，后续检索默认使用所有启用知识库。").arg(bases.size()));
    };

    if (refreshOut) {
        *refreshOut = [refreshWeak]() {
            if (const auto locked = refreshWeak.lock(); locked && *locked)
                (*locked)();
        };
    }

    QObject::connect(refreshBtn, &QPushButton::clicked, page, [refreshBases]() {
        if (refreshBases && *refreshBases)
            (*refreshBases)();
    });

    QObject::connect(newBtn, &QPushButton::clicked, page, [page, refreshBases]() {
        KnowledgeBaseConfigDialog dialog(page);
        dialog.exec();
        if (refreshBases && *refreshBases)
            (*refreshBases)();
    });

    return page;
}

} // namespace

AiCustomerServiceBackendWindow::AiCustomerServiceBackendWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(QStringLiteral("AI客服后台"));
    setMinimumSize(1000, 700);
    resize(1180, 820);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("aiBackendCentral"));
    setCentralWidget(central);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("aiBackendContentStack"));
    m_stack->addWidget(buildDashboardPage());
    m_stack->addWidget(buildRobotStoreConfigPage(&m_refreshRobotConfigs));
    m_stack->addWidget(buildProductKnowledgePage(&m_refreshProductKnowledgeBases));
    m_apiConfigPage = new AiProviderConfigPage(central);
    m_stack->addWidget(m_apiConfigPage);
    m_stack->addWidget(makePlaceholderPage(QStringLiteral("通用设置")));

    QWidget* nav = buildNavSidebar();
    root->addWidget(nav, 0);
    root->addWidget(m_stack, 1);

    connect(m_apiConfigPage, &AiProviderConfigPage::settingsSaved, this, [this]() {
        emit aiProviderConfigChanged();
        if (m_refreshRobotConfigs)
            m_refreshRobotConfigs();
    });

    connect(m_nav, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!m_nav || !m_stack || !cur)
            return;
        if (cur->childCount() > 0)
            return;
        const QVariant v = cur->data(0, Qt::UserRole);
        if (!v.isValid())
            return;
        const int idx = v.toInt();
        if (idx < 0 || idx >= m_stack->count())
            return;
        if (idx == kStackApiModel && m_apiConfigPage)
            m_apiConfigPage->reloadCurrentPreset();
        m_stack->setCurrentIndex(idx);
        if (idx == kStackRobotStoreConfig && m_refreshRobotConfigs)
            m_refreshRobotConfigs();
        if (idx == kStackProductKnowledge && !m_productKnowledgeLoaded && m_refreshProductKnowledgeBases) {
            m_productKnowledgeLoaded = true;
            QTimer::singleShot(0, this, [this]() {
                if (m_refreshProductKnowledgeBases)
                    m_refreshProductKnowledgeBases();
            });
        }
    });

    applyLocalStyle();
}

QWidget* AiCustomerServiceBackendWindow::buildNavSidebar()
{
    auto* wrap = new QWidget;
    wrap->setObjectName(QStringLiteral("aiBackendNav"));
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(8, 16, 8, 16);
    v->setSpacing(4);

    m_nav = new QTreeWidget(wrap);
    m_nav->setObjectName(QStringLiteral("aiBackendNavTree"));
    m_nav->setHeaderHidden(true);
    m_nav->setRootIsDecorated(false);
    m_nav->setIndentation(0);
    m_nav->setExpandsOnDoubleClick(false);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setAnimated(true);
    m_nav->setIconSize(QSize(20, 20));
    m_nav->setUniformRowHeights(false);
    m_nav->setItemDelegate(new SidebarTocDelegate(
        m_nav, ApplyStyle::loadSavedMainWindowTheme(), m_nav));
    m_nav->setAttribute(Qt::WA_StyledBackground, true);
    if (QWidget* vp = m_nav->viewport())
        vp->setAttribute(Qt::WA_StyledBackground, true);
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        m_nav->setStyle(fusion);

    auto addTopLeaf = [this](const QString& text, int stackIdx) {
        auto* it = new QTreeWidgetItem(m_nav, QStringList{text});
        it->setData(0, Qt::UserRole, stackIdx);
        return it;
    };

    QTreeWidgetItem* const dash = addTopLeaf(QStringLiteral("数据概览"), kStackDashboard);

    auto* agentGroup = new QTreeWidgetItem(m_nav, QStringList{QStringLiteral("Agent 设置")});
    auto* robotCfg = new QTreeWidgetItem(agentGroup, QStringList{QStringLiteral("店铺机器人配置")});
    robotCfg->setData(0, Qt::UserRole, kStackRobotStoreConfig);

    addTopLeaf(QStringLiteral("知识库"), kStackProductKnowledge);

    addTopLeaf(QStringLiteral("API 配置/模型"), kStackApiModel);
    addTopLeaf(QStringLiteral("通用设置"), kStackGeneralSettings);

    agentGroup->setExpanded(true);

    m_nav->setCurrentItem(dash);
    m_nav->setFixedWidth(220);
    m_nav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    v->addWidget(m_nav, 1);
    return wrap;
}

QWidget* AiCustomerServiceBackendWindow::buildDashboardPage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("aiBackendDashboardPage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 32);
    outer->setSpacing(20);

    auto* titleL = new QLabel(QStringLiteral("数据概览"), page);
    titleL->setObjectName(QStringLiteral("aiBackendDashTitle"));
    auto* subL = new QLabel(QStringLiteral("实时监控AI客服运行状态与核心业务指标"), page);
    subL->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    outer->addWidget(titleL);
    outer->addWidget(subL);

    auto* dateWrap = new QFrame(page);
    dateWrap->setObjectName(QStringLiteral("aiBackendDateRange"));
    auto* dhl = new QHBoxLayout(dateWrap);
    dhl->setContentsMargins(14, 10, 14, 10);
    auto* dateText = new QLabel(
        QStringLiteral("统计区间：2026-04-01 ～ 2026-04-18（展示占位，后续可接日期筛选）"), dateWrap);
    dateText->setObjectName(QStringLiteral("aiBackendDateRangeLabel"));
    dateText->setWordWrap(true);
    dhl->addWidget(dateText, 1);
    outer->addWidget(dateWrap, 0);

    struct Row {
        const char* t;
        const char* v;
        const char* c;
        bool up;
    };
    const Row r1[] = { { "今日消息数", "12,840", "+5.2%", true },
                      { "有效恢复率", "92.4%", "+2.1%", true },
                      { "独立接待率", "88.5%", "+1.5%", true },
                      { "平均响应时间", "1.2s", "-0.3s", true } };
    const Row r2[] = { { "订单转化率", "15.2%", "+0.8%", true },
                        { "满意度", "4.85", "+0.12", true },
                        { "转人工率", "11.5%", "-2.0%", false },
                        { "撤回率", "0.5%", "-0.1%", false } };

    auto* g = new QGridLayout;
    g->setContentsMargins(2, 0, 2, 2);
    g->setHorizontalSpacing(16);
    g->setVerticalSpacing(16);
    for (int i = 0; i < 4; ++i) {
        g->addWidget(
            makeMetricCard(QString::fromUtf8(r1[i].t), QString::fromUtf8(r1[i].v), QString::fromUtf8(r1[i].c),
                          r1[i].up),
            0, i);
    }
    for (int i = 0; i < 4; ++i) {
        g->addWidget(
            makeMetricCard(QString::fromUtf8(r2[i].t), QString::fromUtf8(r2[i].v), QString::fromUtf8(r2[i].c),
                          r2[i].up),
            1, i);
    }
    outer->addLayout(g);

    auto* chartsRow = new QHBoxLayout;
    chartsRow->setSpacing(16);
    auto* trafficCard = new AiBackendDashTrafficChartCard(page);
    auto* categoryCard = new AiBackendDashCategoryCard(page);
    chartsRow->addWidget(trafficCard, 2);
    chartsRow->addWidget(categoryCard, 1);
    outer->addLayout(chartsRow);

    outer->addStretch(1);
    return page;
}

void AiCustomerServiceBackendWindow::applyLocalStyle()
{
    setObjectName(QStringLiteral("aiBackendWindowRoot"));
    const ApplyStyle::MainWindowTheme navTheme = ApplyStyle::loadSavedMainWindowTheme();
    QString navStrip;
    switch (navTheme) {
    case ApplyStyle::MainWindowTheme::Cool:
        navStrip = QStringLiteral(
            "QWidget#aiBackendNav { background: #0b1220; border-right: 1px solid #334155; }");
        break;
    case ApplyStyle::MainWindowTheme::Warm:
        navStrip = QStringLiteral(
            "QWidget#aiBackendNav { background: #352f2c; border-right: 1px solid #5c4f42; }");
        break;
    case ApplyStyle::MainWindowTheme::Default:
    default:
        navStrip = QStringLiteral(
            "QWidget#aiBackendNav { background: #E4E4E7; border-right: 1px solid #D4D4D8; }");
        break;
    }
    const QString treeQss =
        ApplyStyle::sidebarTocTreeStyleSheet(QStringLiteral("aiBackendNavTree"), navTheme);

    setStyleSheet(QStringLiteral(
                      R"QSS(
QMainWindow#aiBackendWindowRoot { background: %1; }
QWidget#aiBackendCentral { background: %1; }
QStackedWidget#aiBackendContentStack { background: %1; }
QStackedWidget#aiBackendContentStack > QWidget { background: %1; }
)QSS")
            .arg(QLatin1String(kAiBackendContentBg))
        + navStrip + treeQss
        + QStringLiteral(
            R"QSS(
QWidget#aiBackendDashboardPage { background: %1; }
QLabel#aiBackendDashTitle { font-size: 22px; font-weight: 700; color: #0f172a; }
QLabel#aiBackendDashSubtitle { font-size: 14px; color: #64748b; }
QFrame#aiBackendDateRange {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-left: 3px solid #2563eb;
  border-radius: 10px;
  min-height: 44px;
}
QLabel#aiBackendDateRangeLabel {
  color: #0f172a;
  font-size: 14px;
  font-weight: 500;
  background: transparent;
}
QFrame#aiBackendMetricCard {
  background: #ffffff;
  border: 1px solid #E5E7EB;
  border-radius: 12px;
  min-height: 120px;
}
QLabel#aiBackendMetricTag { color: #94a3b8; font-size: 12px; }
QLabel#aiBackendMetricTitle { color: #64748b; font-size: 13px; }
QLabel#aiBackendMetricValue { color: #0f172a; font-size: 24px; font-weight: 700; }
QLabel#aiBackendMetricHint { color: #94a3b8; font-size: 12px; }
QLabel#aiBackendMetricUp { color: #16a34a; font-size: 12px; font-weight: 600; }
QLabel#aiBackendMetricDown { color: #dc2626; font-size: 12px; font-weight: 600; }
QLabel#aiBackendPlaceholderText { color: #64748b; font-size: 16px; }
QWidget#aiBackendSubPage { background: transparent; }
QFrame#aiBackendDataCard {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
}
QScrollArea#aiBackendRobotListScroll {
  background: transparent;
  border: none;
}
QWidget#aiBackendRobotListScrollBody {
  background: transparent;
}
QScrollArea#aiBackendDataListScroll {
  background: transparent;
  border: none;
}
QWidget#aiBackendDataListScrollBody {
  background: transparent;
}
QLabel#aiBackendRobotListColHeader {
  color: #94a3b8;
  font-size: 13px;
  font-weight: 600;
  background: transparent;
}
QFrame#aiBackendRobotListRule {
  background: #f1f5f9;
  border: none;
  min-height: 1px;
  max-height: 1px;
}
QWidget#aiBackendRobotListRow {
  background: transparent;
}
QWidget#aiBackendRobotListRowAlt {
  background: #fafafa;
}
QTableWidget#aiBackendDataTable {
  background: #ffffff;
  border: none;
  gridline-color: transparent;
  alternate-background-color: #f8fafc;
}
QTableWidget#aiBackendDataTable::item {
  padding: 6px 8px;
  border: none;
  color: #334155;
  font-size: 14px;
}
QTableWidget#aiBackendDataTable::item:selected { background: transparent; color: #334155; }
QTableWidget#aiBackendDataTable QHeaderView::section {
  background: #f8fafc;
  color: #64748b;
  font-size: 13px;
  font-weight: 600;
  padding: 10px 8px;
  border: none;
  border-bottom: 1px solid #e2e8f0;
}
QLabel#aiBackendTableCellTitle { color: #0f172a; font-size: 14px; font-weight: 600; background: transparent; }
QLabel#aiBackendTableCellMuted { color: #94a3b8; font-size: 12px; background: transparent; }
QLabel#aiBackendTableCellBody { color: #334155; font-size: 14px; background: transparent; }
QLabel#aiBackendTagKb {
  background: #dbeafe;
  color: #1d4ed8;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 10px;
  border-radius: 999px;
  min-height: 18px;
}
QLabel#aiBackendTagPolicy {
  background: #ede9fe;
  color: #6d28d9;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 10px;
  border-radius: 999px;
  min-height: 18px;
}
QLabel#aiBackendTagModel {
  background: #f1f5f9;
  color: #334155;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 999px;
  min-height: 18px;
}
QLabel#aiBackendTagDefault {
  background: #f1f5f9;
  color: #64748b;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 6px;
  min-height: 16px;
}
QLabel#aiBackendStatusOnline {
  background: #dcfce7;
  color: #15803d;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 999px;
}
QLabel#aiBackendStatusOffline {
  background: #f1f5f9;
  color: #64748b;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 999px;
}
QLabel#aiBackendSyncDone {
  background: #dcfce7;
  color: #15803d;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 999px;
}
QLabel#aiBackendSyncProgress {
  background: #dbeafe;
  color: #1d4ed8;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 999px;
}
QPushButton#aiBackendPurplePrimaryBtn {
  background: #7c3aed;
  color: #ffffff;
  border: none;
  border-radius: 10px;
  padding: 10px 20px;
  font-size: 14px;
  font-weight: 600;
}
QPushButton#aiBackendPurplePrimaryBtn:hover { background: #6d28d9; }
QPushButton#aiBackendBluePrimaryBtn {
  background: #2563eb;
  color: #ffffff;
  border: none;
  border-radius: 10px;
  padding: 10px 20px;
  font-size: 14px;
  font-weight: 600;
}
QPushButton#aiBackendBluePrimaryBtn:hover { background: #1d4ed8; }
QPushButton#aiBackendSecondaryBtn {
  background: #ffffff;
  color: #334155;
  border: 1px solid #cbd5e1;
  border-radius: 10px;
  padding: 10px 18px;
  font-size: 14px;
  font-weight: 600;
}
QPushButton#aiBackendSecondaryBtn:hover { background: #f8fafc; border-color: #94a3b8; }
QPushButton#aiBackendTableActionBtn {
  background: #ffffff;
  color: #0f172a;
  border: 1px solid #cbd5e1;
  border-radius: 9px;
  padding: 7px 10px;
  font-size: 13px;
  font-weight: 600;
  min-height: 20px;
}
QPushButton#aiBackendTableActionBtn:hover { background: #f8fafc; border-color: #94a3b8; }
QPushButton#aiBackendTableActionBtn:disabled { color: #94a3b8; background: #f8fafc; }
QPushButton#aiBackendTableActionPrimaryBtn {
  background: #2563eb;
  color: #ffffff;
  border: none;
  border-radius: 9px;
  padding: 7px 10px;
  font-size: 13px;
  font-weight: 600;
  min-height: 20px;
}
QPushButton#aiBackendTableActionPrimaryBtn:hover { background: #1d4ed8; }
QPushButton#aiBackendTableActionDangerBtn {
  background: #ffffff;
  color: #dc2626;
  border: 1px solid #fecaca;
  border-radius: 9px;
  padding: 7px 10px;
  font-size: 13px;
  font-weight: 600;
  min-height: 20px;
}
QPushButton#aiBackendTableActionDangerBtn:hover { background: #fef2f2; border-color: #fca5a5; }
QLineEdit#aiBackendKnowledgeDirectoryEdit {
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  color: #0f172a;
  padding: 9px 12px;
  min-height: 22px;
  font-size: 14px;
  selection-background-color: #bfdbfe;
  selection-color: #0f172a;
}
QLineEdit#aiBackendKnowledgeDirectoryEdit:focus { border: 1px solid #2563eb; background: #ffffff; }
QLabel#aiBackendKnowledgeStatus { color: #64748b; font-size: 13px; background: transparent; }
QLabel#aiBackendActionLink { color: #2563eb; font-size: 13px; font-weight: 500; background: transparent; }
QLabel#aiBackendActionLink:hover { color: #1d4ed8; text-decoration: underline; }
QLabel#aiBackendActionDanger { color: #dc2626; font-size: 13px; font-weight: 500; background: transparent; }
QLabel#aiBackendActionOk { color: #16a34a; font-size: 13px; font-weight: 500; background: transparent; }
QLabel#aiBackendActionMuted { color: #94a3b8; font-size: 13px; font-weight: 500; background: transparent; }
QFrame#aiBackendInfoBanner {
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  border-radius: 12px;
}
QLabel#aiBackendInfoBannerIcon {
  background: #2563eb;
  color: #ffffff;
  font-size: 13px;
  font-weight: 700;
  border-radius: 14px;
}
QLabel#aiBackendInfoBannerTitle { color: #1e40af; font-size: 15px; font-weight: 700; background: transparent; }
QLabel#aiBackendInfoBannerBody { color: #334155; font-size: 13px; background: transparent; }
QWidget#aiProviderConfigPage { background: %1; }
QWidget#aiProviderConfigPage QScrollArea#aiProviderConfigScroll { background: %1; border: none; }
QWidget#aiProviderConfigPage QScrollArea#aiProviderConfigScroll QWidget#aiProviderConfigScrollViewport {
  background: %1;
  border: none;
}
QWidget#aiProviderConfigForm {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
}
QWidget#aiProviderConfigPage QLabel#aiProviderConfigTitle { font-size: 22px; font-weight: 700; color: #0f172a; }
QWidget#aiProviderConfigPage QLabel#aiProviderConfigSubtitle { color: #64748b; font-size: 14px; }
QWidget#aiProviderConfigPage QLabel#robotSettingsFieldLabel { color: #475569; font-size: 13px; font-weight: 500; }
QWidget#aiProviderConfigPage QLineEdit#robotSettingsField {
  background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; color: #0f172a;
  padding: 8px 10px; min-height: 20px; font-size: 14px; selection-background-color: #bfdbfe; selection-color: #0f172a;
}
QWidget#aiProviderConfigPage QLineEdit#robotSettingsField:focus { border: 1px solid #2563eb; background: #ffffff; }
QWidget#aiProviderConfigPage QComboBox#robotAssistantModelCombo {
  background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; color: #0f172a;
  padding: 6px 10px; min-height: 28px; font-size: 14px;
}
QWidget#aiProviderConfigPage QComboBox#robotAssistantModelCombo:hover { background: #fafafa; }
QWidget#aiProviderConfigPage QComboBox#robotAssistantModelCombo:focus { border: 1px solid #2563eb; }
QWidget#aiProviderConfigPage QComboBox#robotAssistantModelCombo QAbstractItemView {
  background: #ffffff; color: #0f172a; selection-background-color: #dbeafe; selection-color: #0f172a; border: 1px solid #e2e8f0;
}
QWidget#aiProviderConfigPage QPushButton#aiBackendPrimaryBtn {
  background: #2563eb; color: #ffffff; border: none; border-radius: 8px;
  padding: 8px 20px; font-size: 14px; font-weight: 600;
}
QWidget#aiProviderConfigPage QPushButton#aiBackendPrimaryBtn:hover { background: #1d4ed8; }
QWidget#aiProviderConfigPage QPushButton#aiBackendSecondaryBtn {
  background: #ffffff; color: #334155; border: 1px solid #e2e8f0; border-radius: 8px;
  padding: 8px 20px; font-size: 14px;
}
QWidget#aiProviderConfigPage QPushButton#aiBackendSecondaryBtn:hover { background: #f8fafc; border-color: #cbd5e1; }
QWidget#aiProviderConfigPage QLabel#robotAssistantPrivacy { color: #64748b; font-size: 13px; background: transparent; }
QWidget#aiProviderConfigPage QLabel#aiProviderConfigStatus { color: #334155; font-size: 13px; background: transparent; }
)QSS")
            .arg(QLatin1String(kAiBackendContentBg))
        + ApplyStyle::globalScrollBarStyle());
}

void AiCustomerServiceBackendWindow::focusApiModelPage()
{
    if (m_apiConfigPage)
        m_apiConfigPage->reloadCurrentPreset();
    if (m_nav && m_stack) {
        if (QTreeWidgetItem* it = findNavItemByStackIndex(m_nav, kStackApiModel))
            m_nav->setCurrentItem(it);
        m_stack->setCurrentIndex(kStackApiModel);
    }
}

