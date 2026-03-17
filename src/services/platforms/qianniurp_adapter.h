#ifndef QIANNIURP_ADAPTER_H
#define QIANNIURP_ADAPTER_H

#include "iplatformadapter.h"

// 基于共享 SQLite 的千牛 RPA 适配器骨架。
// 当前版本仅做日志与占位，后续再接入 Python RPA / DB 轮询逻辑。
class QianniuRPAAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit QianniuRPAAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("qianniu"); }
    void connectPlatform() override;
    void disconnectPlatform() override;
    void startListening() override;
    void stopListening() override;
    void sendMessage(const QString& conversationId, const QString& text) override;
    bool isConnected() const override { return m_connected; }

private:
    bool m_connected = false;
};

#endif // QIANNIURP_ADAPTER_H

