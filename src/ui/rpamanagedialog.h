#ifndef RPAMANAGEDIALOG_H
#define RPAMANAGEDIALOG_H

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QShowEvent>

class QCheckBox;
class QPushButton;
class MainWindow;

/**
 * 简化版「管理启动/停止 RPA」：多选平台，启动 / 停止 / 关闭。
 * 样式与「添加新窗口」对话框一致（ApplyStyle::addWindowDialogStyle）。
 */
class RpaManageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RpaManageDialog(MainWindow* mainWindow, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onSelectAllClicked();
    void onDeselectAllClicked();
    void onWechatCalibrateClicked();
    void onQianniuCalibrateClicked();
    void onStartClicked();
    void onStopClicked();
    void onCheckboxChanged();

private:
    void setupUI();
    void syncCheckboxesFromRunning();
    void updateButtonStates();
    QStringList checkedPlatformIds() const;

    MainWindow* m_main = nullptr;
    QHash<QString, QCheckBox*> m_checks;
    QPushButton* m_btnWechatCalibrate = nullptr;
    QPushButton* m_btnQianniuCalibrate = nullptr;
    QPushButton* m_btnStart = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnClose = nullptr;
};

#endif // RPAMANAGEDIALOG_H
