#include "system_time_manager.h"
#include "mcp_server.h"
#include "application.h"

#include <esp_log.h>
#include <esp_sntp.h>
#include <cJSON.h>
#include <ctime>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "SysTime"

// C-compatible callback for SNTP sync notification
extern "C" void SntpSyncCallback(struct timeval *tv);

void SystemTimeManager::Initialize() {
    if (initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Initializing SNTP service");
    
    // Set default timezone
    SetTimezone(timezone_offset_);
    
    initialized_ = true;
}

void SystemTimeManager::SetTimezone(int offset_hours) {
    timezone_offset_ = offset_hours;
    
    // Set timezone environment variable
    // Format: TZ=UTC-8 for UTC+8 (Beijing)
    // Note: POSIX timezone has inverted sign
    char tz_str[32];
    if (offset_hours >= 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC-%d", offset_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC+%d", -offset_hours);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    
    ESP_LOGI(TAG, "Timezone set to UTC%+d (%s)", offset_hours, tz_str);
}

// SNTP sync callback implementation
void SntpSyncCallback(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized");
    SystemTimeManager::GetInstance().MarkTimeSynced();
}

bool SystemTimeManager::SyncTime(int timezone_offset) {
    if (!initialized_) {
        Initialize();
    }

    // Update timezone if changed
    if (timezone_offset != timezone_offset_) {
        SetTimezone(timezone_offset);
    }

    ESP_LOGI(TAG, "Starting NTP sync with timezone UTC%+d...", timezone_offset_);
    time_synced_ = false;

    // Try each NTP server
    for (int retry = 0; retry < kMaxRetries; retry++) {
        for (int i = 0; i < kNtpServerCount; i++) {
            const char* server = NTP_SERVERS[i];
            ESP_LOGI(TAG, "Trying NTP server: %s (attempt %d/%d)", server, retry + 1, kMaxRetries);

            // Initialize SNTP
            esp_sntp_stop();
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, server);
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_set_time_sync_notification_cb(SntpSyncCallback);
            esp_sntp_init();

            // Wait for sync with timeout
            int wait_ms = 0;
            while (!time_synced_ && wait_ms < kSntpTimeoutMs) {
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_ms += 100;
            }

            if (time_synced_) {
                // Get and log the synchronized time
                time_t now = time(nullptr);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                ESP_LOGI(TAG, "Time synchronized successfully: %s (from %s)", buf, server);
                esp_sntp_stop();
                return true;
            }

            ESP_LOGW(TAG, "NTP sync timeout from %s", server);
            esp_sntp_stop();
        }
    }

    ESP_LOGE(TAG, "Failed to sync time from all NTP servers after %d retries", kMaxRetries);
    return false;
}

void SystemTimeManager::SntpTask(void* param) {
    auto* manager = static_cast<SystemTimeManager*>(param);
    
    // Use the current timezone setting from the manager instance
    bool success = manager->SyncTime(manager->timezone_offset_);
    
    if (success) {
        ESP_LOGI(TAG, "Background NTP sync completed successfully");
    } else {
        ESP_LOGE(TAG, "Background NTP sync failed");
    }
    
    vTaskDelete(nullptr);
}

void SystemTimeManager::SyncTimeAsync(int timezone_offset) {
    if (!initialized_) {
        Initialize();
    }

    ESP_LOGI(TAG, "Starting async NTP sync...");
    
    // Create a task to do the sync in background
    // Pass timezone as a simple value through a static variable or use the current setting
    if (timezone_offset != timezone_offset_) {
        timezone_offset_ = timezone_offset;
    }
    
    xTaskCreate(SntpTask, "sntp_sync", 4096, this, 3, nullptr);
}

std::string SystemTimeManager::GetCurrentTimeString() const {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Format time string with timezone
    char result[128];
    snprintf(result, sizeof(result), "%04d-%02d-%02d %02d:%02d:%02d (UTC%+d)",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timezone_offset_);
    
    return std::string(result);
}

time_t SystemTimeManager::GetCurrentTimestamp() const {
    return time(nullptr);
}

void SystemTimeManager::RegisterMcpTools(McpServer* server) {
    // Tool: sync_time - Synchronize time from NTP servers
    server->AddTool("self.time.sync",
        "Synchronize system time from NTP servers. Tries multiple servers (pool.ntp.org, ntp.aliyun.com, ntp.tencent.com) automatically if one fails."
        "Returns the synchronized time or error message.",
        PropertyList({
            Property("timezone_offset", kPropertyTypeInteger, 8, -12, 14)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& mgr = SystemTimeManager::GetInstance();
            int tz_offset = properties["timezone_offset"].value<int>();
            
            // Do sync in background to avoid blocking
            mgr.SyncTimeAsync(tz_offset);
            
            // Return immediately with status
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddStringToObject(result, "message", "Time synchronization started in background");
            cJSON_AddNumberToObject(result, "timezone", tz_offset);
            return result;
        });

    // Tool: get_time - Get current system time
    server->AddTool("self.time.get",
        "Get the current system time. Returns the local time based on configured timezone.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& mgr = SystemTimeManager::GetInstance();
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "synced", mgr.IsTimeSynced());
            cJSON_AddNumberToObject(result, "timezone", mgr.GetTimezone());
            cJSON_AddStringToObject(result, "local_time", mgr.GetCurrentTimeString().c_str());
            cJSON_AddNumberToObject(result, "timestamp", (double)mgr.GetCurrentTimestamp());
            return result;
        });

    // Tool: set_timezone - Set timezone offset
    server->AddTool("self.time.set_timezone",
        "Set the system timezone offset from UTC. This affects how local time is displayed."
        "Examples: 8 for Beijing (UTC+8), -5 for New York (UTC-5), 0 for London.",
        PropertyList({
            Property("offset", kPropertyTypeInteger, 8, -12, 14)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& mgr = SystemTimeManager::GetInstance();
            int offset = properties["offset"].value<int>();
            
            mgr.SetTimezone(offset);
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddNumberToObject(result, "timezone", offset);
            cJSON_AddStringToObject(result, "message", "Timezone updated successfully");
            cJSON_AddStringToObject(result, "current_time", mgr.GetCurrentTimeString().c_str());
            return result;
        });
}
