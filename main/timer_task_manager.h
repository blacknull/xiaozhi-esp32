#ifndef TIMER_TASK_MANAGER_H
#define TIMER_TASK_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <cJSON.h>

// 定时任务类型
enum class TimerTaskType {
    kRelative,  // 相对时间（周期性）
    kAbsolute   // 绝对时间（一次性）
};

// 触发条件回调：返回 true 表示条件满足可以触发，false 表示跳过本次触发
using TimerConditionFunc = std::function<bool()>;

// 定时任务信息
struct TimerTask {
    uint32_t id;                                    // 任务ID
    std::string name;                               // 任务名称/描述
    TimerTaskType type;                             // 任务类型

    // 相对时间参数
    uint32_t interval_seconds;                      // 间隔秒数（相对时间用）
    int32_t repeat_count;                           // 重复次数，-1表示无限
    int32_t remaining_count;                        // 剩余次数

    // 绝对时间参数
    std::chrono::system_clock::time_point trigger_time;  // 触发时间点（绝对时间用）

    // 通用参数
    std::string message;                            // 触发时发送给AI的消息
    bool active;                                    // 是否激活
    bool delete_on_trigger;                         // 触发后自动删除
    TimerHandle_t timer_handle;                     // FreeRTOS定时器句柄
    uint64_t created_at;                            // 创建时间戳
    uint64_t last_triggered_at;                     // 上次触发时间戳
    uint64_t next_trigger_at;                       // 下次触发时间戳（用于查询）
    TimerConditionFunc condition;                   // 触发条件回调（可选）
};

class TimerTaskManager {
public:
    static TimerTaskManager& GetInstance();
    
    // 初始化
    void Initialize();
    
    // 创建相对时间定时任务（周期性）
    // @param name: 任务名称
    // @param interval_seconds: 间隔秒数
    // @param repeat_count: 重复次数，-1表示无限重复
    // @param message: 触发时发送给AI的消息
    // @param condition: 触发条件回调（可选），返回 true 才真正触发，false 跳过本次
    // @param delete_on_trigger: 触发后自动删除（默认 false）
    // @return: 任务ID，0表示创建失败
    uint32_t CreateRelativeTask(const std::string& name,
                                 uint32_t interval_seconds,
                                 int32_t repeat_count,
                                 const std::string& message,
                                 TimerConditionFunc condition = nullptr,
                                 bool delete_on_trigger = false);
    
    // 创建绝对时间定时任务（一次性）
    // @param name: 任务名称
    // @param trigger_timestamp: 触发时间戳（Unix时间戳，秒）
    // @param message: 触发时发送给AI的消息
    // @return: 任务ID，0表示创建失败
    uint32_t CreateAbsoluteTask(const std::string& name,
                                 uint64_t trigger_timestamp,
                                 const std::string& message);
    
    // 删除定时任务
    // @param task_id: 任务ID
    // @return: 是否成功删除
    bool DeleteTask(uint32_t task_id);
    
    // 获取所有任务列表（JSON格式）
    cJSON* GetAllTasksJson();
    
    // 获取单个任务信息（JSON格式）
    cJSON* GetTaskJson(uint32_t task_id);
    
    // 设置任务触发回调
    void SetOnTaskTriggered(std::function<void(const TimerTask& task)> callback);
    
    // 检查并触发到期的绝对时间任务（需要在主循环中定期调用）
    void CheckAbsoluteTasks();
    
private:
    TimerTaskManager();
    ~TimerTaskManager();
    TimerTaskManager(const TimerTaskManager&) = delete;
    TimerTaskManager& operator=(const TimerTaskManager&) = delete;
    
    std::mutex tasks_mutex_;
    std::map<uint32_t, std::shared_ptr<TimerTask>> tasks_;
    uint32_t next_task_id_ = 1;
    bool initialized_ = false;
    
    std::function<void(const TimerTask& task)> on_task_triggered_;
    
    TaskHandle_t check_task_handle_ = nullptr;
    
    // 创建FreeRTOS定时器（用于相对时间任务）
    bool CreateFreeRtosTimer(std::shared_ptr<TimerTask> task);
    
    // 删除FreeRTOS定时器
    void DeleteFreeRtosTimer(std::shared_ptr<TimerTask> task);
    
    // 定时器回调函数（静态）
    static void TimerCallback(TimerHandle_t xTimer);
    
    // 处理相对时间任务触发
    void HandleRelativeTaskTriggered(uint32_t task_id);
    
    // 绝对时间检查任务（后台任务）
    static void AbsoluteTaskCheckLoop(void* arg);
    
    // 计算下次触发时间戳
    uint64_t CalculateNextTriggerTime(const TimerTask& task);
};

#endif // TIMER_TASK_MANAGER_H
