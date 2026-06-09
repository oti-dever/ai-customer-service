#ifndef PYTHONSERVICECONNECTIONDIALOG_H
#define PYTHONSERVICECONNECTIONDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;
class QCheckBox;
class MainWindow;

/**
 * Python 服务端连接设置。
 */
class PythonServiceConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PythonServiceConnectionDialog(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onSaveServiceClicked();
    void onTestServiceClicked();

private:
    void setupUI();

    MainWindow* m_main = nullptr;
    QLineEdit* m_serviceEndpointEdit = nullptr;
    QCheckBox* m_startupBackfillCheck = nullptr;
    QPushButton* m_btnSaveService = nullptr;
    QPushButton* m_btnTestService = nullptr;
    QPushButton* m_btnClose = nullptr;
};

#endif // PYTHONSERVICECONNECTIONDIALOG_H
