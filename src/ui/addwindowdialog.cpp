#include "addwindowdialog.h"
#include "mainwindow.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

AddWindowDialog::AddWindowDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("添加新窗口"));
    resize(760, 520);
    setupUI();
    onRefreshClicked();
}

void AddWindowDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    auto* searchLabel = new QLabel(QStringLiteral("搜索窗口："), this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("按窗口标题或进程名搜索..."));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), this);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_btnRefresh);
    mainLayout->addLayout(searchRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    QStringList headers;
    headers << QStringLiteral(" ") << QStringLiteral("窗口标题") << QStringLiteral("进程名") << QStringLiteral("窗口类名") << QStringLiteral("句柄");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 36);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    mainLayout->addWidget(m_table, 1);

    auto* formGroup = new QWidget(this);
    auto* formLayout = new QGridLayout(formGroup);
    formLayout->setContentsMargins(0, 8, 0, 0);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(8);

    int row = 0;
    formLayout->addWidget(new QLabel(QStringLiteral("平台名："), formGroup), row, 0);
    m_editPlatformName = new QLineEdit(formGroup);
    formLayout->addWidget(m_editPlatformName, row, 1);
    formLayout->addWidget(new QLabel(QStringLiteral("进程名"), formGroup), row, 2);
    m_editProcessName = new QLineEdit(formGroup);
    formLayout->addWidget(m_editProcessName, row, 3);
    ++row;
    formLayout->addWidget(new QLabel(QStringLiteral("窗口标题"), formGroup), row, 0);
    m_editWindowTitle = new QLineEdit(formGroup);
    formLayout->addWidget(m_editWindowTitle, row, 1, 1, 3);
    ++row;
    formLayout->addWidget(new QLabel(QStringLiteral("窗口类名"), formGroup), row, 0);
    m_editClassName = new QLineEdit(formGroup);
    formLayout->addWidget(m_editClassName, row, 1);
    formLayout->addWidget(new QLabel(QStringLiteral("窗口句柄"), formGroup), row, 2);
    m_editHandle = new QLineEdit(formGroup);
    m_editHandle->setReadOnly(true);
    formLayout->addWidget(m_editHandle, row, 3);
    mainLayout->addWidget(formGroup);

    auto* buttonBox = new QDialogButtonBox(this);
    m_btnOk = buttonBox->addButton(QDialogButtonBox::Ok);
    m_btnCancel = buttonBox->addButton(QDialogButtonBox::Cancel);
    m_btnOk->setText(QStringLiteral("确定"));
    m_btnCancel->setText(QStringLiteral("取消"));
    mainLayout->addWidget(buttonBox);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &AddWindowDialog::onSearchTextChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AddWindowDialog::onWindowSelectionChanged);
    connect(m_btnRefresh, &QPushButton::clicked, this, &AddWindowDialog::onRefreshClicked);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &AddWindowDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &AddWindowDialog::reject);
}

void AddWindowDialog::onRefreshClicked()
{
    m_allWindows = Win32WindowHelper::enumTopLevelWindows();
    rebuildTable();
}

void AddWindowDialog::rebuildTable()
{
    applyFilter();
}

void AddWindowDialog::applyFilter()
{
    const QString keyword = m_searchEdit->text().trimmed();
    m_table->setRowCount(0);
    m_filteredIndexes.clear();

    for (int i = 0; i < m_allWindows.size(); ++i) {
        const WindowInfo& info = m_allWindows.at(i);
        if (!keyword.isEmpty()) {
            const bool match = info.windowTitle.contains(keyword, Qt::CaseInsensitive) ||
                               info.processName.contains(keyword, Qt::CaseInsensitive);
            if (!match) continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* check = new QCheckBox(this);
        check->setStyleSheet("margin-left: 8px;");
        auto* checkWrap = new QWidget(this);
        auto* checkLayout = new QHBoxLayout(checkWrap);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->addWidget(check);
        m_table->setCellWidget(row, 0, checkWrap);
        m_table->setItem(row, 1, new QTableWidgetItem(info.windowTitle));
        m_table->setItem(row, 2, new QTableWidgetItem(info.processName));
        m_table->setItem(row, 3, new QTableWidgetItem(info.className));
        m_table->setItem(row, 4, new QTableWidgetItem(info.handle == 0 ? QStringLiteral("-")
                                                                       : QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(info.handle), 16))));
        m_filteredIndexes.append(i);
    }

    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
        onWindowSelectionChanged();
    }
}

void AddWindowDialog::onSearchTextChanged()
{
    applyFilter();
}

void AddWindowDialog::onWindowSelectionChanged()
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    const QString title = m_table->item(row, 1)->text();
    const QString process = m_table->item(row, 2)->text();
    const QString cls = m_table->item(row, 3)->text();
    const QString handleStr = m_table->item(row, 4)->text();

    m_editPlatformName->setText(title);
    m_editProcessName->setText(process);
    m_editWindowTitle->setText(title);
    m_editClassName->setText(cls);
    m_editHandle->setText(handleStr);
}

QVector<WindowInfo> AddWindowDialog::selectedWindows() const
{
    QVector<WindowInfo> result;
    const int currentRow = m_table->currentRow();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* wrap = m_table->cellWidget(row, 0);
        if (!wrap) continue;
        auto* check = wrap->findChild<QCheckBox*>();
        if (!check || !check->isChecked()) continue;
        if (row < 0 || row >= m_filteredIndexes.size()) continue;
        int idx = m_filteredIndexes.at(row);
        if (idx < 0 || idx >= m_allWindows.size()) continue;
        WindowInfo info = m_allWindows.at(idx);
        info.platformName = m_table->item(row, 1)->text();
        info.processName = m_table->item(row, 2)->text();
        info.windowTitle = m_table->item(row, 1)->text();
        info.className = m_table->item(row, 3)->text();
        if (info.platformName.isEmpty()) info.platformName = info.windowTitle;
        if (row == currentRow) {
            info.platformName = m_editPlatformName->text();
            info.processName = m_editProcessName->text();
            info.windowTitle = m_editWindowTitle->text();
            info.className = m_editClassName->text();
        }
        result.append(info);
    }
    return result;
}

void AddWindowDialog::onOkClicked()
{
    QVector<WindowInfo> list = selectedWindows();
    if (list.isEmpty()) {
        int row = m_table->currentRow();
        if (row >= 0 && row < m_filteredIndexes.size()) {
            int idx = m_filteredIndexes.at(row);
            if (idx >= 0 && idx < m_allWindows.size()) {
                WindowInfo info = m_allWindows.at(idx);
                info.platformName = m_editPlatformName->text();
                info.processName = m_editProcessName->text();
                info.windowTitle = m_editWindowTitle->text();
                info.className = m_editClassName->text();
                if (info.platformName.isEmpty()) info.platformName = info.windowTitle;
                list.append(info);
            }
        }
    }
    if (list.isEmpty()) {
        return;
    }

    auto* mainWin = qobject_cast<MainWindow*>(parent());
    if (!mainWin) return;

    mainWin->startBatchAddWindows(list);
    accept();
}
