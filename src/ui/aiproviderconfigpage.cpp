#include "aiproviderconfigpage.h"

#include "../services/app/aichatappservice.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../services/ai/aistreamingsession.h"
#include "../utils/applystyle.h"

#include "../services/ai/aitypes.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QColor>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVariantMap>
#include <QVBoxLayout>

namespace {

void showProviderConfigMessageBox(QMessageBox::Icon icon, QWidget* parent, const QString& title,
                                  const QString& text)
{
    QMessageBox box(icon, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(ApplyStyle::messageBoxContrastStyle());
    box.exec();
}

} // namespace

AiProviderConfigPage::AiProviderConfigPage(QWidget* parent)
    : QWidget(parent)
    , m_aiChatService(new AiChatAppService(this))
{
    setObjectName(QStringLiteral("aiProviderConfigPage"));
    setAutoFillBackground(true);
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(QStringLiteral("#F4F4F5")));
        setPalette(pal);
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 28, 32, 32);
    outer->setSpacing(16);

    auto* titleL = new QLabel(QStringLiteral("API 配置 / 模型"), this);
    titleL->setObjectName(QStringLiteral("aiProviderConfigTitle"));
    auto* subL = new QLabel(
        QStringLiteral("为各线路分别配置 Base URL、模型名称与 API Key，保存后内置 AI 助手与聚合能力将按此连接。"),
        this);
    subL->setObjectName(QStringLiteral("aiProviderConfigSubtitle"));
    subL->setWordWrap(true);
    outer->addWidget(titleL);
    outer->addWidget(subL);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("aiProviderConfigScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAttribute(Qt::WA_StyledBackground, true);
    if (QWidget* vp = scroll->viewport()) {
        vp->setObjectName(QStringLiteral("aiProviderConfigScrollViewport"));
        vp->setAutoFillBackground(true);
        QPalette vpPal = vp->palette();
        vpPal.setColor(QPalette::Window, QColor(QStringLiteral("#F4F4F5")));
        vp->setPalette(vpPal);
    }

    auto* formHost = new QWidget;
    formHost->setObjectName(QStringLiteral("aiProviderConfigForm"));
    formHost->setAutoFillBackground(true);
    {
        QPalette fPal = formHost->palette();
        fPal.setColor(QPalette::Window, QColor(QStringLiteral("#ffffff")));
        formHost->setPalette(fPal);
    }
    auto* setLay = new QVBoxLayout(formHost);
    setLay->setContentsMargins(20, 20, 20, 20);
    setLay->setSpacing(12);

    {
        auto* presetPick = new QWidget(formHost);
        auto* presetPickLay = new QVBoxLayout(presetPick);
        presetPickLay->setContentsMargins(0, 0, 0, 0);
        presetPickLay->setSpacing(4);
        auto* presetLab = new QLabel(QStringLiteral("线路 / 模型"), presetPick);
        presetLab->setObjectName(QStringLiteral("robotSettingsFieldLabel"));
        m_presetCombo = new QComboBox(presetPick);
        m_presetCombo->setObjectName(QStringLiteral("robotAssistantModelCombo"));
        m_presetCombo->setCursor(Qt::PointingHandCursor);
        presetPickLay->addWidget(presetLab);
        presetPickLay->addWidget(m_presetCombo);
        setLay->addWidget(presetPick);
    }

    auto addLabeledRow = [&](const QString& label, QLineEdit* edit) {
        auto* row = new QWidget(formHost);
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        auto* lab = new QLabel(label, row);
        lab->setObjectName(QStringLiteral("robotSettingsFieldLabel"));
        v->addWidget(lab);
        edit->setObjectName(QStringLiteral("robotSettingsField"));
        v->addWidget(edit);
        setLay->addWidget(row);
    };

    m_baseUrlEdit = new QLineEdit(formHost);
    m_baseUrlEdit->setPlaceholderText(QStringLiteral("https://api.deepseek.com"));
    addLabeledRow(QStringLiteral("API Base URL"), m_baseUrlEdit);

    m_modelEdit = new QLineEdit(formHost);
    m_modelEdit->setPlaceholderText(QStringLiteral("deepseek-chat"));
    addLabeledRow(QStringLiteral("模型名称"), m_modelEdit);

    m_apiKeyEdit = new QLineEdit(formHost);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("在 DeepSeek 开放平台创建的 API Key"));
    addLabeledRow(QStringLiteral("API Key"), m_apiKeyEdit);

    auto* settingsBtnRow = new QWidget(formHost);
    auto* settingsBtnLay = new QHBoxLayout(settingsBtnRow);
    settingsBtnLay->setContentsMargins(0, 0, 0, 0);
    settingsBtnLay->setSpacing(8);
    m_saveBtn = new QPushButton(QStringLiteral("保存"), settingsBtnRow);
    m_saveBtn->setObjectName(QStringLiteral("aiBackendPrimaryBtn"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_testBtn = new QPushButton(QStringLiteral("测试连接"), settingsBtnRow);
    m_testBtn->setObjectName(QStringLiteral("aiBackendSecondaryBtn"));
    m_testBtn->setCursor(Qt::PointingHandCursor);
    settingsBtnLay->addWidget(m_saveBtn);
    settingsBtnLay->addWidget(m_testBtn);
    settingsBtnLay->addStretch(1);
    setLay->addWidget(settingsBtnRow);

    m_statusLabel = new QLabel(formHost);
    m_statusLabel->setObjectName(QStringLiteral("aiProviderConfigStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    setLay->addWidget(m_statusLabel);

    auto* privacyLabel = new QLabel(
        QStringLiteral(
            "说明：为获得回答，您输入的内容会发送至所配置的 API 服务商（如 DeepSeek、火山方舟等）。\n"
            "各线路在「API 配置/模型」中分别保存 Base URL、模型名称与 API Key；请勿将 Key 告知他人或提交到公开代码库。\n"
            "本助手与微信、千牛等平台内的客户聊天无关，也不会自动向客户发送消息。"),
        formHost);
    privacyLabel->setWordWrap(true);
    privacyLabel->setObjectName(QStringLiteral("robotAssistantPrivacy"));
    setLay->addWidget(privacyLabel);
    setLay->addStretch(1);

    scroll->setWidget(formHost);
    outer->addWidget(scroll, 1);

    connect(m_saveBtn, &QPushButton::clicked, this, &AiProviderConfigPage::onSaveSettings);
    connect(m_testBtn, &QPushButton::clicked, this, &AiProviderConfigPage::onTestConnection);
    connect(m_presetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &AiProviderConfigPage::onPresetIndexChanged);

    migrateLegacyAiSettingsToPresets();
    fillPresetCombo();
    if (m_presetCombo->count() > 0) {
        m_activePresetSessionKey = sessionModelKeyAtComboIndex(0);
        m_fillingCombo = true;
        m_presetCombo->setCurrentIndex(0);
        m_fillingCombo = false;
        loadEditsFromPreset(m_activePresetSessionKey);
    }
}

void AiProviderConfigPage::reloadCurrentPreset()
{
    if (!m_activePresetSessionKey.isEmpty())
        loadEditsFromPreset(m_activePresetSessionKey);
}

void AiProviderConfigPage::fillPresetCombo()
{
    if (!m_presetCombo)
        return;
    m_presetCombo->clear();
    for (const AiPresetDefinition& def : aiPresetDefinitions()) {
        QVariantMap m;
        m.insert(QStringLiteral("sessionModelKey"), def.sessionModelKey);
        m.insert(QStringLiteral("available"), def.available);
        const int idx = m_presetCombo->count();
        m_presetCombo->addItem(def.label);
        m_presetCombo->setItemData(idx, m, Qt::UserRole);
    }
}

void AiProviderConfigPage::migrateLegacyAiSettingsToPresets()
{
    migrateLegacyAiSettingsToPreset(QStringLiteral("deepseek:deepseek-chat"));
}

void AiProviderConfigPage::loadEditsFromPreset(const QString& sessionModelKey)
{
    if (!m_baseUrlEdit || !m_modelEdit || !m_apiKeyEdit)
        return;

    const AiPresetDefinition def = aiPresetDefinition(sessionModelKey);
    const AiProviderConfig config = loadAiProviderConfig(sessionModelKey);

    m_baseUrlEdit->setText(config.baseUrl);
    m_modelEdit->setText(config.model);
    m_apiKeyEdit->setText(config.apiKey);

    m_baseUrlEdit->setPlaceholderText(def.defaultBaseUrl.isEmpty() ? QStringLiteral("https://…")
                                                                    : def.defaultBaseUrl);
    m_modelEdit->setPlaceholderText(
        sessionModelKey == QLatin1String("doubao:ark")
            ? QStringLiteral("接入点 ID（如 ep-…）或文档中的 Model ID")
            : (def.defaultModel.isEmpty() ? QStringLiteral("模型名称") : def.defaultModel));
    m_apiKeyEdit->setPlaceholderText(def.apiKeyPlaceholder.isEmpty() ? QStringLiteral("API Key")
                                                                    : def.apiKeyPlaceholder);
}

void AiProviderConfigPage::saveEditsToPreset(const QString& sessionModelKey)
{
    if (sessionModelKey.isEmpty() || !m_baseUrlEdit || !m_modelEdit || !m_apiKeyEdit)
        return;
    AiProviderConfig config;
    config.sessionModelKey = sessionModelKey;
    config.baseUrl = m_baseUrlEdit->text().trimmed();
    config.model = m_modelEdit->text().trimmed();
    config.apiKey = m_apiKeyEdit->text();
    config.capabilities = aiPresetDefinition(sessionModelKey).capabilities;
    saveAiProviderConfig(config);
}

QString AiProviderConfigPage::sessionModelKeyAtComboIndex(int index) const
{
    if (!m_presetCombo || index < 0 || index >= m_presetCombo->count())
        return {};
    return m_presetCombo->itemData(index, Qt::UserRole)
        .toMap()
        .value(QStringLiteral("sessionModelKey"))
        .toString();
}

void AiProviderConfigPage::onPresetIndexChanged(int index)
{
    if (m_fillingCombo)
        return;
    if (index < 0 || !m_presetCombo || index >= m_presetCombo->count())
        return;

    saveEditsToPreset(m_activePresetSessionKey);

    m_activePresetSessionKey = sessionModelKeyAtComboIndex(index);
    loadEditsFromPreset(m_activePresetSessionKey);
    m_statusLabel->setVisible(false);
}

void AiProviderConfigPage::onSaveSettings()
{
    saveEditsToPreset(m_activePresetSessionKey);
    m_statusLabel->setText(QStringLiteral("已保存。内置 AI 助手与对话将按最新配置连接。"));
    m_statusLabel->setVisible(true);
    emit settingsSaved();
}

void AiProviderConfigPage::onTestConnection()
{
    if (!m_aiChatService)
        return;
    const AiProviderConfig config =
        m_aiChatService->resolveProviderConfig(m_activePresetSessionKey, m_baseUrlEdit->text().trimmed(),
                                                m_modelEdit->text().trimmed(), m_apiKeyEdit->text());
    if (config.apiKey.trimmed().isEmpty()) {
        showProviderConfigMessageBox(QMessageBox::Warning, this, QStringLiteral("测试连接"),
                                     QStringLiteral("请先填写 API Key。"));
        return;
    }
    if (config.baseUrl.trimmed().isEmpty() || config.model.trimmed().isEmpty()) {
        showProviderConfigMessageBox(QMessageBox::Warning, this, QStringLiteral("测试连接"),
                                     QStringLiteral("请先填写 Base URL 与模型名称。"));
        return;
    }

    AiRequest request;
    request.turns.append(makeAiTextTurn(QStringLiteral("user"), QStringLiteral("ping")));
    request.stream = false;

    m_testBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在测试连接…"));
    m_statusLabel->setVisible(true);

    auto* testSession = m_aiChatService->createSession(config, request, this);
    const QPointer<AiProviderConfigPage> self(this);
    connect(testSession, &IAiStreamingSession::completed, this, [this, self, testSession]() {
        testSession->deleteLater();
        if (!self)
            return;
        m_testBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("连接成功。"));
        m_statusLabel->setVisible(true);
        showProviderConfigMessageBox(QMessageBox::Information, self.get(), QStringLiteral("测试连接"),
                                    QStringLiteral("连接成功。"));
    });
    connect(testSession, &IAiStreamingSession::failed, this, [this, self, testSession](const QString& reason) {
        testSession->deleteLater();
        if (!self)
            return;
        m_testBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("连接失败：%1").arg(reason));
        m_statusLabel->setVisible(true);
        showProviderConfigMessageBox(QMessageBox::Warning, self.get(), QStringLiteral("测试连接"),
                                    QStringLiteral("连接失败：\n%1").arg(reason));
    });
    testSession->start();
}
