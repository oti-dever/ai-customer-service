#include "simplatformadapter.h"
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <QUuid>
#include <iterator>

SimPlatformAdapter::SimPlatformAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    m_sampleMessages = {
        QStringLiteral("你好，请问这个商品还有货吗？"),
        QStringLiteral("发货几天能到呢？"),
        QStringLiteral("可以便宜一点吗？"),
        QStringLiteral("有没有优惠券？"),
        QStringLiteral("尺码偏大还是偏小？"),
        QStringLiteral("支持七天无理由退换吗？"),
        QStringLiteral("这个颜色还有其他款式吗？"),
        QStringLiteral("我之前买过，质量不错，再买一件"),
        QStringLiteral("包邮吗？"),
        QStringLiteral("请问有赠品吗？"),
    };
    m_sampleNames = {
        QStringLiteral("测试买家A"),
        QStringLiteral("淘宝用户_小明"),
        QStringLiteral("开心购物666"),
        QStringLiteral("爱买买买"),
        QStringLiteral("阳光少年"),
    };
}

void SimPlatformAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[SimPlatform] 模拟平台已连接";
    emit connectionStateChanged(true);
}

void SimPlatformAdapter::disconnectPlatform()
{
    m_connected = false;
    qInfo() << "[SimPlatform] 模拟平台已断开";
    emit connectionStateChanged(false);
}

void SimPlatformAdapter::startListening()
{
    qInfo() << "[SimPlatform] 开始监听（模拟模式，等待手动触发）";
}

void SimPlatformAdapter::stopListening()
{
    qInfo() << "[SimPlatform] 停止监听";
}

void SimPlatformAdapter::sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    qInfo() << "[SimPlatform] 发送消息到会话" << conversationId << ":" << text.left(50);
    const QString normalized = text.trimmed().toLower();
    if (normalized.contains(QLatin1String("/fail")) || normalized.contains(QLatin1String("#fail"))) {
        emit sendFailed(conversationId, QStringLiteral("模拟平台发送失败：检测到 /fail 测试指令"), clientMessageId);
        return;
    }
    emit messageSent(conversationId, text, clientMessageId);
}

void SimPlatformAdapter::simulateIncomingMessage(const QString& buyerName, const QString& text)
{
    QString name = buyerName;
    QString content = text;

    if (name.isEmpty()) {
        int idx = QRandomGenerator::global()->bounded(m_sampleNames.size());
        name = m_sampleNames[idx];
    }
    if (content.isEmpty()) {
        int idx = QRandomGenerator::global()->bounded(m_sampleMessages.size());
        content = m_sampleMessages[idx];
    }

    PlatformMessage msg;
    msg.platform = platformName();
    msg.platformConversationId = QStringLiteral("sim_") + name;
    msg.customerName = name;
    msg.content = content;
    msg.direction = QStringLiteral("in");
    msg.sender = QStringLiteral("customer");
    msg.createdAt = QDateTime::currentDateTime();
    msg.platformMsgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    msg.sourceType = QStringLiteral("mock");
    msg.confidence = 100;
    msg.verificationStatus = QStringLiteral("manual_verified");
    msg.contentType = QStringLiteral("text");

    qInfo() << "[SimPlatform] 模拟收到消息: [" << name << "]" << content.left(30);
    emit incomingMessage(msg);
}

void SimPlatformAdapter::simulateRandomPlatformIncomingMessage()
{
    struct Scenario {
        const char* platformLabel;
        const char* buyerPrefix;
        const char* message;
    };
    static const Scenario kScenarios[] = {
        {"千牛", "淘宝买家", "这件商品今天能发货吗？"},
        {"拼多多", "拼多多买家", "下单后大概多久可以收到？"},
        {"微信", "微信客户", "你好，我想咨询一下售后怎么处理。"},
        {"抖店", "抖店买家", "这个规格还有库存吗？"},
    };

    const int idx = QRandomGenerator::global()->bounded(int(std::size(kScenarios)));
    const Scenario& scenario = kScenarios[idx];
    const QString buyerName = QStringLiteral("【%1】%2%3")
                                  .arg(QString::fromUtf8(scenario.platformLabel),
                                       QString::fromUtf8(scenario.buyerPrefix))
                                  .arg(m_nextBuyerId++);
    const QString message = QString::fromUtf8(scenario.message);
    simulateIncomingMessage(buyerName, message);
}
