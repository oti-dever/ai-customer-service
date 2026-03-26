#include "rpamanagedialog.h"
#include "mainwindow.h"
#include "../utils/applystyle.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

const QString kTipSelectRpaFirst = QStringLiteral("请先选择RPA项");

static const QIcon& prohibitIcon()
{
    static const QIcon icon(QStringLiteral(":/prohibit_icon.svg"));
    return icon;
}

} // namespace

RpaManageDialog::RpaManageDialog(MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent)
    , m_main(mainWindow)
{
    setWindowTitle(QStringLiteral("管理启动/停止 RPA"));
    setStyleSheet(ApplyStyle::addWindowDialogStyle()
                   + QStringLiteral(
                       R"QSS(
        QDialog QPushButton:disabled {
            color: #1e293b;
            background-color: #ffffff;
            border: 1px solid #c0d9f7;
            border-radius: 8px;
            padding: 6px 16px;
            min-height: 22px;
        }
    )QSS"));
    resize(520, 360);
    setupUI();
}

void RpaManageDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    syncCheckboxesFromRunning();
    updateButtonStates();
}

void RpaManageDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* hint = new QLabel(
        QStringLiteral("勾选要启动或停止的平台。正在运行的项在下次打开本窗口时会默认勾选，便于直接停止。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto* rowBtns = new QHBoxLayout();
    rowBtns->setSpacing(8);
    auto* btnAll = new QPushButton(QStringLiteral("全选"), this);
    auto* btnNone = new QPushButton(QStringLiteral("取消全选"), this);
    rowBtns->addWidget(btnAll);
    rowBtns->addWidget(btnNone);
    rowBtns->addStretch(1);
    mainLayout->addLayout(rowBtns);

    struct Item {
        const char* id;
        const char* label;
    };
    static const Item items[] = {
        {"wechat", "微信 PC（wechat_pc）"},
        {"qianniu", "千牛 PC"},
        {"pdd", "拼多多网页"},
    };

    auto* listArea = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listArea);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);

    for (const auto& it : items) {
        auto* cb = new QCheckBox(QString::fromUtf8(it.label), listArea);
        m_checks.insert(QString::fromLatin1(it.id), cb);
        listLayout->addWidget(cb);
        connect(cb, &QCheckBox::stateChanged, this, &RpaManageDialog::onCheckboxChanged);
    }
    mainLayout->addWidget(listArea, 1);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(10);
    m_btnStart = new QPushButton(QStringLiteral("启动"), this);
    m_btnStop = new QPushButton(QStringLiteral("停止"), this);
    m_btnClose = new QPushButton(QStringLiteral("关闭"), this);
    const QSize iconSz(18, 18);
    m_btnStart->setIconSize(iconSz);
    m_btnStop->setIconSize(iconSz);
    bottom->addStretch(1);
    bottom->addWidget(m_btnStart);
    bottom->addWidget(m_btnStop);
    bottom->addWidget(m_btnClose);
    mainLayout->addLayout(bottom);

    connect(btnAll, &QPushButton::clicked, this, &RpaManageDialog::onSelectAllClicked);
    connect(btnNone, &QPushButton::clicked, this, &RpaManageDialog::onDeselectAllClicked);
    connect(m_btnStart, &QPushButton::clicked, this, &RpaManageDialog::onStartClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &RpaManageDialog::onStopClicked);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::reject);
}

QStringList RpaManageDialog::checkedPlatformIds() const
{
    QStringList out;
    for (auto it = m_checks.constBegin(); it != m_checks.constEnd(); ++it) {
        if (it.value() && it.value()->isChecked())
            out.append(it.key());
    }
    return out;
}

void RpaManageDialog::syncCheckboxesFromRunning()
{
    if (!m_main)
        return;
    const QStringList running = m_main->runningRpaPlatformIds();
    for (auto it = m_checks.constBegin(); it != m_checks.constEnd(); ++it) {
        if (it.value())
            it.value()->setChecked(running.contains(it.key()));
    }
}

void RpaManageDialog::updateButtonStates()
{
    if (!m_main) {
        m_btnStart->setEnabled(true);
        m_btnStop->setEnabled(true);
        m_btnStart->setIcon(prohibitIcon());
        m_btnStart->setToolTip(kTipSelectRpaFirst);
        m_btnStop->setIcon(prohibitIcon());
        m_btnStop->setToolTip(kTipSelectRpaFirst);
        return;
    }

    const QStringList checked = checkedPlatformIds();
    const QStringList running = m_main->runningRpaPlatformIds();

    bool anyCheckedRunning = false;
    for (const QString& id : checked) {
        if (running.contains(id))
            anyCheckedRunning = true;
    }

    m_btnStart->setEnabled(true);
    if (checked.isEmpty()) {
        m_btnStart->setIcon(prohibitIcon());
        m_btnStart->setToolTip(kTipSelectRpaFirst);
    } else {
        m_btnStart->setIcon(QIcon());
        m_btnStart->setToolTip(QString());
    }

    m_btnStop->setEnabled(true);
    const bool stopCanAct = (!checked.isEmpty() && anyCheckedRunning);
    if (running.isEmpty()) {
        m_btnStop->setIcon(prohibitIcon());
        m_btnStop->setToolTip(kTipSelectRpaFirst);
    } else {
        m_btnStop->setIcon(QIcon());
        m_btnStop->setToolTip(stopCanAct ? QString() : kTipSelectRpaFirst);
    }
}

void RpaManageDialog::onSelectAllClicked()
{
    for (auto it = m_checks.begin(); it != m_checks.end(); ++it) {
        if (it.value())
            it.value()->setChecked(true);
    }
}

void RpaManageDialog::onDeselectAllClicked()
{
    for (auto it = m_checks.begin(); it != m_checks.end(); ++it) {
        if (it.value())
            it.value()->setChecked(false);
    }
}

void RpaManageDialog::onCheckboxChanged()
{
    updateButtonStates();
}

void RpaManageDialog::onStartClicked()
{
    if (!m_main)
        return;

    const QStringList checked = checkedPlatformIds();
    if (checked.isEmpty())
        return;

    const QStringList running = m_main->runningRpaPlatformIds();
    QStringList toStart;
    for (const QString& id : checked) {
        if (!running.contains(id))
            toStart.append(id);
    }

    if (toStart.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("启动 RPA"),
            QStringLiteral("当前勾选的平台均已启动，或没有可启动的项。\n请取消勾选已运行项，或勾选尚未启动的平台。"));
        return;
    }

    m_main->startRpaPlatforms(toStart);
    syncCheckboxesFromRunning();
    updateButtonStates();
}

void RpaManageDialog::onStopClicked()
{
    if (!m_main)
        return;

    const QStringList checked = checkedPlatformIds();
    const QStringList running = m_main->runningRpaPlatformIds();
    if (running.isEmpty())
        return;

    QStringList toStop;
    for (const QString& id : checked) {
        if (running.contains(id))
            toStop.append(id);
    }

    if (toStop.isEmpty())
        return;

    m_main->stopRpaPlatforms(toStop);
    syncCheckboxesFromRunning();
    updateButtonStates();
}
