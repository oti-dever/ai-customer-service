#include "GroupReceptionDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QScreen>
#include <QStyle>

GroupReceptionDialog::GroupReceptionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
    setWindowTitle(QStringLiteral("管理后台-聚合接待"));

    const QSize screenSize = qApp->primaryScreen()->availableSize();
    resize(qMin(screenSize.width() * 0.85, 1200.0), qMin(screenSize.height() * 0.78, 720.0));
    setMinimumSize(880, 520);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    buildUI();
    applyStyle();
}

void GroupReceptionDialog::buildUI()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildLeftPanel());
    rootLayout->addWidget(buildCenterPanel(), 1);
    rootLayout->addWidget(buildRightPanel());
}

/**
 * @brief 左侧会话管理栏（#f5f7fa，固定宽度）
 */
QWidget* GroupReceptionDialog::buildLeftPanel()
{
    auto* left = new QWidget(this);
    left->setObjectName("receptionLeftPanel");
    left->setFixedWidth(280);

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    // 顶部：标题 + 小房子图标
    auto* titleRow = new QHBoxLayout();
    auto* titleLabel = new QLabel(QStringLiteral("聚合对话接待"), left);
    titleLabel->setObjectName("receptionLeftTitle");
    auto* homeBtn = new QPushButton(left);
    homeBtn->setObjectName("receptionHomeBtn");
    homeBtn->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
    homeBtn->setIconSize(QSize(18, 18));
    homeBtn->setFixedSize(28, 28);
    homeBtn->setFlat(true);
    titleRow->addWidget(titleLabel, 1);
    titleRow->addWidget(homeBtn);
    layout->addLayout(titleRow);

    // 当前模式
    auto* modeRow = new QHBoxLayout();
    auto* modeLabel = new QLabel(QStringLiteral("当前模式:"), left);
    modeLabel->setObjectName("receptionModeLabel");
    auto* modeCombo = new QComboBox(left);
    modeCombo->setObjectName("receptionModeCombo");
    modeCombo->addItem(QStringLiteral("人工休息"));
    modeCombo->addItem(QStringLiteral("机器人"));
    modeCombo->addItem(QStringLiteral("人工待接"));
    modeCombo->setMinimumHeight(28);
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(modeCombo, 1);
    layout->addLayout(modeRow);

    // 待处理 / 全部会话
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* btnPending = new QPushButton(QStringLiteral("待处理"), left);
    btnPending->setObjectName("receptionBtnPending");
    btnPending->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    btnPending->setIconSize(QSize(16, 16));
    btnPending->setFixedHeight(36);
    auto* btnAll = new QPushButton(QStringLiteral("全部会话"), left);
    btnAll->setObjectName("receptionBtnAll");
    btnAll->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    btnAll->setIconSize(QSize(16, 16));
    btnAll->setFixedHeight(36);
    btnRow->addWidget(btnPending);
    btnRow->addWidget(btnAll);
    layout->addLayout(btnRow);

    // 搜索框
    auto* searchEdit = new QLineEdit(left);
    searchEdit->setObjectName("receptionSearch");
    searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名..."));
    searchEdit->setClearButtonEnabled(false);
    searchEdit->setMinimumHeight(32);
    layout->addWidget(searchEdit);

    // 空状态：暂无会话
    auto* emptyWrap = new QFrame(left);
    emptyWrap->setObjectName("receptionLeftEmpty");
    auto* emptyLayout = new QVBoxLayout(emptyWrap);
    emptyLayout->setContentsMargins(20, 40, 20, 40);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* emptyIcon = new QLabel(QStringLiteral("💬"), emptyWrap);
    emptyIcon->setObjectName("receptionEmptyIcon");
    emptyIcon->setStyleSheet("font-size: 48px; color: #9ca3af;");
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto* emptyText = new QLabel(QStringLiteral("暂无会话"), emptyWrap);
    emptyText->setObjectName("receptionEmptyText");
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyText);
    layout->addWidget(emptyWrap, 1);

    return left;
}

