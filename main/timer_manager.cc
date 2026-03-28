#include "timer_manager.h"
#include "mcp_server.h"
#include "application.h"

#include <esp_log.h>
#include <cJSON.h>
#include <ctime>
#include <cstring>
#include <algorithm>

#define TAG "TimerMgr"

TimerManager::~TimerManager() {
    for (auto& t : timers_) {
        StopAndCleanTimer(t);
    }
    timers_.clear();
}

void TimerManager::StopAndCleanTimer(TimerConfig& config) {
    if (config.timer_handle) {
        esp_timer_stop(config.timer_handle);
        esp_timer_delete(config.timer_handle);
        config.timer_handle = nullptr;
    }
    config.active = false;
}

void TimerManager::StartEspTimer(TimerConfig& config) {
    int timer_id = config.id;

    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            int id = (int)(intptr_t)arg;
            TimerManager::GetInstance().OnTimerFired(id);
        },
        .arg = (void*)(intptr_t)timer_id,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mcp_timer",
        .skip_unhandled_events = false,
    };

    esp_timer_create(&args, &config.timer_handle);

    if (config.type == TimerConfig::kRelative) {
        esp_timer_start_once(config.timer_handle, config.interval_ms * 1000);
    } else if (config.type == TimerConfig::kMusicCheck) {
        // Music check timer: use interval_ms as periodic interval
        esp_timer_start_once(config.timer_handle, config.interval_ms * 1000);
    } else {
        // Absolute: compute delay from now
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        int64_t now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        int64_t delay_ms = config.target_epoch_ms - now_ms;
        if (delay_ms < 100) delay_ms = 100;
        esp_timer_start_once(config.timer_handle, delay_ms * 1000);
    }

    config.active = true;
}

void TimerManager::OnTimerFired(int timer_id) {
    auto& app = Application::GetInstance();
    app.Schedule([this, timer_id]() {
        auto it = std::find_if(timers_.begin(), timers_.end(),
            [timer_id](const TimerConfig& t) { return t.id == timer_id; });

        if (it == timers_.end() || !it->active) return;

        // Handle music check timer specially
        if (it->type == TimerConfig::kMusicCheck) {
            std::string song_name;
            if (it->music_check_callback && it->music_check_callback(song_name)) {
                // Music playback completed, trigger conversation
                std::string message = "评价" + song_name;
                ESP_LOGI(TAG, "Music playback completed: %s, triggering review", song_name.c_str());
                TriggerAutoConversation(message);
            }
            
            // Reschedule music check timer (infinite repeats)
            if (it->timer_handle) {
                esp_timer_delete(it->timer_handle);
                it->timer_handle = nullptr;
            }
            StartEspTimer(*it);
            return;
        }

        ESP_LOGI(TAG, "Timer %d fired, message: %s", timer_id, it->message.c_str());
        TriggerAutoConversation(it->message);

        if (it->type == TimerConfig::kRelative && it->repeat_remaining > 1) {
            it->repeat_remaining--;
            // Clean old handle and restart
            if (it->timer_handle) {
                esp_timer_delete(it->timer_handle);
                it->timer_handle = nullptr;
            }
            StartEspTimer(*it);
            ESP_LOGI(TAG, "Timer %d rescheduled, remaining: %d", timer_id, it->repeat_remaining);
        } else {
            ESP_LOGI(TAG, "Timer %d completed", timer_id);
            StopAndCleanTimer(*it);
            timers_.erase(it);
        }
    });
}

void TimerManager::TriggerAutoConversation(const std::string& message) {
    Application::GetInstance().TriggerAutoConversation(message);
}

