#pragma once

#include <QAbstractListModel>
#include <QIcon>
#include <QString>
#include <QVector>

/**
 * @brief 平台列表数据模型（Model）
 *
 * 目前仅用于界面展示：在线平台 / 离线平台。
 * 后续你可以在这里接入真实平台数据（网络/本地配置/数据库等），
 * View 不需要修改，只要更新 model 数据即可。
 */
class PlatformModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    /// 自定义数据角色枚举
    enum Role {
        TitleRole = Qt::UserRole + 1,     ///< 平台标题角色
        StatusTextRole,                   ///< 状态文本角色
        StatusDotColorRole,               ///< 状态圆点颜色角色
        ExpandedRole                      ///< 展开状态角色
    };

    /// 平台项数据结构
    struct Item {
        QString title;                    ///< 分组标题：在线平台/离线平台
        QString statusText;               ///< 辅助说明：例如"点击选择平台"
        QColor statusDotColor;            ///< 左侧小圆点颜色
        bool expanded = false;            ///< 是否展开（先占位，暂不实现交互）
        QIcon icon;                       ///< 可选图标
    };

    explicit PlatformModel(QObject* parent = nullptr);

    /// QAbstractListModel 接口实现
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 获取指定行的平台项
    const Item& itemAt(int row) const;

private:
    QVector<Item> m_items;                ///< 平台项数据容器
};
