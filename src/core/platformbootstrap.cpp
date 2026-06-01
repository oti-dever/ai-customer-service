#include "platformbootstrap.h"

#include "conversationmanager.h"
#include "messagerouter.h"
#include "../services/platforms/qianniurp_adapter.h"
#include "../services/platforms/simplatformadapter.h"
#include "../services/platforms/wechatrp_adapter.h"

namespace {

void registerAndStartAdapter(MessageRouter* router, IPlatformAdapter* adapter)
{
    if (!router || !adapter)
        return;
    router->registerAdapter(adapter);
    adapter->connectPlatform();
    adapter->startListening();
}

} // namespace

void PlatformBootstrap::initializeDefaultPlatforms(ConversationManager& manager)
{
    auto* router = new MessageRouter(&manager);
    manager.initialize(router);

    registerAndStartAdapter(router, new SimPlatformAdapter(&manager));
    registerAndStartAdapter(router, new QianniuRPAAdapter(&manager));
    registerAndStartAdapter(router, new WechatRPAAdapter(&manager));
}