int TimerManager::CreateRelativeTimer(const std::string& message, int64_t interval_ms, int repeat_count) {
    if ((int)timers_.size() >= kMaxTimers) {
        ESP_LOGE(TAG, "Max timer limit reached (%d)", kMaxTimers);
        return -1;
    }
    if (interval_ms < 1000) {
        ESP_LOGE(TAG, "Interval too short: %d ms", (int)interval_ms);
        return -1;
    }

    TimerConfig config;
    config.id = next_id_++;
    config.message = message;
    config.type = TimerConfig::kRelative;
    config.interval_ms = interval_ms;
    config.repeat_total = repeat_count;
    config.repeat_remaining = repeat_count;
    config.target_epoch_ms = 0;

    timers_.push_back(std::move(config));
    StartEspTimer(timers_.back());

    ESP_LOGI(TAG, "Created relative timer %d: interval=%dms, repeat=%d, msg=%s",
             timers_.back().id, (int)interval_ms, repeat_count, message.c_str());
    return timers_.back().id;
}

int TimerManager::CreateAbsoluteTimer(const std::string& message, int64_t target_epoch_ms) {
    if ((int)timers_.size() >= kMaxTimers) {
        ESP_LOGE(TAG, "Max timer limit reached (%d)", kMaxTimers);
        return -1;
    }

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (target_epoch_ms <= now_ms) {
        ESP_LOGE(TAG, "Target time already passed");
        return -1;
    }

    TimerConfig config;
    config.id = next_id_++;
    config.message = message;
    config.type = TimerConfig::kAbsolute;
    config.interval_ms = 0;
    config.repeat_total = 1;
    config.repeat_remaining = 1;
    config.target_epoch_ms = target_epoch_ms;

    timers_.push_back(std::move(config));
    StartEspTimer(timers_.back());

    ESP_LOGI(TAG, "Created absolute timer %d: target=%dms, msg=%s",
             timers_.back().id, (int)(target_epoch_ms / 1000), message.c_str());
    return timers_.back().id;
}

int TimerManager::CreateMusicCheckTimer(int64_t interval_ms, std::function<bool(std::string&)> check_callback) {
    if ((int)timers_.size() >= kMaxTimers) {
        ESP_LOGE(TAG, "Max timer limit reached (%d)", kMaxTimers);
        return -1;
    }
    if (interval_ms < 1000) {
        ESP_LOGE(TAG, "Interval too short: %d ms", (int)interval_ms);
        return -1;
    }

    TimerConfig config;
    config.id = next_id_++;
    config.message = "music_check";  // Special marker for music check timer
    config.type = TimerConfig::kMusicCheck;
    config.interval_ms = interval_ms;
    config.repeat_total = -1;  // Infinite repeats
    config.repeat_remaining = -1;
    config.music_check_callback = check_callback;

    timers_.push_back(std::move(config));
    StartEspTimer(timers_.back());

    ESP_LOGI(TAG, "Created music check timer %d: interval=%ds, infinite repeats",
             timers_.back().id, (int)(interval_ms / 1000));
    return timers_.back().id;
}

bool TimerManager::DeleteTimer(int id) {
    auto it = std::find_if(timers_.begin(), timers_.end(),
        [id](const TimerConfig& t) { return t.id == id; });

    if (it == timers_.end()) return false;

    ESP_LOGI(TAG, "Deleting timer %d", id);
    StopAndCleanTimer(*it);
    timers_.erase(it);
    return true;
}

