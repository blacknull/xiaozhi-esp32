#include "timer_task_manager.h"
#include <esp_log.h>
#include <algorithm>
#include <ctime>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>

#define TAG "TimerTask"

TimerTaskManager::TimerTaskManager() {
}

TimerTaskManager::~TimerTaskManager() {
    // 清理所有任务
    for (auto& [id, task] : tasks_) {
        if (task->timer_handle != nullptr) {
            xTimerDelete(task->timer_handle, 0);
        }
    }
    tasks_.clear();
}

TimerTaskManager& TimerTaskManager::GetInstance() {
    static TimerTaskManager instance;
    return instance;
}

void TimerTaskManager::Initialize() {
    if (initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing Timer Task Manager...");
    
    // 创建绝对时间检查任务
    xTaskCreateWithCaps(
        AbsoluteTaskCheckLoop,
        "timer_check",
        4096,
        this,
        5,
        &check_task_handle_,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    initialized_ = true;
    ESP_LOGI(TAG, "Timer Task Manager initialized");
}

uint32_t TimerTaskManager::CreateRelativeTask(const std::string& name,
                                               uint32_t interval_seconds,
                                               int32_t repeat_count,
                                               const std::string& message,
                                               TimerConditionFunc condition,
                                               bool delete_on_trigger) {
    if (!initialized_) {
        Initialize();
    }
    
    if (interval_seconds < 1) {
        ESP_LOGE(TAG, "Invalid interval: %u seconds", interval_seconds);
        return 0;
    }

    // repeat_count == 0 视为执行一次（等同于 repeat_count == 1）
    if (repeat_count == 0) {
        repeat_count = 1;
    }

    std::lock_guard<std::mutex> lock(tasks_mutex_);

    uint32_t task_id = next_task_id_++;

    auto task = std::make_shared<TimerTask>();
    task->id = task_id;
    task->name = name;
    task->type = TimerTaskType::kRelative;
    task->interval_seconds = interval_seconds;
    task->repeat_count = repeat_count;
    task->remaining_count = repeat_count;
    task->message = message;
    task->active = true;
    task->delete_on_trigger = delete_on_trigger;
    task->timer_handle = nullptr;
    task->created_at = std::chrono::system_clock::now().time_since_epoch().count();
    task->last_triggered_at = 0;
    task->next_trigger_at = CalculateNextTriggerTime(*task);
    task->condition = condition;

    // 创建FreeRTOS定时器
    if (!CreateFreeRtosTimer(task)) {
        ESP_LOGE(TAG, "Failed to create timer for task %u", task_id);
        return 0;
    }
    
    tasks_[task_id] = task;
    
    ESP_LOGI(TAG, "Created relative task %u: '%s' every %u seconds, repeat %d times, message: '%s'",
             task_id, name.c_str(), interval_seconds, repeat_count, message.c_str());
    
    return task_id;
}

uint32_t TimerTaskManager::CreateAbsoluteTask(const std::string& name,
                                               uint64_t trigger_timestamp,
                                               const std::string& message) {
    if (!initialized_) {
        Initialize();
    }
    
    // 检查时间是否在未来
    auto now = std::chrono::system_clock::now();
    auto trigger_time = std::chrono::system_clock::time_point(
        std::chrono::seconds(trigger_timestamp));
    
    // 调试信息：显示当前时间和目标时间
    auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    int64_t diff_seconds = trigger_timestamp - now_seconds;
    
    ESP_LOGI(TAG, "CreateAbsoluteTask: now=%lld, trigger=%llu, diff=%lld seconds",
             now_seconds, trigger_timestamp, diff_seconds);
    
    // 转换时间为可读格式（使用 localtime_r 避免静态缓冲区被覆盖）
    time_t now_tt = now_seconds;
    time_t trigger_tt = trigger_timestamp;
    struct tm now_tm_buf, trigger_tm_buf;
    localtime_r(&now_tt, &now_tm_buf);
    localtime_r(&trigger_tt, &trigger_tm_buf);
    char now_str[32], trigger_str[32];
    strftime(now_str, sizeof(now_str), "%Y-%m-%d %H:%M:%S", &now_tm_buf);
    strftime(trigger_str, sizeof(trigger_str), "%Y-%m-%d %H:%M:%S", &trigger_tm_buf);
    
    ESP_LOGI(TAG, "CreateAbsoluteTask: now=%s, trigger=%s", now_str, trigger_str);
    
    if (trigger_time <= now) {
        ESP_LOGE(TAG, "Trigger time is in the past (now=%s, trigger=%s)", now_str, trigger_str);
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    uint32_t task_id = next_task_id_++;
    
    auto task = std::make_shared<TimerTask>();
    task->id = task_id;
    task->name = name;
    task->type = TimerTaskType::kAbsolute;
    task->interval_seconds = 0;
    task->repeat_count = 1;
    task->remaining_count = 1;
    task->trigger_time = trigger_time;
    task->message = message;
    task->active = true;
    task->delete_on_trigger = false;
    task->timer_handle = nullptr;
    task->created_at = std::chrono::system_clock::now().time_since_epoch().count();
    task->last_triggered_at = 0;
    task->next_trigger_at = trigger_timestamp;
    task->condition = nullptr;
    
    tasks_[task_id] = task;
    
    // 转换时间为人可读格式用于日志
    std::time_t tt = trigger_timestamp;
    std::tm tm = *std::localtime(&tt);
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);
    
    ESP_LOGI(TAG, "Created absolute task %u: '%s' at %s, message: '%s'",
             task_id, name.c_str(), time_str, message.c_str());
    
    return task_id;
}

bool TimerTaskManager::DeleteTask(uint32_t task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        ESP_LOGW(TAG, "Task %u not found", task_id);
        return false;
    }
    
    auto task = it->second;
    
    // 停止并删除定时器
    if (task->timer_handle != nullptr) {
        DeleteFreeRtosTimer(task);
    }
    
    task->active = false;
    tasks_.erase(it);
    
    ESP_LOGI(TAG, "Deleted task %u", task_id);
    return true;
}

cJSON* TimerTaskManager::GetAllTasksJson() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    cJSON* array = cJSON_CreateArray();
    
    for (const auto& [id, task] : tasks_) {
        if (task->active) {
            cJSON* task_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(task_json, "id", task->id);
            cJSON_AddStringToObject(task_json, "name", task->name.c_str());
            cJSON_AddStringToObject(task_json, "type", 
                task->type == TimerTaskType::kRelative ? "relative" : "absolute");
            cJSON_AddStringToObject(task_json, "message", task->message.c_str());
            cJSON_AddBoolToObject(task_json, "active", task->active);
            
            if (task->type == TimerTaskType::kRelative) {
                cJSON_AddNumberToObject(task_json, "interval_seconds", task->interval_seconds);
                cJSON_AddNumberToObject(task_json, "repeat_count", task->repeat_count);
                cJSON_AddNumberToObject(task_json, "remaining_count", task->remaining_count);
            } else {
                cJSON_AddNumberToObject(task_json, "trigger_timestamp", task->next_trigger_at);
            }
            
            cJSON_AddItemToArray(array, task_json);
        }
    }
    
    return array;
}

