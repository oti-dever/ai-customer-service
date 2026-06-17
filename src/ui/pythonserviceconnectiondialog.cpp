#include "pythonserviceconnectiondialog.h"
#include "mainwindow.h"
#include "../ipc/ipcservice.h"
#include "../utils/applystyle.h"
#include "../utils/appsettings.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

PythonServiceConnectionDialog::PythonServiceConnectionDialog(MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent)
    , m_main(mainWindow)
{
    setWindowTitle(QStringLiteral("本机 Python 服务设置"));
    {
        const ApplyStyle::MainWindowTheme th = m_main ? m_main->mainWindowTheme()
                                                      : ApplyStyle::MainWindowTheme::Default;
        setStyleSheet(ApplyStyle::addWindowDialogStyle(th));
    }
    resize(560, 220);
    setupUI();
}

void PythonServiceConnectionDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* hint = new QLabel(
        QStringLiteral("当前版本按单机客服工作台运行。Python 服务通常由聚合界面左侧按钮启动或停止；这里仅用于查看或调整本机服务地址，默认保持 http://127.0.0.1:8765。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto* serviceRow = new QHBoxLayout();
    serviceRow->setSpacing(8);
    m_serviceEndpointEdit = new QLineEdit(Ipc::IpcService::instance().serviceEndpoint(), this);
    m_serviceEndpointEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:8765"));
    m_btnSaveService = new QPushButton(QStringLiteral("保存设置"), this);
    m_btnTestService = new QPushButton(QStringLiteral("测试本机服务"), this);
    serviceRow->addWidget(m_serviceEndpointEdit, 1);
    serviceRow->addWidget(m_btnSaveService);
    serviceRow->addWidget(m_btnTestService);
    mainLayout->addLayout(serviceRow);

    QSettings settings = AppSettings::create();
    m_startupBackfillCheck = new QCheckBox(QStringLiteral("调试：连接后拉取历史快照"), this);
    m_startupBackfillCheck->setChecked(
        settings.value(QStringLiteral("pythonService/startupBackfillEnabled"),
                       false).toBool());
    m_startupBackfillCheck->setToolTip(
        QStringLiteral("默认关闭。单机模式下聚合界面直接读取 app_data.db，通常不需要开启；仅排查历史同步链路时使用。"));
    mainLayout->addWidget(m_startupBackfillCheck);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(10);
    m_btnClose = new QPushButton(QStringLiteral("关闭"), this);
    bottom->addStretch(1);
    bottom->addWidget(m_btnClose);
    mainLayout->addLayout(bottom);

    connect(m_btnSaveService, &QPushButton::clicked,
            this, &PythonServiceConnectionDialog::onSaveServiceClicked);
    connect(m_btnTestService, &QPushButton::clicked,
            this, &PythonServiceConnectionDialog::onTestServiceClicked);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::reject);
}

void PythonServiceConnectionDialog::onSaveServiceClicked()
{
    auto& ipc = Ipc::IpcService::instance();
    ipc.setServiceEndpoint(m_serviceEndpointEdit ? m_serviceEndpointEdit->text() : QString());
    ipc.saveConnectionSettings();
    QSettings settings = AppSettings::create();
    settings.setValue(
        QStringLiteral("pythonService/startupBackfillEnabled"),
        m_startupBackfillCheck ? m_startupBackfillCheck->isChecked() : false);
    QMessageBox::information(this, QStringLiteral("本机 Python 服务"), QStringLiteral("本机服务设置已保存。"));
}

void PythonServiceConnectionDialog::onTestServiceClicked()
{
    onSaveServiceClicked();
    QString error;
    const bool ok = Ipc::IpcService::instance().connectToConfiguredService(&error);
    QMessageBox::information(
        this,
        QStringLiteral("本机 Python 服务"),
        ok ? QStringLiteral("连接成功。") : QStringLiteral("连接失败：%1").arg(error));
}
