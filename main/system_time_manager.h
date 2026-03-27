#ifndef SYSTEM_TIME_MANAGER_H
#define SYSTEM_TIME_MANAGER_H

#include <string>
#include <functional>
#include <ctime>
#include <sys/time.h>
#include <esp_sntp.h>

class McpServer;

class SystemTimeManager {
public:
    static SystemTimeManager& GetInstance() {
        static SystemTimeManager instance;
        return instance;
    }

    SystemTimeManager(const SystemTimeManager&) = delete;
    SystemTimeManager& operator=(const SystemTimeManager&) = delete;

    // Initialize SNTP service
    void Initialize();

    // Register MCP tools
    void RegisterMcpTools(McpServer* server);

    // Synchronize time from NTP servers (blocking call)
    // Returns true if successful, false otherwise
    bool SyncTime(int timezone_offset = 8);

    // Synchronize time asynchronously (non-blocking)
    void SyncTimeAsync(int timezone_offset = 8);

    // Check if time has been synchronized
    bool IsTimeSynced() const { return time_synced_; }

    // Mark time as synchronized (used by SNTP callback)
    void MarkTimeSynced() { time_synced_ = true; }

    // Get current time as formatted string
    std::string GetCurrentTimeString() const;

    // Get current timestamp (seconds since epoch)
    time_t GetCurrentTimestamp() const;

    // Set timezone offset (hours from UTC, e.g., 8 for Beijing, -5 for New York)
    void SetTimezone(int offset_hours);

    // Get timezone offset
    int GetTimezone() const { return timezone_offset_; }

private:
    SystemTimeManager() = default;
    ~SystemTimeManager() = default;


    static void SntpTask(void* param);

    static constexpr const char* NTP_SERVERS[] = {
        "pool.ntp.org",
        "ntp.aliyun.com",
        "ntp.tencent.com"
    };
    static constexpr int kNtpServerCount = 3;
    static constexpr int kSntpTimeoutMs = 10000; // 10 seconds timeout per server
    static constexpr int kMaxRetries = 3;

    bool initialized_ = false;
    bool time_synced_ = false;
    int timezone_offset_ = 8; // Default to UTC+8 (Beijing)
    std::function<void(bool)> sync_callback_ = nullptr;
};

#endif // SYSTEM_TIME_MANAGER_H
