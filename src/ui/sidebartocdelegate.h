#ifndef SIDEBARTOCDELEGATE_H
#define SIDEBARTOCDELEGATE_H

#include "../utils/applystyle.h"

#include <QStyledItemDelegate>

class QTreeWidget;
class QTreeWidgetItem;

/** 与帮助中心左栏一致：无系统分支线、分组行右侧折叠箭头、叶子圆角选中块。 */
class SidebarTocDelegate : public QStyledItemDelegate
{
public:
    explicit SidebarTocDelegate(QTreeWidget* tree, ApplyStyle::MainWindowTheme theme, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

private:
    void paintLeaf(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                   QTreeWidgetItem* item) const;

    QTreeWidget* m_tree = nullptr;
    ApplyStyle::MainWindowTheme m_theme = ApplyStyle::MainWindowTheme::Default;
};

#endif // SIDEBARTOCDELEGATE_H
