/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <esp_pthread.h>
#include <esp_heap_caps.h>

#include "application.h"
#include "display.h"
#include "board.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"
#include "boards/common/esp32_music.h"
#include "ntp_time_sync.h"
#include "timer_task_manager.h"

#define TAG "MCP"

#define DEFAULT_TOOLCALL_STACK_SIZE 6144

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return true;
                }
                return false;
            });
    }
#endif

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Take a photo and explain it. Use this tool after the user asks you to see something.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo");
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }
 
     auto music = board.GetMusic();
     if (music) {
         AddTool("self.music.play_song",
             "播放指定的歌曲。当用户要求播放音乐时使用此工具，会自动获取歌曲详情并开始流式播放。\n"
             "参数:\n"
             "  `song_name`: 要播放的歌曲名称（必需）。\n"
             "  `artist_name`: 要播放的歌曲艺术家名称（可选，默认为空字符串）。\n"
             "返回:\n"
             "  播放状态信息，不需确认，立刻播放歌曲。",
             PropertyList({
                 Property("song_name", kPropertyTypeString),//歌曲名称（必需）
                 Property("artist_name", kPropertyTypeString, "")//艺术家名称（可选，默认为空字符串）
             }),
             [music](const PropertyList& properties) -> ReturnValue {
                 auto song_name = properties["song_name"].value<std::string>();
                 auto artist_name = properties["artist_name"].value<std::string>();

                 if (!music->Download(song_name, artist_name)) {
                     return "{\"success\": false, \"message\": \"获取音乐资源失败\"}";
                 }
                 auto download_result = music->GetDownloadResult();
                 ESP_LOGI(TAG, "Music details result: %s", download_result.c_str());

                 // 创建音乐播放完成监控定时器（3秒轮询，条件触发，触发后自动删除）
                 auto* esp_music = dynamic_cast<Esp32Music*>(music);
                 if (esp_music) {
                     auto& timer_mgr = TimerTaskManager::GetInstance();

                     // 删除旧的监控定时器（如上一首歌被打断，定时器仍在运行）
                     uint32_t old_id = esp_music->GetMonitorTimerId();
                     if (old_id != 0) {
                         timer_mgr.DeleteTask(old_id);
                         esp_music->SetMonitorTimerId(0);
                     }

                     std::string review_msg = "评论歌曲" + song_name;
                     uint32_t new_id = timer_mgr.CreateRelativeTask(
                         "音乐播放完成检测",
                         3,       // 每3秒检查一次
                         -1,      // 无限循环
                         review_msg,
                         [esp_music]() -> bool {
                             std::string finished_song;
                             return esp_music->CheckPlaybackCompleted(finished_song);
                         },
                         true     // 触发后自动删除
                     );
                     esp_music->SetMonitorTimerId(new_id);
                 }

                 return "{\"success\": true, \"message\": \"音乐开始播放\"}";
             });
 
         AddTool("self.music.set_display_mode",
             "设置音乐播放时的显示模式。可以选择显示频谱或歌词，比如用户说‘打开频谱’或者‘显示频谱’，‘打开歌词’或者‘显示歌词’就设置对应的显示模式。\n"
             "参数:\n"
             "  `mode`: 显示模式，可选值为 'spectrum'（频谱）或 'lyrics'（歌词）。\n"
             "返回:\n"
             "  设置结果信息。",
             PropertyList({
                 Property("mode", kPropertyTypeString)//显示模式: "spectrum" 或 "lyrics"
             }),
             [music](const PropertyList& properties) -> ReturnValue {
                 auto mode_str = properties["mode"].value<std::string>();
                 
                 // 转换为小写以便比较
                 std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
                 
                 if (mode_str == "spectrum" || mode_str == "频谱") {
                     // 设置为频谱显示模式
                     auto esp32_music = static_cast<Esp32Music*>(music);
                     esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_SPECTRUM);
                     return "{\"success\": true, \"message\": \"已切换到频谱显示模式\"}";
                 } else if (mode_str == "lyrics" || mode_str == "歌词") {
                     // 设置为歌词显示模式
                     auto esp32_music = static_cast<Esp32Music*>(music);
                     esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_LYRICS);
                     return "{\"success\": true, \"message\": \"已切换到歌词显示模式\"}";
                 } else {
                     return "{\"success\": false, \"message\": \"无效的显示模式，请使用 'spectrum' 或 'lyrics'\"}";
                 }
                 
                 return "{\"success\": false, \"message\": \"设置显示模式失败\"}";
             });
     }

    // Timer task tools (AI可见) - 放在常用工具列表中
    AddTool("self.timer.create_relative",
        "产生一个相对时间定时器（周期性）。在指定间隔后触发，并且可以重复多次。"
        "当用户要求设置带有相对时间的定时器、闹钟或提醒时使用，例如 '提醒我5分钟后喝水', '每小时提醒我去休息一会', 等。"
        "有带提醒，通知等词汇时，要发送的消息前部要以'提醒我'，'通知我'等词汇开头，后面跟上具体的提醒内容。"
        "示例: '提醒我每小时去查一下天气' -> interval_seconds=3600, repeat_count=3, message='提醒我去查天气'",
        PropertyList({
            Property("name", kPropertyTypeString),                    // 任务名称
            Property("interval_seconds", kPropertyTypeInteger, 1),    // 间隔秒数，至少1秒
            Property("repeat_count", kPropertyTypeInteger, -1),       // 重复次数，-1表示无限，0或1表示只执行一次
            Property("message", kPropertyTypeString)                  // 触发时发送给AI的消息
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& timer_manager = TimerTaskManager::GetInstance();
            
            std::string name = properties["name"].value<std::string>();
            int interval = properties["interval_seconds"].value<int>();
            int repeat = properties["repeat_count"].value<int>();
            std::string message = properties["message"].value<std::string>();
            
            if (interval < 1) {
                return std::string("Error: interval_seconds must be at least 1");
            }
            
            // 转换repeat_count：如果为0或1，都表示只执行一次
            if (repeat == 0) {
                repeat = 1;
            }
            
            uint32_t task_id = timer_manager.CreateRelativeTask(name, interval, repeat, message);
            
            cJSON* result = cJSON_CreateObject();
            if (task_id != 0) {
                cJSON_AddBoolToObject(result, "success", true);
                cJSON_AddNumberToObject(result, "task_id", task_id);
                cJSON_AddStringToObject(result, "name", name.c_str());
                cJSON_AddNumberToObject(result, "interval_seconds", interval);
                cJSON_AddNumberToObject(result, "repeat_count", repeat);
                cJSON_AddStringToObject(result, "message", message.c_str());
                
                // 计算并显示人类可读的时间
                int hours = interval / 3600;
                int minutes = (interval % 3600) / 60;
                int seconds = interval % 60;
                char time_desc[64];
                if (hours > 0) {
                    snprintf(time_desc, sizeof(time_desc), "every %d hour(s)", hours);
                } else if (minutes > 0) {
                    snprintf(time_desc, sizeof(time_desc), "every %d minute(s)", minutes);
                } else {
                    snprintf(time_desc, sizeof(time_desc), "every %d second(s)", seconds);
                }
                cJSON_AddStringToObject(result, "interval_description", time_desc);
                
                if (repeat > 0) {
                    cJSON_AddStringToObject(result, "schedule", 
                        (std::string("Will trigger ") + time_desc + ", " + std::to_string(repeat) + " time(s) total").c_str());
                } else {
                    cJSON_AddStringToObject(result, "schedule", 
                        (std::string("Will trigger ") + time_desc + " indefinitely").c_str());
                }
            } else {
                cJSON_AddBoolToObject(result, "success", false);
                cJSON_AddStringToObject(result, "error", "Failed to create timer task");
            }
            
            return result;
        });
    
    AddTool("self.timer.create_absolute",
        "生成一个绝对时间定时器（单次）。在特定时间戳触发。 "
        "当用户要求设置带有绝对时间的定时器、闹钟或提醒时使用，例如 '下午三点', '明天上午10：30', 等。 "
        "有带提醒，通知等词汇时，要发送的消息前部要以'提醒我'，'通知我'等词汇开头，后面跟上具体的提醒内容。"
        "示例: '提醒我明天下午1：30分去学校' -> timestamp=明天下午1:30的Unix时间戳, message='提醒我去学校'",
        PropertyList({
            Property("name", kPropertyTypeString),           // 任务名称
            Property("timestamp", kPropertyTypeInteger),     // Unix时间戳（秒）
            Property("message", kPropertyTypeString)         // 触发时发送给AI的消息
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& timer_manager = TimerTaskManager::GetInstance();
            
            std::string name = properties["name"].value<std::string>();
            int64_t timestamp = properties["timestamp"].value<int>();
            std::string message = properties["message"].value<std::string>();
            
            if (timestamp <= 0) {
                return std::string("Error: timestamp must be a positive integer");
            }
            
            uint32_t task_id = timer_manager.CreateAbsoluteTask(name, timestamp, message);
            
            cJSON* result = cJSON_CreateObject();
            if (task_id != 0) {
                cJSON_AddBoolToObject(result, "success", true);
                cJSON_AddNumberToObject(result, "task_id", task_id);
                cJSON_AddStringToObject(result, "name", name.c_str());
                cJSON_AddNumberToObject(result, "timestamp", timestamp);
                cJSON_AddStringToObject(result, "message", message.c_str());
                
                // 转换时间戳为人类可读格式
                time_t tt = timestamp;
                struct tm* tm_info = localtime(&tt);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                cJSON_AddStringToObject(result, "trigger_time", time_str);
            } else {
                cJSON_AddBoolToObject(result, "success", false);
                cJSON_AddStringToObject(result, "error", "Failed to create timer task (timestamp is in the past)");
                
                // 返回当前时间信息，帮助 AI 校准
                auto& ntp = NtpTimeSync::GetInstance();
                cJSON_AddNumberToObject(result, "current_timestamp", ntp.GetTimestamp());
                cJSON_AddStringToObject(result, "current_time", ntp.GetLocalTimeString().c_str());
                cJSON_AddNumberToObject(result, "requested_timestamp", timestamp);
                cJSON_AddStringToObject(result, "hint", "The requested time is in the past. Please use self.time.get to get current time and calculate a future timestamp.");
            }
            
            return result;
        });
    
    AddTool("self.timer.list",
        "List all active timer tasks. Use this when user asks about existing timers or reminders.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& timer_manager = TimerTaskManager::GetInstance();
            cJSON* tasks = timer_manager.GetAllTasksJson();
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddItemToObject(result, "tasks", tasks);
            cJSON_AddNumberToObject(result, "count", cJSON_GetArraySize(tasks));
            
            return result;
        });
    
    AddTool("self.timer.delete",
        "Delete a timer task by ID. Use this when user asks to cancel or remove a timer.",
        PropertyList({
            Property("task_id", kPropertyTypeInteger)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& timer_manager = TimerTaskManager::GetInstance();
            int task_id = properties["task_id"].value<int>();
            
            bool success = timer_manager.DeleteTask(task_id);
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", success);
            cJSON_AddNumberToObject(result, "task_id", task_id);
            if (!success) {
                cJSON_AddStringToObject(result, "error", "Task not found or already deleted");
            }
            
            return result;
        });

    // Time sync tools
    AddTool("self.time.sync",
        "Synchronize system time from NTP servers. Supports multiple NTP servers: pool.ntp.org, ntp.aliyun.com, ntp.tencent.com. "
        "If one server fails, it will automatically try the next one.",
        PropertyList({
            Property("timezone_offset", kPropertyTypeInteger, 8, -12, 14),  // 默认东8区，范围UTC-12到UTC+14
            Property("set_local_time", kPropertyTypeBoolean, true)  // 是否设置到本地系统时间
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& ntp = NtpTimeSync::GetInstance();
            int tz_offset = properties["timezone_offset"].value<int>();
            bool set_local = properties["set_local_time"].value<bool>();
            
            ESP_LOGI(TAG, "MCP: Syncing time with timezone UTC%+d", tz_offset);
            
            bool success = ntp.SyncTime(tz_offset);
            std::string time_str = ntp.GetLocalTimeString();
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "success", success);
            cJSON_AddStringToObject(result, "current_time", time_str.c_str());
            cJSON_AddNumberToObject(result, "timezone_offset", tz_offset);
            cJSON_AddBoolToObject(result, "local_time_set", set_local);
            
            if (success) {
                cJSON_AddStringToObject(result, "message", "Time synchronized successfully");
            } else {
                cJSON_AddStringToObject(result, "message", "Failed to synchronize time from all NTP servers");
            }
            
            return result;
        });
    
    AddTool("self.time.get",
        "Get current system time. Returns the local time string, timestamp and timezone offset.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& ntp = NtpTimeSync::GetInstance();
            
            // 获取带时区的 ISO 格式时间
            std::string iso_time = ntp.GetLocalTimeString("%Y-%m-%dT%H:%M:%S");
            
            // 获取时区偏移（小时）并格式化为字符串，如 "+08:00" 或 "-05:00"
            int tz_offset = ntp.GetTimezoneOffset();
            char tz_str[16];
            if (tz_offset >= 0) {
                snprintf(tz_str, sizeof(tz_str), "+%02d:00", tz_offset);
            } else {
                snprintf(tz_str, sizeof(tz_str), "-%02d:00", -tz_offset);
            }
            
            cJSON* result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "local_time", ntp.GetLocalTimeString().c_str());
            cJSON_AddStringToObject(result, "iso_time", (iso_time + tz_str).c_str());
            cJSON_AddNumberToObject(result, "timestamp", ntp.GetTimestamp());
            cJSON_AddNumberToObject(result, "timezone_offset_hours", tz_offset);
            cJSON_AddStringToObject(result, "timezone", tz_str);
            cJSON_AddBoolToObject(result, "synced", ntp.IsTimeSynced());
            
            return result;
        });
    
    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddTool("self.reboot", "Reboot the device / 重启设备",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            std::thread([]() {
                ESP_LOGW(TAG, "Reboot requested");
                vTaskDelay(pdMS_TO_TICKS(1000));
                auto& app = Application::GetInstance();
                app.Reboot();
            }).detach();
            return true;
        });


    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                return json;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    total_read += ret;
                }
                http->Close();

                auto img_dsc = (lv_img_dsc_t*)heap_caps_calloc(1, sizeof(lv_img_dsc_t), MALLOC_CAP_8BIT);
                img_dsc->data_size = content_length;
                img_dsc->data = (uint8_t*)data;
                if (lv_image_decoder_get_info(img_dsc, &img_dsc->header) != LV_RESULT_OK) {
                    heap_caps_free(data);
                    heap_caps_free(img_dsc);
                    throw std::runtime_error("Failed to get image info");
                }
                ESP_LOGI(TAG, "Preview image: %s size: %d resolution: %d x %d", url.c_str(), content_length, img_dsc->header.w, img_dsc->header.h);

                auto& app = Application::GetInstance();
                app.Schedule([display, img_dsc]() {
                    display->SetPreviewImage(img_dsc);
                });
                return true;
            });
    }