/**
 * @brief 中间聊天交互主区（#f9fafb，占剩余宽度）
 */
QWidget* GroupReceptionDialog::buildCenterPanel()
{
    auto* center = new QWidget(this);
    center->setObjectName("receptionCenterPanel");

    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);

    // 空状态：橙色方形圆角 + 对话气泡图标（系统图标占位）
    auto* emptyIconBox = new QFrame(center);
    emptyIconBox->setObjectName("receptionCenterIconBox");
    emptyIconBox->setFixedSize(80, 80);
    auto* iconLayout = new QVBoxLayout(emptyIconBox);
    iconLayout->setAlignment(Qt::AlignCenter);
    auto* centerIcon = new QLabel(emptyIconBox);
    centerIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(40, 40));
    centerIcon->setAlignment(Qt::AlignCenter);
    centerIcon->setStyleSheet("background: transparent;");
    iconLayout->addWidget(centerIcon);

    auto* centerTitle = new QLabel(QStringLiteral("选择一个会话开始聊天"), center);
    centerTitle->setObjectName("receptionCenterTitle");
    centerTitle->setAlignment(Qt::AlignCenter);

    auto* centerDesc = new QLabel(
        QStringLiteral("从左侧列表中选择会话，开始与客户对话。优先处理转人工请求，提供及时的客户服务。"),
        center);
    centerDesc->setObjectName("receptionCenterDesc");
    centerDesc->setWordWrap(true);
    centerDesc->setAlignment(Qt::AlignCenter);

    layout->addWidget(emptyIconBox);
    layout->addSpacing(16);
    layout->addWidget(centerTitle);
    layout->addSpacing(8);
    layout->addWidget(centerDesc);

    return center;
}

/**
 * @brief 右侧客户信息栏（固定宽度，顶部深色栏）
 */