std::string TimerManager::ListTimers() {
    cJSON* arr = cJSON_CreateArray();

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    for (const auto& t : timers_) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", t.id);
        cJSON_AddStringToObject(obj, "message", t.message.c_str());
        const char* type_str = "absolute";
        if (t.type == TimerConfig::kRelative) type_str = "relative";
        else if (t.type == TimerConfig::kMusicCheck) type_str = "music_check";
        cJSON_AddStringToObject(obj, "type", type_str);
        cJSON_AddBoolToObject(obj, "active", t.active);

        if (t.type == TimerConfig::kRelative) {
            cJSON_AddNumberToObject(obj, "interval_seconds", (double)t.interval_ms / 1000.0);
            cJSON_AddNumberToObject(obj, "repeat_total", t.repeat_total);
            cJSON_AddNumberToObject(obj, "repeat_remaining", t.repeat_remaining);
        } else {
            // Format target time
            time_t target_sec = (time_t)(t.target_epoch_ms / 1000);
            struct tm timeinfo;
            localtime_r(&target_sec, &timeinfo);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            cJSON_AddStringToObject(obj, "target_time", buf);

            int64_t remaining_ms = t.target_epoch_ms - now_ms;
            if (remaining_ms > 0) {
                cJSON_AddNumberToObject(obj, "remaining_seconds", (double)remaining_ms / 1000.0);
            }
        }

        cJSON_AddItemToArray(arr, obj);
    }

    char* str = cJSON_PrintUnformatted(arr);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(arr);
    return result;
}

void TimerManager::RegisterMcpTools(McpServer* server) {
    // Tool: create timer
    server->AddTool("self.timer.create",
        "创建定时器。支持两种类型：\n"
        "1. 相对时间定时器：指定间隔秒数和重复次数，例如每3600秒(1小时)触发一次，重复3次\n"
        "2. 绝对时间定时器：指定触发的Unix时间戳(毫秒)，只触发一次\n"
        "定时器触发后会以指定的message内容向AI发起对话。\n"
        "message如含有“提醒我”，那么设置的消息最前面要加上“提醒我”。\n"
        "通过message的内容可以触发MCP工具调用或普通对话。",
        PropertyList({
            Property("message", kPropertyTypeString),
            Property("timer_type", kPropertyTypeString),
            Property("interval_seconds", kPropertyTypeInteger, 1, 1, 86400),
            Property("repeat_count", kPropertyTypeInteger, 1, 1, 100),
            Property("target_timestamp_ms", kPropertyTypeString, std::string("")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& mgr = TimerManager::GetInstance();
            std::string message = properties["message"].value<std::string>();
            std::string timer_type = properties["timer_type"].value<std::string>();

            if (timer_type == "relative") {
                int interval = properties["interval_seconds"].value<int>();
                int repeat = properties["repeat_count"].value<int>();
                int id = mgr.CreateRelativeTimer(message, (int64_t)interval * 1000, repeat);
                if (id < 0) return std::string("{\"success\":false,\"error\":\"创建定时器失败，可能已达上限(8个)\"}");
                return std::string("{\"success\":true,\"timer_id\":" + std::to_string(id) + "}");
            } else if (timer_type == "absolute") {
                std::string ts_str = properties["target_timestamp_ms"].value<std::string>();
                int64_t target_ms = 0;

                // Parse as epoch milliseconds
                for (char c : ts_str) {
                    if (c >= '0' && c <= '9') {
                        target_ms = target_ms * 10 + (c - '0');
                    }
                }

                if (target_ms == 0) return std::string("{\"success\":false,\"error\":\"无效的时间戳\"}");

                int id = mgr.CreateAbsoluteTimer(message, target_ms);
                if (id < 0) return std::string("{\"success\":false,\"error\":\"创建定时器失败，时间可能已过\"}");
                return std::string("{\"success\":true,\"timer_id\":" + std::to_string(id) + "}");
            }

            return std::string("{\"success\":false,\"error\":\"timer_type必须是relative或absolute\"}");
        });

    // Tool: delete timer
    server->AddTool("self.timer.delete",
        "删除指定ID的定时器",
        PropertyList({
            Property("timer_id", kPropertyTypeInteger),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int id = properties["timer_id"].value<int>();
            bool ok = TimerManager::GetInstance().DeleteTimer(id);
            if (ok) return std::string("{\"success\":true}");
            return std::string("{\"success\":false,\"error\":\"定时器不存在\"}");
        });

    // Tool: list timers
    server->AddTool("self.timer.list",
        "列出所有活动的定时器",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            return TimerManager::GetInstance().ListTimers();
        });
}
