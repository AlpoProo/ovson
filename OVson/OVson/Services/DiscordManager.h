#include <unordered_map>
#include <vector>
#include <functional>

namespace Services {
#pragma pack(push, 8)
    struct DiscordUser {
        const char* userId;
        const char* username;
        const char* discriminator;
        const char* avatar;
    };

    struct DiscordEventHandlers {
        void (*ready)(const DiscordUser* request);
        void (*disconnected)(int errorCode, const char* message);
        void (*errored)(int errorCode, const char* message);
        void (*joinGame)(const char* joinSecret);
        void (*spectateGame)(const char* spectateSecret);
        void (*joinRequest)(const DiscordUser* request);
    };

    struct DiscordRichPresence {
        const char* state;
        const char* details;
        long long startTimestamp;
        long long endTimestamp;
        const char* largeImageKey;
        const char* largeImageText;
        const char* smallImageKey;
        const char* smallImageText;
        const char* partyId;
        int partySize;
        int partyMax;
        const char* matchSecret;
        const char* joinSecret;
        const char* spectateSecret;
        signed char instance;
    };
#pragma pack(pop)

    class DiscordManager {
    public:
        static DiscordManager* getInstance();
        void init();
        void update();
        void shutdown();

        void onReady(const DiscordUser* user);
        void onDisconnected(int errorCode, const char* message);
        void onError(int errorCode, const char* message);

    private:
        DiscordManager() = default;
        bool m_initialized = false;
        bool m_connected = false;
        long long m_startTime = 0;
        long long m_lastInitTime = 0;
        
        DiscordEventHandlers m_handlers{};
        void* m_discordDll = nullptr;
    };
}
