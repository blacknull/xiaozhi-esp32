#include "ntp_time_sync.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_sntp.h>
#include <time.h>
#include <cstring>

#define TAG "NTP"

NtpTimeSync& NtpTimeSync::GetInstance() {
    static NtpTimeSync instance;
    return instance;
}

void NtpTimeSync::Initialize() {
    if (initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing SNTP...");
    
    // 设置默认时区（东8区）
    SetTimezone(current_timezone_);
    
    initialized_ = true;
    time_synced_ = false;
    
    ESP_LOGI(TAG, "SNTP initialized, timezone: UTC%+d", current_timezone_);
}

bool NtpTimeSync::SyncTime(int timezone_offset_hours, int timeout_ms) {
    if (!initialized_) {
        Initialize();
    }
    
    // 更新时区
    if (timezone_offset_hours != current_timezone_) {
        SetTimezone(timezone_offset_hours);
    }
    
    ESP_LOGI(TAG, "Starting time sync with timezone UTC%+d...", timezone_offset_hours);
    
    // 依次尝试各个 NTP 服务器
    for (int i = 0; NTP_SERVERS[i] != nullptr; i++) {
        ESP_LOGI(TAG, "Trying NTP server: %s", NTP_SERVERS[i]);
        if (TrySyncFromServer(NTP_SERVERS[i], timezone_offset_hours, timeout_ms)) {
            ESP_LOGI(TAG, "Time synced successfully from %s", NTP_SERVERS[i]);
            time_synced_ = true;
            return true;
        }
        ESP_LOGW(TAG, "Failed to sync from %s, trying next...", NTP_SERVERS[i]);
    }
    
    ESP_LOGE(TAG, "All NTP servers failed");
    return false;
}

bool NtpTimeSync::TrySyncFromServer(const char* server, int timezone_offset_hours, int timeout_ms) {
    // 停止之前的 SNTP（如果运行中）
    esp_sntp_stop();
    
    // 配置 SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    
    // 等待同步完成
    bool result = WaitForSync(timeout_ms);
    
    // 停止 SNTP
    esp_sntp_stop();
    
    return result;
}

bool NtpTimeSync::WaitForSync(int timeout_ms) {
    int elapsed = 0;
    const int check_interval = 100;  // 每100ms检查一次
    
    while (elapsed < timeout_ms) {
        // 检查时间是否已同步（通过比较时间戳）
        time_t now = 0;
        struct tm timeinfo = {};
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // 如果时间年份大于2024，认为已同步
        if (timeinfo.tm_year > (2024 - 1900)) {
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    return false;
}

void NtpTimeSync::SyncTimeAsync(int timezone_offset_hours, 
                                 std::function<void(bool success, const std::string& time_str)> callback) {
    // 创建异步任务
    auto* params = new std::pair<int, std::function<void(bool, const std::string&)>>(
        timezone_offset_hours, callback);
    
    xTaskCreate([](void* arg) {
        auto* p = static_cast<std::pair<int, std::function<void(bool, const std::string&)>>*>(arg);
        int tz = p->first;
        auto cb = p->second;
        delete p;
        
        auto& ntp = NtpTimeSync::GetInstance();
        bool success = ntp.SyncTime(tz);
        std::string time_str = ntp.GetLocalTimeString();
        
        if (cb) {
            cb(success, time_str);
        }
        
        vTaskDelete(nullptr);
    }, "ntp_sync", 4096, params, 5, nullptr);
}

std::string NtpTimeSync::GetLocalTimeString(const char* format) {
    time_t now;
    struct tm timeinfo;
    char buffer[64];
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    strftime(buffer, sizeof(buffer), format, &timeinfo);
    return std::string(buffer);
}

time_t NtpTimeSync::GetTimestamp() {
    time_t now;
    time(&now);
    return now;
}

bool NtpTimeSync::IsTimeSynced() {
    return time_synced_;
}

void NtpTimeSync::SetTimezone(int timezone_offset_hours) {
    current_timezone_ = timezone_offset_hours;
    
    // 设置时区环境变量
    // POSIX 时区格式："UTC-8" 表示东8区（注意符号相反）
    char tz_str[32];
    if (timezone_offset_hours >= 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC-%d", timezone_offset_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC+%d", -timezone_offset_hours);
    }
    
    setenv("TZ", tz_str, 1);
    tzset();
    
    ESP_LOGI(TAG, "Timezone set to: %s (UTC%+d)", tz_str, timezone_offset_hours);
}
