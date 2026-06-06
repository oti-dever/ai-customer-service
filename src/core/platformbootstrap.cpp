#include "platformbootstrap.h"

#include "conversationmanager.h"
#include "messagerouter.h"
#include "../services/platforms/qianniurp_adapter.h"
#include "../services/platforms/simplatformadapter.h"
#include "../services/platforms/wechatrp_adapter.h"

namespace {

void registerAdapter(MessageRouter* router, IPlatformAdapter* adapter)
{
    if (!router || !adapter)
        return;
    router->registerAdapter(adapter);
}

} // namespace

void PlatformBootstrap::initializeDefaultPlatforms(ConversationManager& manager)
{
    auto* router = new MessageRouter(&manager);
    manager.initialize(router);

    registerAdapter(router, new SimPlatformAdapter(&manager));
    registerAdapter(router, new QianniuRPAAdapter(&manager));
    registerAdapter(router, new WechatRPAAdapter(&manager));
}
