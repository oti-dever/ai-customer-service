#include "aicustomerservicebackendwindow.h"
#include "aiproviderconfigpage.h"
#include "sidebartocdelegate.h"

#include "../utils/applystyle.h"

#include <QColor>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QScrollArea>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QStyleFactory>
#include <QVariant>

#include <cmath>
#include <initializer_list>
#include <tuple>
#include <utility>

#include <QVector>

namespace {

/** 与 m_stack 添加顺序一致 */
constexpr int kStackDashboard = 0;
constexpr int kStackRobotStoreConfig = 1;
constexpr int kStackProductKnowledge = 2;
constexpr int kStackFaqKnowledge = 3;
constexpr int kStackApiModel = 4;
constexpr int kStackGeneralSettings = 5;

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
    table->setWordWrap(true);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(72);
    table->horizontalHeader()->setHighlightSections(false);
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

/** 产品知识库列表：列标题（与机器人列表同视觉体系） */
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

/** F&A 列表「名称 + 可选默认基础标签」 */
static QWidget* buildFaqNameCell(const QString& name, bool withDefaultTag, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 2, 8, 8);
    h->setSpacing(8);
    auto* nl = new QLabel(name, w);
    nl->setObjectName(QStringLiteral("aiBackendTableCellTitle"));
    nl->setWordWrap(true);
    h->addWidget(nl, 0, Qt::AlignTop | Qt::AlignLeft);
    if (withDefaultTag) {
        auto* tag = makePillLabel(QStringLiteral("默认基础"), QStringLiteral("aiBackendTagDefault"), w);
        h->addWidget(tag, 0, Qt::AlignTop);
    }
    h->addStretch(1);
    return w;
}

static QWidget* buildFaqKbListHeader(QWidget* parent)
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
    addHdr(QStringLiteral("问答库名称"), 3);
    addHdr(QStringLiteral("绑定店铺"), 3);
    addHdr(QStringLiteral("问答总数"), 1);
    addHdr(QStringLiteral("最后更新"), 1);
    addHdr(QStringLiteral("操作"), 2);
    return w;
}

static QWidget* buildFaqKbDataRow(
    QWidget* parent,
    const QString& name,
    bool withDefaultTag,
    const QString& stores,
    const QString& count,
    const QString& date,
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
    addCell(buildFaqNameCell(name, withDefaultTag, row), 3);
    addCell(wrapListPlainText(stores, false, row), 3);
    addCell(wrapListPlainText(count, false, row), 1);
    addCell(wrapListPlainText(date, false, row), 1);
    addCell(wrapActionLinks(row, actions), 2);
    return row;
}

