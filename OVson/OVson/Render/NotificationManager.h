#pragma once
#include <string>
#include <vector>
#include <Windows.h>
#include <mutex>

namespace Render {
    enum class NotificationType {
        Info,
        Success,
        Warning,
        Error
    };

    struct Notification {
        std::string title;
        std::string message;
        NotificationType type;
        float timer;
        float duration;
        float slideAnim;
        
        DWORD getTitleColor() const;
        DWORD getBodyColor() const;
    };

    class NotificationManager {
    public:
        static NotificationManager* getInstance();

        void add(const std::string& title, const std::string& message, NotificationType type = NotificationType::Info, float duration = 3.0f);
        void render(HDC hdc);

    private:
        NotificationManager() = default;
        std::vector<Notification> m_notifications;
        std::mutex m_mutex;
        
        bool m_fontInit = false;
    };
}
