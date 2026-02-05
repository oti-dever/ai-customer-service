#include "AddWindowDialogView.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

/**
 * @brief 构造函数
 * @param parent 父窗口指针
 */
AddWindowDialogView::AddWindowDialogView(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("添加新窗口 - 羊羊AI客服"));  // 设置窗口标题
    setModal(true);       // 设置为模态对话框
    resize(820, 560);    // 设置对话框大小

    buildUi();     // 构建界面
    applyStyle();  // 应用样式
}

/**
 * @brief 构建用户界面
 *
 * 负责创建和布局所有界面组件。
 */
void AddWindowDialogView::buildUi()
{
    // 创建根布局（垂直布局）
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 12, 16, 16);  // 设置边距（左、上、右、下）
    rootLayout->setSpacing(10);  // 设置组件间距

    // 创建顶部搜索框
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("searchEdit");  // 设置对象名称（用于样式表选择器）
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索窗口..."));  // 设置占位文本
    rootLayout->addWidget(m_searchEdit);  // 将搜索框添加到根布局

    // 创建中间区域（左右两栏）
    auto* center = new QWidget(this);
    auto* centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(10);

    // === 左侧：窗口列表区域 ===
    auto* leftPanel = new QWidget(center);
    leftPanel->setObjectName("leftPanel");
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 10, 10, 10);
    leftLayout->setSpacing(8);

    // 左上工具条：包含标题和刷新按钮
    auto* leftTop = new QWidget(leftPanel);
    auto* leftTopLayout = new QHBoxLayout(leftTop);
    leftTopLayout->setContentsMargins(0, 0, 0, 0);
    leftTopLayout->setSpacing(4);

    // 窗口列表标题
    auto* windowListTitle = new QLabel(QStringLiteral("窗口列表"), leftTop);
    windowListTitle->setObjectName("windowListTitle");

    leftTopLayout->addWidget(windowListTitle);  // 添加标题
    leftTopLayout->addStretch(1);               // 添加伸缩空间

    // 刷新资源
    m_btnRefresh = new QPushButton(leftTop);
    m_btnRefresh->setObjectName("btnRefresh");
    // m_btnRefresh->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    // 使用项目资源中的图标
    m_btnRefresh->setIcon(QIcon(":/res/RefreshWindowList.png"));
    m_btnRefresh->setToolTip(QStringLiteral("刷新窗口列表"));
    m_btnRefresh->setFlat(true);  // 设置扁平样式（无边框）
    leftTopLayout->addWidget(m_btnRefresh);  // 添加刷新按钮

    leftLayout->addWidget(leftTop);  // 将工具条添加到左侧布局

    // 窗口列表视图
    m_windowList = new QListView(leftPanel);
    m_windowList->setObjectName("windowList");
    m_windowList->setAlternatingRowColors(true);  // 启用交替行颜色
    leftLayout->addWidget(m_windowList, 1);  // 添加列表并设置拉伸因子为1

    centerLayout->addWidget(leftPanel, 2); // 将左侧面板添加到中心布局，拉伸因子为2

    // === 右侧：表单区域 ===
    auto* rightPanel = new QWidget(center);
    rightPanel->setObjectName("rightPanel");
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(16, 14, 16, 8);
    rightLayout->setSpacing(12);

    // Lambda函数：创建表单行（标签 + 输入框）
    auto makeRow = [&](const QString& labelText, QLineEdit*& edit, const QString& placeholder = QString()) {
        auto* row = new QWidget(rightPanel);
        row->setMinimumHeight(36);  // 设置最小高度
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        // 创建标签
        auto* label = new QLabel(labelText, row);
        label->setFixedWidth(72);  // 固定标签宽度
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);  // 右对齐，垂直居中

        // 创建输入框
        edit = new QLineEdit(row);
        edit->setMinimumHeight(28);  // 设置最小高度
        if (!placeholder.isEmpty())
            edit->setPlaceholderText(placeholder);  // 设置占位文本

        rowLayout->addWidget(label);    // 添加标签
        rowLayout->addWidget(edit, 1);  // 添加输入框，拉伸因子为1

        rightLayout->addWidget(row);  // 将行添加到右侧布局
    };

    // 右侧区域标题
    auto* infoTitle = new QLabel(QStringLiteral("窗口信息"), rightPanel);
    infoTitle->setObjectName("infoTitle");
    rightLayout->addWidget(infoTitle);

    // 创建表单行
    makeRow(QStringLiteral("平台名:"), m_platformNameEdit, QStringLiteral("自动读取窗口标题"));
    makeRow(QStringLiteral("进程名:"), m_processNameEdit, QStringLiteral("进程名称"));
    makeRow(QStringLiteral("窗口标题:"), m_windowTitleEdit, QStringLiteral("窗口标题（可选，留空则不作为筛选条件）"));
    makeRow(QStringLiteral("窗口类名:"), m_windowClassEdit, QStringLiteral("窗口类名"));
    makeRow(QStringLiteral("窗口句柄:"), m_windowHandleEdit, QStringLiteral("窗口句柄"));

    rightLayout->addStretch(1); // 添加伸缩空间

    // 底部按钮行
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);
    btnRow->addStretch(1);  // 左侧伸缩空间（使按钮靠右）

    // 创建取消和确定按钮
    m_btnCancel = new QPushButton(QStringLiteral("取消"), rightPanel);
    m_btnOk = new QPushButton(QStringLiteral("确定"), rightPanel);
    m_btnOk->setDefault(true); // 设置为默认按钮（响应回车键）

    btnRow->addWidget(m_btnCancel);  // 添加取消按钮
    btnRow->addWidget(m_btnOk);      // 添加确定按钮
    rightLayout->addLayout(btnRow);  // 将按钮布局添加到右侧布局

    centerLayout->addWidget(rightPanel, 3);  // 将右侧面板添加到中心布局，拉伸因子为3

    rootLayout->addWidget(center, 1);  // 将中心区域添加到根布局，拉伸因子为1

    // ======================= 连接信号与槽 =======================
    // 当前阶段只实现基本对话框行为：确定/取消关闭窗口。
    // 实际业务逻辑将在Controller中实现。
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);  // 取消按钮->关闭对话框并返回reject
    connect(m_btnOk, &QPushButton::clicked, this, &QDialog::accept);      // 确定按钮->关闭对话框并返回accept
}

