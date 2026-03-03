#ifndef ADDWINDOWDIALOG_H
#define ADDWINDOWDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include "../utils/win32windowhelper.h"

class AddWindowDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AddWindowDialog(QWidget* parent = nullptr);
    WindowInfo selectedWindow() const;
private:
    void setupUI();
    void rebuildTable();
    void applyFilter();

private slots:
    void onSearchTextChanged();
    void onRefreshClicked();
    void onWindowSelectionChanged();

private:
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnRefresh = nullptr;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_editPlatformName = nullptr;
    QLineEdit* m_editProcessName = nullptr;
    QLineEdit* m_editWindowTitle = nullptr;
    QLineEdit* m_editClassName = nullptr;
    QLineEdit* m_editHandle = nullptr;
    QPushButton* m_btnOk = nullptr;
    QPushButton* m_btnCancel = nullptr;
    QVector<WindowInfo> m_allWindows;
    QVector<int> m_filteredIndexes;
};

#endif // ADDWINDOWDIALOG_H
