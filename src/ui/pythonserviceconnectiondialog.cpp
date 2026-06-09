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
    setWindowTitle(QStringLiteral("Python 服务端连接"));
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
        QStringLiteral("配置 C++ 客户端要连接的独立 Python 服务端地址。Python 服务端需要单独启动，客户端不再负责启动或停止它。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto* serviceRow = new QHBoxLayout();
    serviceRow->setSpacing(8);
    m_serviceEndpointEdit = new QLineEdit(Ipc::IpcService::instance().serviceEndpoint(), this);
    m_serviceEndpointEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:8765"));
    m_btnSaveService = new QPushButton(QStringLiteral("保存连接"), this);
    m_btnTestService = new QPushButton(QStringLiteral("测试连接"), this);
    serviceRow->addWidget(m_serviceEndpointEdit, 1);
    serviceRow->addWidget(m_btnSaveService);
    serviceRow->addWidget(m_btnTestService);
    mainLayout->addLayout(serviceRow);

    QSettings settings = AppSettings::create();
    m_startupBackfillCheck = new QCheckBox(QStringLiteral("连接后同步服务端历史数据到本地缓存"), this);
    m_startupBackfillCheck->setChecked(
        settings.value(QStringLiteral("pythonService/startupBackfillEnabled"), false).toBool());
    m_startupBackfillCheck->setToolTip(
        QStringLiteral("默认关闭。当前历史同步链路仍在优化，开启后会从 Python 服务拉取 replay/snapshot 并写入客户端缓存。"));
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
    QMessageBox::information(this, QStringLiteral("Python 服务端"), QStringLiteral("服务端配置已保存。"));
}

void PythonServiceConnectionDialog::onTestServiceClicked()
{
    onSaveServiceClicked();
    QString error;
    const bool ok = Ipc::IpcService::instance().connectToConfiguredService(&error);
    QMessageBox::information(
        this,
        QStringLiteral("Python 服务端"),
        ok ? QStringLiteral("连接成功。") : QStringLiteral("连接失败：%1").arg(error));
}
