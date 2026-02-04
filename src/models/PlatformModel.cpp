#include "PlatformModel.h"

#include <QApplication>
#include <QStyle>

/**
 * @brief 构造函数，初始化平台数据模型
 * @param parent 父对象
 */
PlatformModel::PlatformModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // 使用占位数据，满足效果图展示。后续可以由 Controller 注入真实数据。
    const QIcon listIcon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);

    // 添加"在线平台"项
    m_items.push_back(Item{
        .title = QStringLiteral("在线平台"),
        .statusText = QStringLiteral(""),
        .statusDotColor = QColor(0, 200, 120),  // 绿色圆点表示在线
        .expanded = true,                       // 默认展开
        .icon = listIcon,
    });

    // 添加"离线平台"项
    m_items.push_back(Item{
        .title = QStringLiteral("离线平台"),
        .statusText = QStringLiteral(""),
        .statusDotColor = QColor(160, 160, 160), // 灰色圆点表示离线
        .expanded = false,                       // 默认不展开
        .icon = listIcon,
    });
}

/**
 * @brief 返回模型行数
 * @param parent 父索引
 * @return 数据项数量
 */
int PlatformModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;  // 列表模型，父索引有效时返回0
    return m_items.size();
}

/**
 * @brief 返回指定索引和角色的数据
 * @param index 模型索引
 * @param role 数据角色
 * @return 对应角色的数据
 */
QVariant PlatformModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};  // 索引无效或越界时返回空值

    const Item& it = m_items[index.row()];

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return it.title;                     // 返回标题
    case StatusTextRole:
        return it.statusText;                // 返回状态文本
    case StatusDotColorRole:
        return it.statusDotColor;            // 返回状态圆点颜色
    case ExpandedRole:
        return it.expanded;                  // 返回展开状态
    case Qt::DecorationRole:
        return it.icon;                      // 返回图标
    default:
        return {};                           // 未知角色返回空值
    }
}

/**
 * @brief 返回角色名称映射表
 * @return 角色名称的哈希表
 */
QHash<int, QByteArray> PlatformModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();  // 获取基类角色
    roles[TitleRole] = "title";                    // 添加自定义角色
    roles[StatusTextRole] = "statusText";
    roles[StatusDotColorRole] = "statusDotColor";
    roles[ExpandedRole] = "expanded";
    return roles;
}

/**
 * @brief 获取指定行的平台项
 * @param row 行索引
 * @return 对应行的平台项引用
 */
const PlatformModel::Item& PlatformModel::itemAt(int row) const
{
    return m_items[row];
}