#endif

    // Assets download url
    auto assets = Board::GetInstance().GetAssets();
    if (assets) {
        if (assets->partition_valid()) {
            AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
                PropertyList({
                    Property("url", kPropertyTypeString)
                }),
                [assets](const PropertyList& properties) -> ReturnValue {
                    auto url = properties["url"].value<std::string>();
                    Settings settings("assets", true);
                    settings.SetString("download_url", url);
                    return true;
                });
        }
    }
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
                ESP_LOGI(TAG, "Set camera explain url: %s", url_str.c_str());
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        auto stack_size = cJSON_GetObjectItem(params, "stackSize");
        if (stack_size != nullptr && !cJSON_IsNumber(stack_size)) {
            ESP_LOGE(TAG, "tools/call: Invalid stackSize");
            ReplyError(id_int, "Invalid stackSize");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments, stack_size ? stack_size->valueint : DEFAULT_TOOLCALL_STACK_SIZE);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, int stack_size) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Start a task to receive data with stack size
    // 注意：tool_call 线程可能访问 NVS/SPI Flash（如 DeviceManager、Settings），
    // 因此栈必须在 SRAM，不能用 PSRAM（禁用缓存时 PSRAM 不可访问）
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.thread_name = "tool_call";
    cfg.stack_size = stack_size;
    cfg.prio = 1;
    esp_pthread_set_cfg(&cfg);

    // Use a thread to call the tool to avoid blocking the main thread
    tool_call_thread_ = std::thread([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
    tool_call_thread_.detach();
}