#ifndef AIPROVIDERCONFIGPAGE_H
#define AIPROVIDERCONFIGPAGE_H

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class AiChatAppService;

/** 「AI 客服后台」中的「API 配置/模型」页，读写 QSettings 与机器人对话页中的线路/预设一致。 */
class AiProviderConfigPage : public QWidget
{
    Q_OBJECT
public:
    explicit AiProviderConfigPage(QWidget* parent = nullptr);

signals:
    void settingsSaved();

public slots:
    /** 在磁盘被外部更新后重载当前预设下的表单（可选用于将来扩展）。 */
    void reloadCurrentPreset();

private slots:
    void onSaveSettings();
    void onTestConnection();
    void onPresetIndexChanged(int index);

private:
    void fillPresetCombo();
    void migrateLegacyAiSettingsToPresets();
    void loadEditsFromPreset(const QString& sessionModelKey);
    void saveEditsToPreset(const QString& sessionModelKey);
    QString sessionModelKeyAtComboIndex(int index) const;

    QComboBox* m_presetCombo = nullptr;
    QLineEdit* m_baseUrlEdit = nullptr;
    QLineEdit* m_modelEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_testBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    AiChatAppService* m_aiChatService = nullptr;

    QString m_activePresetSessionKey;
    bool m_fillingCombo = false;
};

#endif
