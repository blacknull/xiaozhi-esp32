#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H

#include <esp_timer.h>
#include <string>
#include <vector>

class McpServer;

struct TimerConfig {
    int id;
    std::string message;

    enum Type { kRelative, kAbsolute };
    Type type;

    // For kRelative
    int64_t interval_ms;
    int repeat_total;
    int repeat_remaining;

    // For kAbsolute
    int64_t target_epoch_ms;

    // Runtime
    esp_timer_handle_t timer_handle = nullptr;
    bool active = false;
};

class TimerManager {
public:
    static TimerManager& GetInstance() {
        static TimerManager instance;
        return instance;
    }

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    void RegisterMcpTools(McpServer* server);

    // Create a relative/repeating timer. Returns timer ID or -1 on error.
    int CreateRelativeTimer(const std::string& message, int64_t interval_ms, int repeat_count);

    // Create an absolute timer (fires once at epoch ms). Returns timer ID or -1 on error.
    int CreateAbsoluteTimer(const std::string& message, int64_t target_epoch_ms);

    // Delete a timer by ID.
    bool DeleteTimer(int id);

    // List all active timers as JSON string.
    std::string ListTimers();

private:
    TimerManager() = default;
    ~TimerManager();

    static constexpr int kMaxTimers = 8;
    int next_id_ = 1;
    std::vector<TimerConfig> timers_;

    void OnTimerFired(int timer_id);
    void TriggerAutoConversation(const std::string& message);
    void StartEspTimer(TimerConfig& config);
    void StopAndCleanTimer(TimerConfig& config);
};

#endif // TIMER_MANAGER_H