static QWidget* buildRobotStoreConfigPage()
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
        QStringLiteral("在这里管理您的所有机器人，为其配置模型能力、知识库及策略。"), page);
    sub->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    sub->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    titleRow->addLayout(titleCol, 1);
    auto* btnNew = new QPushButton(QStringLiteral("+  新建机器人"), page);
    btnNew->setObjectName(QStringLiteral("aiBackendPurplePrimaryBtn"));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(btnNew, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("aiBackendDataCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    cardLay->addWidget(buildRobotConfigListHeader(card));
    cardLay->addWidget(makeRobotListHorizontalRule(card));

    auto* scroll = new QScrollArea(card);
    scroll->setObjectName(QStringLiteral("aiBackendRobotListScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* listBody = new QWidget;
    listBody->setObjectName(QStringLiteral("aiBackendRobotListScrollBody"));
    scroll->setWidget(listBody);
    auto* listLay = new QVBoxLayout(listBody);
    listLay->setContentsMargins(0, 0, 0, 8);
    listLay->setSpacing(0);

    QWidget* row1 = buildRobotConfigDataRow(
        listBody, QStringLiteral("官方品牌导购机器人"), QStringLiteral("rob-001"),
        QStringLiteral("Gemini 1.5 Pro"),
        {{QStringLiteral("产品知识"), QStringLiteral("aiBackendTagKb")},
         {QStringLiteral("F&A问答知识库"), QStringLiteral("aiBackendTagKb")},
         {QStringLiteral("基础应答策略"), QStringLiteral("aiBackendTagPolicy")},
         {QStringLiteral("分流转接策略"), QStringLiteral("aiBackendTagPolicy")}},
        QStringLiteral("官方旗舰店"), true,
        {{QStringLiteral("下线"), QStringLiteral("aiBackendActionMuted")},
         {QStringLiteral("配置"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("删除"), QStringLiteral("aiBackendActionDanger")}});
    listLay->addWidget(row1);

    listLay->addWidget(makeRobotListHorizontalRule(listBody));

    QWidget* row2 = buildRobotConfigDataRow(
        listBody, QStringLiteral("大促活动专服机器人"), QStringLiteral("rob-002"), QStringLiteral("GPT-4o"),
        {{QStringLiteral("产品知识"), QStringLiteral("aiBackendTagKb")},
         {QStringLiteral("发送策略"), QStringLiteral("aiBackendTagPolicy")},
         {QStringLiteral("违禁词拦截"), QStringLiteral("aiBackendTagPolicy")}},
        QStringLiteral("美妆生活馆\n数码精品店"), false,
        {{QStringLiteral("上线"), QStringLiteral("aiBackendActionOk")},
         {QStringLiteral("配置"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("删除"), QStringLiteral("aiBackendActionDanger")}});
    row2->setObjectName(QStringLiteral("aiBackendRobotListRowAlt"));
    listLay->addWidget(row2);
    listLay->addStretch(1);

    cardLay->addWidget(scroll, 1);
    outer->addWidget(card, 1);
    outer->addStretch(0);
    return page;
}

static QWidget* buildProductKnowledgePage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("aiBackendSubPage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 28);
    outer->setSpacing(16);

    auto* titleRow = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("产品知识库"), page);
    title->setObjectName(QStringLiteral("aiBackendDashTitle"));
    auto* sub = new QLabel(
        QStringLiteral("按店铺独立管理产品知识库，自动同步店铺商品信息，让 AI 更精准地回答产品咨询。"), page);
    sub->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    sub->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    titleRow->addLayout(titleCol, 1);
    auto* btnNew = new QPushButton(QStringLiteral("+  新增知识库"), page);
    btnNew->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(btnNew, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("aiBackendDataCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    cardLay->addWidget(buildProductKbListHeader(card));
    cardLay->addWidget(makeRobotListHorizontalRule(card));

    auto* scroll = new QScrollArea(card);
    scroll->setObjectName(QStringLiteral("aiBackendDataListScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* listBody = new QWidget;
    listBody->setObjectName(QStringLiteral("aiBackendDataListScrollBody"));
    scroll->setWidget(listBody);
    auto* listLay = new QVBoxLayout(listBody);
    listLay->setContentsMargins(0, 0, 0, 8);
    listLay->setSpacing(0);

    listLay->addWidget(buildProductKbDataRow(
        listBody, QStringLiteral("官方旗舰店产品库"), QStringLiteral("官方旗舰店"),
        QStringLiteral("245 款商品"), true, QStringLiteral("2026-04-20"),
        {{QStringLiteral("查看"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("重新获取"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("下线"), QStringLiteral("aiBackendActionMuted")},
         {QStringLiteral("配置"), QStringLiteral("aiBackendActionLink")}}));

    listLay->addWidget(makeRobotListHorizontalRule(listBody));

    QWidget* row2 = buildProductKbDataRow(
        listBody, QStringLiteral("美妆生活馆产品库"), QStringLiteral("美妆生活馆"), QStringLiteral("128 款商品"),
        false, QStringLiteral("2026-04-20"),
        {{QStringLiteral("查看"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("重新获取"), QStringLiteral("aiBackendActionLink")},
         {QStringLiteral("下线"), QStringLiteral("aiBackendActionMuted")},
         {QStringLiteral("配置"), QStringLiteral("aiBackendActionLink")}});
    row2->setObjectName(QStringLiteral("aiBackendRobotListRowAlt"));
    listLay->addWidget(row2);
    listLay->addStretch(1);

    cardLay->addWidget(scroll, 1);
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
    auto* bt = new QLabel(QStringLiteral("关于自动同步功能"), banner);
    bt->setObjectName(QStringLiteral("aiBackendInfoBannerTitle"));
    auto* bd = new QLabel(
        QStringLiteral(
            "绑定店铺后，系统将自动通过 API 获取该店铺的所有商品信息（包含商品详情图、规格参数等）进行向量化存储，"
            "以保证机器人能实时掌握最新产品动态。每个店铺可建立独立产品知识库以防止信息串台。"),
        banner);
    bd->setObjectName(QStringLiteral("aiBackendInfoBannerBody"));
    bd->setWordWrap(true);
    textCol->addWidget(bt);
    textCol->addWidget(bd);
    bh->addWidget(icon, 0, Qt::AlignTop);
    bh->addLayout(textCol, 1);
    outer->addWidget(banner, 0);

    outer->addStretch(0);
    return page;
}

static QWidget* buildFaqKnowledgePage()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("aiBackendSubPage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 28);
    outer->setSpacing(16);

    auto* titleRow = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("F&A 问答知识库"), page);
    title->setObjectName(QStringLiteral("aiBackendDashTitle"));
    auto* sub = new QLabel(
        QStringLiteral("配置店铺常见的咨询问答对，覆盖售后、物流、发票等通用场景。"), page);
    sub->setObjectName(QStringLiteral("aiBackendDashSubtitle"));
    sub->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(sub);
    titleRow->addLayout(titleCol, 1);
    auto* btnNew = new QPushButton(QStringLiteral("+  新增知识库"), page);
    btnNew->setObjectName(QStringLiteral("aiBackendBluePrimaryBtn"));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(btnNew, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("aiBackendDataCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    cardLay->addWidget(buildFaqKbListHeader(card));
    cardLay->addWidget(makeRobotListHorizontalRule(card));

    auto* scroll = new QScrollArea(card);
    scroll->setObjectName(QStringLiteral("aiBackendDataListScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* listBody = new QWidget;
    listBody->setObjectName(QStringLiteral("aiBackendDataListScrollBody"));
    scroll->setWidget(listBody);
    auto* listLay = new QVBoxLayout(listBody);
    listLay->setContentsMargins(0, 0, 0, 8);
    listLay->setSpacing(0);

    const auto kActions = std::initializer_list<std::tuple<QString, QString>>{
        {QStringLiteral("管理问答"), QStringLiteral("aiBackendActionLink")},
        {QStringLiteral("导入"), QStringLiteral("aiBackendActionLink")},
        {QStringLiteral("导出"), QStringLiteral("aiBackendActionLink")}};

    listLay->addWidget(buildFaqKbDataRow(
        listBody, QStringLiteral("通用问答库"), true, QStringLiteral("官方旗舰店, 美妆生活馆"),
        QStringLiteral("124 条"), QStringLiteral("2026-04-20"), kActions));

    listLay->addWidget(makeRobotListHorizontalRule(listBody));

    QWidget* row2 = buildFaqKbDataRow(listBody, QStringLiteral("官方旗舰店独立问答库"), false,
                                      QStringLiteral("官方旗舰店"), QStringLiteral("45 条"),
                                      QStringLiteral("2026-04-25"), kActions);
    row2->setObjectName(QStringLiteral("aiBackendRobotListRowAlt"));
    listLay->addWidget(row2);

    listLay->addWidget(makeRobotListHorizontalRule(listBody));

    listLay->addWidget(buildFaqKbDataRow(listBody, QStringLiteral("美妆生活馆独立问答库"), false,
                                         QStringLiteral("美妆生活馆"), QStringLiteral("32 条"),
                                         QStringLiteral("2026-04-26"), kActions));
    listLay->addStretch(1);

    cardLay->addWidget(scroll, 1);
    outer->addWidget(card, 1);
    outer->addStretch(0);
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
    m_stack->addWidget(buildRobotStoreConfigPage());
    m_stack->addWidget(buildProductKnowledgePage());
    m_stack->addWidget(buildFaqKnowledgePage());
    m_apiConfigPage = new AiProviderConfigPage(central);
    m_stack->addWidget(m_apiConfigPage);
    m_stack->addWidget(makePlaceholderPage(QStringLiteral("通用设置")));

    QWidget* nav = buildNavSidebar();
    root->addWidget(nav, 0);
    root->addWidget(m_stack, 1);

    connect(m_apiConfigPage, &AiProviderConfigPage::settingsSaved, this, [this]() {
        emit aiProviderConfigChanged();
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

    auto* kbGroup = new QTreeWidgetItem(m_nav, QStringList{QStringLiteral("知识库")});
    auto* prodKb = new QTreeWidgetItem(kbGroup, QStringList{QStringLiteral("产品知识")});
    prodKb->setData(0, Qt::UserRole, kStackProductKnowledge);
    auto* faqKb = new QTreeWidgetItem(kbGroup, QStringList{QStringLiteral("F&A问答知识库")});
    faqKb->setData(0, Qt::UserRole, kStackFaqKnowledge);

    addTopLeaf(QStringLiteral("API 配置/模型"), kStackApiModel);
    addTopLeaf(QStringLiteral("通用设置"), kStackGeneralSettings);

    agentGroup->setExpanded(true);
    kbGroup->setExpanded(true);

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
  padding: 8px 10px;
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
  padding: 12px 10px;
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
            .arg(QLatin1String(kAiBackendContentBg)));
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

