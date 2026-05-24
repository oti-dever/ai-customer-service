#include "sidebartocdelegate.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {

struct TocPalette {
    QColor selBg;
    QColor selText;
    QColor hovBg;
    QColor hovText;
    QColor normalText;
};

TocPalette tocPalette(ApplyStyle::MainWindowTheme theme)
{
    switch (theme) {
    case ApplyStyle::MainWindowTheme::Cool:
        return {QColor(QStringLiteral("#0284c7")), QColor(QStringLiteral("#f8fafc")),
                QColor(QStringLiteral("#1e293b")), QColor(QStringLiteral("#f1f5f9")),
                QColor(QStringLiteral("#e2e8f0"))};
    case ApplyStyle::MainWindowTheme::Warm:
        return {QColor(QStringLiteral("#b45309")), QColor(QStringLiteral("#fffbeb")),
                QColor(QStringLiteral("#3d3835")), QColor(QStringLiteral("#fff7ed")),
                QColor(QStringLiteral("#f5efe6"))};
    case ApplyStyle::MainWindowTheme::Default:
    default:
        return {QColor(QStringLiteral("#3F3F46")), QColor(QStringLiteral("#FAFAFA")),
                QColor(QStringLiteral("#E4E4E7")), QColor(QStringLiteral("#18181B")),
                QColor(QStringLiteral("#27272A"))};
    }
}

} // namespace

SidebarTocDelegate::SidebarTocDelegate(QTreeWidget* tree, ApplyStyle::MainWindowTheme theme, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_tree(tree)
    , m_theme(theme)
{
}

void SidebarTocDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    QTreeWidgetItem* item = m_tree ? m_tree->itemFromIndex(index) : nullptr;
    if (!item || item->childCount() == 0) {
        paintLeaf(painter, option, index, item);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const TocPalette c = tocPalette(m_theme);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect cell = opt.rect.adjusted(4, 2, -4, -2);
    if (opt.state.testFlag(QStyle::State_MouseOver)) {
        painter->setBrush(c.hovBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(cell, 8, 8);
    }

    QFont f = opt.font;
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(c.normalText);

    static const QIcon expandIcon(QStringLiteral(":/fold_arrow_to_expand_icon.svg"));
    static const QIcon collapseIcon(QStringLiteral(":/fold_arrow_to_collapse_icon.svg"));
    const QPixmap pix = (item->isExpanded() ? collapseIcon : expandIcon).pixmap(QSize(16, 16));

    const int rightReserve = pix.isNull() ? 8 : 8 + pix.width() + 4;
    const QRect textRect(cell.left() + 8, cell.top(), cell.width() - 8 - rightReserve, cell.height());
    const QString elided = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elided);

    if (!pix.isNull()) {
        const QPoint pt(cell.right() - pix.width() - 4, cell.center().y() - pix.height() / 2);
        painter->drawPixmap(pt, pix);
    }
    painter->restore();
}

bool SidebarTocDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                                     const QModelIndex& index)
{
    Q_UNUSED(model);
    if (!m_tree || event->type() != QEvent::MouseButtonRelease)
        return false;
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() != Qt::LeftButton)
        return false;

    QTreeWidgetItem* item = m_tree->itemFromIndex(index);
    if (!item || item->childCount() == 0)
        return false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint p = me->position().toPoint();
#else
    const QPoint p = me->pos();
#endif
    if (!option.rect.contains(p))
        return false;

    item->setExpanded(!item->isExpanded());
    return true;
}

void SidebarTocDelegate::paintLeaf(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                                   QTreeWidgetItem* item) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const TocPalette c = tocPalette(m_theme);
    const QRect r = opt.rect.adjusted(4, 2, -4, -2);
    const bool isChild = item && item->parent();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (opt.state.testFlag(QStyle::State_Selected)) {
        painter->setBrush(c.selBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(r, 8, 8);
        painter->setPen(c.selText);
    } else if (opt.state.testFlag(QStyle::State_MouseOver)) {
        painter->setBrush(c.hovBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(r, 8, 8);
        painter->setPen(c.hovText);
    } else {
        painter->setPen(c.normalText);
    }

    QFont f = opt.font;
    f.setBold(false);
    painter->setFont(f);
    const int textLeft = r.left() + 10 + (isChild ? 12 : 0);
    const QRect textRect(textLeft, r.top(), r.right() - 8 - textLeft, r.height());
    const QString elided = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elided);
    painter->restore();
}
