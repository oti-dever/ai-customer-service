#ifndef IPCSERVICE_H
#define IPCSERVICE_H

#include "ipctypes.h"
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QWebSocket;
class QWebSocketServer;

namespace Ipc {

class IpcService : public QObject
{
    Q_OBJECT
public:
    static IpcService& instance();

    void initialize();
    void shutdown();

    bool isServiceAvailable() const { return m_serviceAvailable; }
    QString serviceEndpoint() const { return m_endpoint; }
    void setServiceEndpoint(const QString& endpoint);
    void markServiceUnavailable();
    void loadConnectionSettings();
    void saveConnectionSettings() const;
    bool connectToConfiguredService(QString* errorOut = nullptr);
    QString eventWebSocketUrl() const;
    bool ensureServiceAvailable(QString* errorOut = nullptr);

    QString requestAiSuggestion(const AiSuggestionRequest& request);
    void cancelRequest(const QString& requestId);

    HealthCheckResponse checkHealth();
    QJsonObject fetchPlatformStatuses(int timeoutMs = 3000,
                                      ResponseStatus* statusOut = nullptr,
                                      QString* errorOut = nullptr);
    QJsonObject fetchCacheSnapshot(const QString& platform = QString(),
                                   int conversationLimit = 100,
                                   int messageLimit = 200,
                                   const QString& cursor = QString(),
                                   int timeoutMs = 5000,
                                   ResponseStatus* statusOut = nullptr,
                                   QString* errorOut = nullptr);
    QJsonObject fetchConversationList(const QString& platform = QString(),
                                      int conversationLimit = 100,
                                      int timeoutMs = 5000,
                                      ResponseStatus* statusOut = nullptr,
                                      QString* errorOut = nullptr);
    QJsonObject fetchConversationMessages(const QString& platform,
                                          const QString& conversationKey,
                                          int messageLimit = 300,
                                          int timeoutMs = 5000,
                                          ResponseStatus* statusOut = nullptr,
                                          QString* errorOut = nullptr);
    QJsonObject fetchKnowledgeBases(int timeoutMs = 5000,
                                    ResponseStatus* statusOut = nullptr,
                                    QString* errorOut = nullptr);
    QJsonObject createKnowledgeBase(const QJsonObject& payload,
                                    int timeoutMs = 5000,
                                    ResponseStatus* statusOut = nullptr,
                                    QString* errorOut = nullptr);
    QJsonObject updateKnowledgeBase(const QJsonObject& payload,
                                    int timeoutMs = 5000,
                                    ResponseStatus* statusOut = nullptr,
                                    QString* errorOut = nullptr);
    QJsonObject setKnowledgeBaseEnabled(const QString& baseId,
                                        bool enabled,
                                        int timeoutMs = 5000,
                                        ResponseStatus* statusOut = nullptr,
                                        QString* errorOut = nullptr);
    QJsonObject deleteKnowledgeBase(const QString& baseId,
                                    int timeoutMs = 5000,
                                    ResponseStatus* statusOut = nullptr,
                                    QString* errorOut = nullptr);
    QJsonObject fetchKnowledgePlatformBindings(const QString& platform,
                                               int timeoutMs = 5000,
                                               ResponseStatus* statusOut = nullptr,
                                               QString* errorOut = nullptr);
    QJsonObject saveKnowledgePlatformBindings(const QString& platform,
                                              const QStringList& baseIds,
                                              int timeoutMs = 5000,
                                              ResponseStatus* statusOut = nullptr,
                                              QString* errorOut = nullptr);
    QJsonObject importKnowledgeDirectory(const QString& directory,
                                         const QString& baseName = QStringLiteral("键盘键帽客服知识库PoC"),
                                         const QString& shopId = QStringLiteral("keyboard-demo-shop"),
                                         const QString& scene = QStringLiteral("reply_draft"),
                                         int timeoutMs = 120000,
                                         bool asyncTask = false,
                                         ResponseStatus* statusOut = nullptr,
                                         QString* errorOut = nullptr);
    QJsonObject importKnowledgeDirectoryForBase(const QString& directory,
                                                const QString& baseId,
                                                const QString& baseName = QStringLiteral("键盘键帽客服知识库PoC"),
                                                const QString& applicableShops = QString(),
                                                const QString& scene = QStringLiteral("reply_draft"),
                                                int timeoutMs = 120000,
                                                bool asyncTask = false,
                                                ResponseStatus* statusOut = nullptr,
                                                QString* errorOut = nullptr);
    QJsonObject fetchKnowledgeImportTask(const QString& taskId,
                                         int timeoutMs = 120000,
                                         ResponseStatus* statusOut = nullptr,
                                         QString* errorOut = nullptr);
    QJsonObject fetchKnowledgeDocuments(const QString& baseId = QString(),
                                        int timeoutMs = 5000,
                                        ResponseStatus* statusOut = nullptr,
                                        QString* errorOut = nullptr);
    QJsonObject fetchKnowledgeImages(const QString& baseId = QString(),
                                     int timeoutMs = 5000,
                                     ResponseStatus* statusOut = nullptr,
                                     QString* errorOut = nullptr);
    QJsonObject searchKnowledgeImages(const QString& query,
                                      const QString& platform = QString(),
                                      const QString& shopId = QStringLiteral("keyboard-demo-shop"),
                                      const QString& scene = QStringLiteral("reply_draft"),
                                      int topK = 3,
                                      int timeoutMs = 15000,
                                      ResponseStatus* statusOut = nullptr,
                                      QString* errorOut = nullptr,
                                      const QStringList& baseIds = QStringList());
    QJsonObject searchKnowledge(const QString& query,
                                const QString& platform = QString(),
                                const QString& shopId = QStringLiteral("keyboard-demo-shop"),
                                const QString& scene = QStringLiteral("reply_draft"),
                                int topK = 3,
                                int timeoutMs = 2500,
                                ResponseStatus* statusOut = nullptr,
                                QString* errorOut = nullptr,
                                const QStringList& baseIds = QStringList());
    QJsonObject fetchPlatformReplay(const QString& platform = QString(),
                                    const QString& cursor = QString(),
                                    int limit = 100,
                                    int timeoutMs = 5000,
                                    ResponseStatus* statusOut = nullptr,
                                    QString* errorOut = nullptr);
    QJsonObject fetchRpaReplay(const QString& platform = QString(),
                               const QString& cursor = QString(),
                               int limit = 100,
                               int timeoutMs = 5000,
                               ResponseStatus* statusOut = nullptr,
                               QString* errorOut = nullptr);
    QJsonObject clearConversationMessages(const QString& platform,
                                          const QString& accountId,
                                          const QString& conversationKey,
                                          int timeoutMs = 5000,
                                          ResponseStatus* statusOut = nullptr,
                                          QString* errorOut = nullptr);
    QJsonObject deleteConversationOnService(const QString& platform,
                                            const QString& accountId,
                                            const QString& conversationKey,
                                            int timeoutMs = 5000,
                                            ResponseStatus* statusOut = nullptr,
                                            QString* errorOut = nullptr);
    bool dispatchPlatformEvent(const QJsonObject& event, bool replayed = false);
    int dispatchPlatformReplayEvents(const QJsonObject& replay);
    int dispatchRpaReplayEvents(const QJsonObject& replay);
    PlatformCommandResponse sendPlatformCommandViaWebSocket(const PlatformCommandRequest& request,
                                                            int timeoutMs = 3000);
    RpaCommandResponse sendRpaCommandViaWebSocket(const RpaCommandRequest& request,
                                                  int timeoutMs = 3000);

signals:
    void aiSuggestionReceived(const AiSuggestionResponse& response);
    void requestFailed(const QString& requestId, const QString& reason);
    void serviceStatusChanged(bool available);
    void platformEventReceived(const QJsonObject& event);
    void rpaEventReceived(const QJsonObject& event);
    void platformEventBridgeStateChanged(bool connected);
    void rpaEventBridgeStateChanged(bool connected);

private slots:
    void onRequestFinished(QNetworkReply* reply);
    void onRequestTimeout();
    void onEventSocketTextMessageReceived(const QString& message);
    void onEventSocketDisconnected();
    void onCommandSocketTextMessageReceived(const QString& message);
    void onCommandSocketDisconnected();

private:
    explicit IpcService(QObject* parent = nullptr);
    ~IpcService();