cJSON* TimerTaskManager::GetTaskJson(uint32_t task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end() || !it->second->active) {
        return nullptr;
    }
    
    auto task = it->second;
    cJSON* json = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(json, "id", task->id);
    cJSON_AddStringToObject(json, "name", task->name.c_str());
    cJSON_AddStringToObject(json, "type", 
        task->type == TimerTaskType::kRelative ? "relative" : "absolute");
    cJSON_AddStringToObject(json, "message", task->message.c_str());
    cJSON_AddBoolToObject(json, "active", task->active);
    
    if (task->type == TimerTaskType::kRelative) {
        cJSON_AddNumberToObject(json, "interval_seconds", task->interval_seconds);
        cJSON_AddNumberToObject(json, "repeat_count", task->repeat_count);
        cJSON_AddNumberToObject(json, "remaining_count", task->remaining_count);
    } else {
        cJSON_AddNumberToObject(json, "trigger_timestamp", task->next_trigger_at);
    }
    
    return json;
}

void TimerTaskManager::SetOnTaskTriggered(std::function<void(const TimerTask& task)> callback) {
    on_task_triggered_ = callback;
}

void TimerTaskManager::CheckAbsoluteTasks() {
    // 这个方法由后台任务调用
}

bool TimerTaskManager::CreateFreeRtosTimer(std::shared_ptr<TimerTask> task) {
    // 将任务ID作为定时器ID
    void* timer_id = reinterpret_cast<void*>(static_cast<uintptr_t>(task->id));
    
    task->timer_handle = xTimerCreate(
        task->name.c_str(),
        pdMS_TO_TICKS(task->interval_seconds * 1000),
        pdTRUE,  // 自动重载（周期性）
        timer_id,
        TimerCallback
    );
    
    if (task->timer_handle == nullptr) {
        return false;
    }
    
    // 启动定时器
    if (xTimerStart(task->timer_handle, 0) != pdPASS) {
        xTimerDelete(task->timer_handle, 0);
        task->timer_handle = nullptr;
        return false;
    }
    
    return true;
}

