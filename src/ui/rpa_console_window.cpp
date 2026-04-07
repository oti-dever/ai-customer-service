#include "rpa_console_window.h"
#include "foldarrowcombobox.h"
#include "mainwindow.h"
#include "../utils/applystyle.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

RpaConsoleWindow::RpaConsoleWindow(MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent)
    , m_main(mainWindow)
{
    setWindowTitle(QStringLiteral("控制台输出"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setStyleSheet(ApplyStyle::addWindowDialogStyle(
        m_main ? m_main->mainWindowTheme() : ApplyStyle::loadSavedMainWindowTheme()));
    resize(780, 520);
    setupUi();

    if (m_main) {
        connect(m_main, &MainWindow::rpaProcessOutputAppended,
                this, &RpaConsoleWindow::onOutputAppended);
    }
    reloadCurrentLog();
}

void RpaConsoleWindow::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(new QLabel(QStringLiteral("平台："), this));
    m_combo = new FoldArrowComboBox(this);
    m_combo->addItem(QStringLiteral("微信 PC"), QStringLiteral("wechat"));
    m_combo->addItem(QStringLiteral("千牛 PC"), QStringLiteral("qianniu"));
    m_combo->addItem(QStringLiteral("拼多多网页"), QStringLiteral("pdd"));
    row->addWidget(m_combo, 1);
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), this);
    clearBtn->setToolTip(QStringLiteral("清空当前平台已缓存的控制台输出"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    row->addWidget(clearBtn);
    root->addLayout(row);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    // 输出行过长时自动换行，避免只能水平滚动看不全
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setFont(QFont(QStringLiteral("Consolas"), 10));
    root->addWidget(m_log, 1);

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RpaConsoleWindow::onPlatformChanged);
    connect(clearBtn, &QPushButton::clicked, this, &RpaConsoleWindow::onClearClicked);
}

QString RpaConsoleWindow::currentPlatformId() const
{
    if (!m_combo)
        return {};
    return m_combo->currentData().toString();
}

void RpaConsoleWindow::reloadCurrentLog()
{
    if (!m_log || !m_main)
        return;
    const QString id = currentPlatformId();
    m_log->setPlainText(m_main->rpaProcessLog(id));
    {
        QScrollBar* sb = m_log->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void RpaConsoleWindow::appendToView(const QString& text)
{
    if (!m_log || text.isEmpty())
        return;
    QScrollBar* sb = m_log->verticalScrollBar();
    const bool stickToEnd = sb->value() >= sb->maximum() - 24;

    QTextCursor c = m_log->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(text);
    m_log->setTextCursor(c);

    if (stickToEnd) {
        sb->setValue(sb->maximum());
    }
}

void RpaConsoleWindow::onPlatformChanged(int)
{
    reloadCurrentLog();
}

void RpaConsoleWindow::onClearClicked()
{
    if (!m_main)
        return;
    const QString id = currentPlatformId();
    if (id.isEmpty())
        return;
    m_main->clearRpaProcessLog(id);
    reloadCurrentLog();
}

void RpaConsoleWindow::onOutputAppended(const QString& platformId, const QString& text)
{
    if (!m_main || platformId != currentPlatformId())
        return;
    appendToView(text);
}
