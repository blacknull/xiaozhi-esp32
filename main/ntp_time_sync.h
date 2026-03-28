#ifndef NTP_TIME_SYNC_H
#define NTP_TIME_SYNC_H

#include <string>
#include <functional>
#include <esp_sntp.h>

// NTP 服务器列表
static const char* NTP_SERVERS[] = {
    "pool.ntp.org",
    "ntp.aliyun.com", 
    "ntp.tencent.com",
    nullptr
};

class NtpTimeSync {
public:
    // 获取单例实例
    static NtpTimeSync& GetInstance();
    
    // 初始化 SNTP（不立即同步，等待网络就绪后调用 SyncTime）
    void Initialize();
    
    // 同步时间（阻塞方式，带超时）
    // @param timezone_offset_hours: 时区偏移（小时），默认东8区=8
    // @param timeout_ms: 超时时间（毫秒），默认10秒
    // @return: 是否同步成功
    bool SyncTime(int timezone_offset_hours = 8, int timeout_ms = 10000);
    
    // 异步同步时间（非阻塞，通过回调通知结果）
    void SyncTimeAsync(int timezone_offset_hours = 8, 
                       std::function<void(bool success, const std::string& time_str)> callback = nullptr);
    
    // 获取当前时间字符串（本地时间，带时区）
    // @param format: 时间格式，默认 "%Y-%m-%d %H:%M:%S"
    std::string GetLocalTimeString(const char* format = "%Y-%m-%d %H:%M:%S");
    
    // 获取当前时间戳（秒）
    time_t GetTimestamp();
    
    // 检查时间是否已同步
    bool IsTimeSynced();
    
    // 设置时区
    void SetTimezone(int timezone_offset_hours);
    
    // 获取当前时区偏移（小时）
    int GetTimezoneOffset() const { return current_timezone_; }
    
private:
    NtpTimeSync() = default;
    ~NtpTimeSync() = default;
    NtpTimeSync(const NtpTimeSync&) = delete;
    NtpTimeSync& operator=(const NtpTimeSync&) = delete;
    
    bool initialized_ = false;
    bool time_synced_ = false;
    int current_timezone_ = 8;  // 默认东8区
    
    // 尝试从指定服务器同步
    bool TrySyncFromServer(const char* server, int timezone_offset_hours, int timeout_ms);
    
    // 等待时间同步完成
    bool WaitForSync(int timeout_ms);
};

#endif // NTP_TIME_SYNC_H