void TimerTaskManager::DeleteFreeRtosTimer(std::shared_ptr<TimerTask> task) {
    if (task->timer_handle != nullptr) {
        xTimerStop(task->timer_handle, 0);
        xTimerDelete(task->timer_handle, 0);
        task->timer_handle = nullptr;
    }
}

void TimerTaskManager::TimerCallback(TimerHandle_t xTimer) {
    void* timer_id = pvTimerGetTimerID(xTimer);
    uint32_t task_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(timer_id));
    
    auto& manager = TimerTaskManager::GetInstance();
    manager.HandleRelativeTaskTriggered(task_id);
}

void TimerTaskManager::HandleRelativeTaskTriggered(uint32_t task_id) {
    std::shared_ptr<TimerTask> task;
    bool should_delete = false;

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);

        auto it = tasks_.find(task_id);
        if (it == tasks_.end() || !it->second->active) {
            return;
        }

        task = it->second;

        // 如果设置了条件回调，检查条件是否满足
        if (task->condition && !task->condition()) {
            // 条件不满足，跳过本次触发，不消耗重复次数
            return;
        }

        // 更新计数
        if (task->remaining_count > 0) {
            task->remaining_count--;
        }

        task->last_triggered_at = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;

        // 触发后自动删除
        if (task->delete_on_trigger) {
            should_delete = true;
            task->active = false;
            if (task->timer_handle != nullptr) {
                xTimerStop(task->timer_handle, 0);
            }
        }
        // 检查是否需要停止（非无限重复且次数用完）
        else if (task->repeat_count > 0 && task->remaining_count <= 0) {
            ESP_LOGI(TAG, "Task %u finished all repetitions", task_id);
            should_delete = true;
            task->active = false;
            if (task->timer_handle != nullptr) {
                xTimerStop(task->timer_handle, 0);
            }
        } else {
            // 更新下次触发时间
            task->next_trigger_at = CalculateNextTriggerTime(*task);
        }
    }

    ESP_LOGI(TAG, "Task %u triggered, message: '%s'", task_id, task->message.c_str());

    // 调用回调
    if (on_task_triggered_) {
        on_task_triggered_(*task);
    }

    // 在回调执行完成后删除任务
    if (should_delete) {
        ESP_LOGI(TAG, "Auto-deleting task %u after trigger", task_id);
        DeleteTask(task_id);
    }
}

void TimerTaskManager::AbsoluteTaskCheckLoop(void* arg) {
    auto* manager = static_cast<TimerTaskManager*>(arg);
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 每秒检查一次
        
        auto now = std::chrono::system_clock::now();
        // 收集触发的任务信息（持有锁时拷贝 shared_ptr，避免锁外访问 tasks_ map）
        std::vector<std::pair<uint32_t, std::shared_ptr<TimerTask>>> triggered_tasks;

        {
            std::lock_guard<std::mutex> lock(manager->tasks_mutex_);

            for (auto& [id, task] : manager->tasks_) {
                if (!task->active || task->type != TimerTaskType::kAbsolute) {
                    continue;
                }

                if (now >= task->trigger_time) {
                    task->last_triggered_at = now.time_since_epoch().count() / 1000000000;
                    task->active = false;  // 绝对时间任务只执行一次
                    triggered_tasks.emplace_back(id, task);  // 拷贝 shared_ptr

                    ESP_LOGI(TAG, "Absolute task %u triggered at scheduled time", id);
                }
            }
        }

        // 在锁外调用回调（通过 shared_ptr 安全访问 task 数据）
        for (auto& [task_id, task] : triggered_tasks) {
            if (manager->on_task_triggered_) {
                manager->on_task_triggered_(*task);
            }
        }

        // 清理已触发的任务
        for (auto& [task_id, task] : triggered_tasks) {
            manager->DeleteTask(task_id);
        }
    }
}

uint64_t TimerTaskManager::CalculateNextTriggerTime(const TimerTask& task) {
    auto now = std::chrono::system_clock::now();
    auto next_trigger = now + std::chrono::seconds(task.interval_seconds);
    return std::chrono::duration_cast<std::chrono::seconds>(
        next_trigger.time_since_epoch()).count();
}