QWidget* GroupReceptionDialog::buildRightPanel()
{
    auto* right = new QWidget(this);
    right->setObjectName("receptionRightPanel");
    right->setFixedWidth(260);

    auto* layout = new QVBoxLayout(right);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部标题栏 #1f2937
    auto* rightHeader = new QFrame(right);
    rightHeader->setObjectName("receptionRightHeader");
    rightHeader->setFixedHeight(72);
    auto* headerLayout = new QHBoxLayout(rightHeader);
    headerLayout->setContentsMargins(12, 8, 8, 8);
    auto* headerCol = new QVBoxLayout();
    headerCol->setSpacing(4);
    auto* headerTitle = new QLabel(QStringLiteral("聚合对话接待"), rightHeader);
    headerTitle->setObjectName("receptionRightHeaderTitle");
    auto* headerSub = new QLabel(QStringLiteral("统一客户服务平台"), rightHeader);
    headerSub->setObjectName("receptionRightHeaderSub");
    headerCol->addWidget(headerTitle);
    headerCol->addWidget(headerSub);
    headerLayout->addLayout(headerCol, 1);
    auto* closeBtn = new QPushButton(QStringLiteral("×"), rightHeader);
    closeBtn->setObjectName("receptionRightCloseBtn");
    closeBtn->setFixedSize(28, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    headerLayout->addWidget(closeBtn);
    layout->addWidget(rightHeader);

    // 空状态区域
    auto* rightEmpty = new QFrame(right);
    rightEmpty->setObjectName("receptionRightEmpty");
    auto* rightEmptyLayout = new QVBoxLayout(rightEmpty);
    rightEmptyLayout->setContentsMargins(16, 16, 16, 16);
    rightEmptyLayout->setSpacing(12);
    rightEmptyLayout->setAlignment(Qt::AlignCenter);
    auto* userIcon = new QLabel(QStringLiteral("👤"), rightEmpty);
    userIcon->setStyleSheet("font-size: 56px; color: #9ca3af;");
    userIcon->setAlignment(Qt::AlignCenter);
    auto* rightEmptyTitle = new QLabel(QStringLiteral("客户信息"), rightEmpty);
    rightEmptyTitle->setObjectName("receptionRightEmptyTitle");
    rightEmptyTitle->setAlignment(Qt::AlignCenter);
    auto* rightEmptyDesc = new QLabel(QStringLiteral("选择会话查看详细信息"), rightEmpty);
    rightEmptyDesc->setObjectName("receptionRightEmptyDesc");
    rightEmptyDesc->setAlignment(Qt::AlignCenter);
    rightEmptyLayout->addWidget(userIcon);
    rightEmptyLayout->addWidget(rightEmptyTitle);
    rightEmptyLayout->addWidget(rightEmptyDesc);
    layout->addWidget(rightEmpty, 1);

    return right;
}

void GroupReceptionDialog::applyStyle()
{
    setStyleSheet(QStringLiteral(R"QSS(
        QDialog { background: #f9fafb; }
        /* 左侧会话管理栏 #f5f7fa */
        QWidget#receptionLeftPanel { background: #f5f7fa; }
        QLabel#receptionLeftTitle { color: #1f2937; font-size: 14px; font-weight: bold; }
        QPushButton#receptionHomeBtn { background: transparent; border: none; color: #6b7280; }
        QPushButton#receptionHomeBtn:hover { color: #1f2937; }
        QLabel#receptionModeLabel { color: #1f2937; font-size: 12px; }
        QComboBox#receptionModeCombo {
            background: #e5e7eb; border: 1px solid #d1d5db; border-radius: 4px;
            padding: 4px 8px; color: #1f2937; min-height: 28px;
        }
        QComboBox#receptionModeCombo::drop-down { border: none; width: 20px; }
        QPushButton#receptionBtnPending {
            background: #ff7d00; color: #ffffff; border: none; border-radius: 4px;
            padding: 0 12px; font-size: 13px;
        }
        QPushButton#receptionBtnPending:hover { background: #e66d00; }
        QPushButton#receptionBtnAll {
            background: #e5e7eb; color: #1f2937; border: none; border-radius: 4px;
            padding: 0 12px; font-size: 13px;
        }
        QPushButton#receptionBtnAll:hover { background: #d1d5db; }
        QLineEdit#receptionSearch {
            background: #ffffff; border: 1px solid #e5e7eb; border-radius: 4px;
            padding: 6px 10px; color: #1f2937;
        }
        QFrame#receptionLeftEmpty { background: transparent; }
        QLabel#receptionEmptyText { color: #1f2937; font-size: 12px; }

        /* 中间主区 #f9fafb */
        QWidget#receptionCenterPanel { background: #f9fafb; }
        QFrame#receptionCenterIconBox {
            background: #ff7d00; border-radius: 8px;
        }
        QFrame#receptionCenterIconBox QLabel { color: #ffffff; }
        QLabel#receptionCenterTitle { color: #1f2937; font-size: 16px; font-weight: bold; }
        QLabel#receptionCenterDesc { color: #6b7280; font-size: 12px; }

        /* 右侧客户信息栏 */
        QWidget#receptionRightPanel { background: #ffffff; }
        QFrame#receptionRightHeader { background: #1f2937; }
        QLabel#receptionRightHeaderTitle { color: #ffffff; font-size: 14px; font-weight: bold; }
        QLabel#receptionRightHeaderSub { color: #e5e7eb; font-size: 12px; }
        QPushButton#receptionRightCloseBtn {
            background: #374151; color: #ffffff; border: none; border-radius: 4px; font-size: 16px;
        }
        QPushButton#receptionRightCloseBtn:hover { background: #4b5563; }
        QFrame#receptionRightEmpty { background: #ffffff; }
        QLabel#receptionRightEmptyTitle { color: #1f2937; font-size: 12px; font-weight: bold; }
        QLabel#receptionRightEmptyDesc { color: #6b7280; font-size: 11px; }
    )QSS"));
}
