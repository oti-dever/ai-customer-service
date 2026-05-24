#ifndef AISERVICEFACADE_H
#define AISERVICEFACADE_H

#include "aitypes.h"

#include <QObject>

class QNetworkAccessManager;
class IAiStreamingSession;

class AiServiceFacade : public QObject
{
    Q_OBJECT
public:
    explicit AiServiceFacade(QNetworkAccessManager* nam, QObject* parent = nullptr);

    IAiStreamingSession* createSession(const AiProviderConfig& config,
                                       const AiRequest& request,
                                       QObject* parent = nullptr) const;

private:
    QNetworkAccessManager* m_nam = nullptr;
};

#endif // AISERVICEFACADE_H