/**
 * @brief 应用样式表
 *
 * 设置对话框和各控件的样式。
 * 注意：样式表中使用了对象名称选择器，确保控件的objectName已正确设置。
 */
void AddWindowDialogView::applyStyle()
{
    setStyleSheet(QStringLiteral(R"QSS(
        /* ----- 对话框背景 ----- */
        QDialog {
            background: #f5f7fb;  /* 浅灰色背景 */
            color: #000000;  /* 设置对话框默认字体颜色为黑色 */
        }

        /* ----- 搜索框样式 ----- */
        QLineEdit#searchEdit {
            background: #ffffff;       /* 白色背景 */
            border-radius: 6px;       /* 圆角半径 */
            border: 1px solid #d9e1f2; /* 边框颜色 */
            padding: 6px 10px;        /* 内边距 */
            color: #1f2933;  /* 明确设置字体颜色为深灰色 */
        }

        /* ----- 左右面板样式 ----- */
        QWidget#leftPanel, QWidget#rightPanel {
            background: #ffffff;       /* 白色背景 */
            border-radius: 8px;       /* 圆角半径 */
            border: 1px solid #e2e6f0; /* 边框颜色 */
        }

        /* ----- 标题标签样式 ----- */
        QLabel#windowListTitle, QLabel#infoTitle {
            font-size: 13px;          /* 字体大小 */
            font-weight: 600;         /* 字体粗细（半粗体） */
            color: #1f2933;           /* 深灰色文字 */
        }

        /* ----- 右侧面板中的标签样式 ----- */
        QWidget#rightPanel QLabel {
            color: #1f2933;           /* 深灰色文字 */
            font-size: 13px;          /* 字体大小 */
        }

        /* ----- 右侧面板中的输入框样式 ----- */
        QWidget#rightPanel QLineEdit {
            background: #ffffff;       /* 白色背景 */
            border: 1px solid #d9e1f2; /* 边框颜色 */
            border-radius: 6px;       /* 圆角半径 */
            padding: 6px 10px;        /* 内边距 */
            min-height: 28px;         /* 最小高度 */
            color: #1f2933;           /* 深灰色文字 */
        }

        /* ----- 窗口列表样式 ----- */
        QListView#windowList {
            border: none;             /* 无边框 */
            background: transparent;  /* 透明背景 */
        }
        QListView#windowList::item {
            padding: 6px 8px;         /* 列表项内边距 */
        }
        QListView#windowList::item:selected {
            background: #eef2ff;      /* 选中项背景色（浅蓝色） */
        }

        /* ----- 刷新按钮样式 ----- */
        QPushButton#btnRefresh {
            border: none;             /* 无边框 */
            padding: 4px;             /* 内边距 */
            background: transparent;  /* 透明背景 */
        }
        /* 刷新按钮悬停状态 */
        QPushButton#btnRefresh:hover {
            background: #e0f2fe;      /* 悬停时浅蓝色背景 */
        }
        /* 刷新按钮按下状态 - 修改为浅蓝色 */
        QPushButton#btnRefresh:pressed {
            background: #93c5fd;      /* 按下时浅蓝色背景 */
        }

        /* ----- 通用按钮样式 ----- */
        QPushButton {
            min-width: 72px;          /* 最小宽度 */
            padding: 5px 14px;        /* 内边距 */
            border-radius: 4px;       /* 圆角半径 */
        }

        /* ----- 默认按钮（确定按钮）样式 ----- */
        QPushButton:default {
            background: #2563eb;      /* 蓝色背景 */
            color: white;             /* 白色文字 */
            border: none;             /* 无边框 */
        }
        QPushButton:default:hover {
            background: #1d4ed8;      /* 悬停时的深蓝色背景 */
        }
        /* 确定按钮按下状态 - 修改为浅蓝色 */
        QPushButton:default:pressed {
            background: #60a5fa;      /* 按下时浅蓝色背景 */
        }

        /* ----- 普通按钮（取消按钮）样式 ----- */
        QPushButton {
            background: #f3f4f6;      /* 浅灰色背景 */
            border: 1px solid #d1d5db; /* 边框颜色 */
            color: #374151;           /* 深灰色文字 */
        }
        QPushButton:hover {
            background: #e5e7eb;      /* 悬停时的背景色 */
        }
        /* 取消按钮按下状态 - 修改为浅蓝色 */
        QPushButton:pressed {
            background: #93c5fd;      /* 按下时浅蓝色背景 */
        }
    )QSS"));
}
