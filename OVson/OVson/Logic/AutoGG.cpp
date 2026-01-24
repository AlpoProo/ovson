#include "AutoGG.h"
#include "../Config/Config.h"
#include "../Chat/ChatSDK.h"
#include "../Render/NotificationManager.h"
#include "../Render/RenderHook.h"
#include "../Utils/Logger.h"
#include <thread>
#include <chrono>

namespace Logic {
    void AutoGG::handleChat(const std::string& chat) {
        if (!Config::isAutoGGEnabled()) return;

        if (chat.find("1st Killer") != std::string::npos || 
            chat.find("2nd Killer") != std::string::npos || 
            chat.find("3rd Killer") != std::string::npos) 
        {
            static ULONGLONG lastGG = 0;
            if (GetTickCount64() - lastGG < 5000) return;
            lastGG = GetTickCount64();

            Logger::log(Config::DebugCategory::General, "AutoGG Triggered by chat");

            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                std::string msg = Config::getAutoGGMessage();
                if (msg.empty()) msg = "gg";
                
                std::string fullMsg = "/ac " + msg;
                
                Logger::log(Config::DebugCategory::General, "AutoGG enqueuing task: %s", fullMsg.c_str());

                RenderHook::enqueueTask([fullMsg]() {
                    Logger::log(Config::DebugCategory::General, "AutoGG executing task (main thread)");
                    if (!ChatSDK::sendClientChat(fullMsg)) {
                        Logger::log(Config::DebugCategory::General, "AutoGG failed to send chat (player might be null)");
                    }
                });
                
                Render::NotificationManager::getInstance()->add("AutoGG", "Message sent: " + fullMsg, Render::NotificationType::Success);
            }).detach();
        }
    }
}
