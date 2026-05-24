#ifndef PLATFORMBOOTSTRAP_H
#define PLATFORMBOOTSTRAP_H

class ConversationManager;

class PlatformBootstrap
{
public:
    static void initializeDefaultPlatforms(ConversationManager& manager);
};

#endif // PLATFORMBOOTSTRAP_H