    QJsonObject buildAiSuggestionPayload(const AiSuggestionRequest& request) const;
    QJsonObject buildPlatformCommandPayload(const PlatformCommandRequest& request) const;
    QJsonObject buildRpaCommandPayload(const RpaCommandRequest& request) const;
    QJsonObject performJsonGet(const QUrl& url, int timeoutMs,
                               ResponseStatus* statusOut,
                               QString* errorOut);
    QJsonObject performJsonPost(const QUrl& url, const QJsonObject& payload, int timeoutMs,
                                ResponseStatus* statusOut,
                                QString* errorOut);
    AiSuggestionResponse parseAiSuggestionResponse(const QJsonObject& json,
                                                    const QString& requestId) const;
    void appendServiceLog(const QByteArray& chunk);
    void startEventWebSocketServer();
    void stopEventWebSocketServer();
    void startCommandWebSocketClient();
    void stopCommandWebSocketClient();
    void handleEventSocketPayload(const QJsonObject& payload);
    QString normalizedEndpoint(const QString& endpoint) const;

    QNetworkAccessManager* m_network = nullptr;
    QWebSocketServer* m_eventServer = nullptr;
    QSet<QWebSocket*> m_eventSockets;
    QWebSocket* m_commandSocket = nullptr;
    QString m_endpoint;
    quint16 m_eventPort = 8766;
    quint16 m_commandPort = 8767;
    bool m_serviceAvailable = false;
    int m_defaultTimeoutMs = 30000;

    struct PendingRequest {
        QString requestId;
        RequestType type;
        QNetworkReply* reply = nullptr;
        QTimer* timer = nullptr;
        QDateTime startedAt;
    };
    QMap<QString, PendingRequest> m_pendingRequests;
};

} // namespace Ipc

#endif // IPCSERVICE_H
