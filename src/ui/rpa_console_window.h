#ifndef RPA_CONSOLE_WINDOW_H
#define RPA_CONSOLE_WINDOW_H

#include <QDialog>

class QComboBox;
class QPlainTextEdit;
class MainWindow;

/**
 * 非模态窗口：按平台查看 RPA 子进程（python -m rpa.main）的控制台输出。
 * mainWindow 用于读日志；parent 宜为 nullptr，避免作为主窗子 QDialog 时始终叠在主窗之上。
 */
class RpaConsoleWindow : public QDialog
{
    Q_OBJECT
public:
    explicit RpaConsoleWindow(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onPlatformChanged(int index);
    void onOutputAppended(const QString& platformId, const QString& text);
    void onClearClicked();

private:
    void setupUi();
    void reloadCurrentLog();
    QString currentPlatformId() const;
    void appendToView(const QString& text);

    MainWindow* m_main = nullptr;
    QComboBox* m_combo = nullptr;
    QPlainTextEdit* m_log = nullptr;
};

#endif // RPA_CONSOLE_WINDOW_H
