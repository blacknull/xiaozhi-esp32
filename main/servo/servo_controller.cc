#include "servo/servo_controller.h"
#include "servo/pca9685.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <esp_log.h>
#include <freertos/task.h>
#include <cJSON.h>

static const char* TAG = "ServoCtrl";

// MG90S 脉冲宽度范围（ms）
static constexpr double kMinPulse = 0.4;
static constexpr double kMaxPulse = 2.6;

// ---------- 初始化 ----------

bool ServoController::Initialize(i2c_master_bus_handle_t bus, uint8_t pca_addr) {
    if (initialized_) {
        return true;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return false;
    }

    pca_ = new Pca9685(bus, pca_addr);
    pca_->Init(50);

    // 所有通道初始化到 90°
    for (int i = 0; i < kMaxServos; i++) {
        states_[i] = {90, 90, 0, false};
        pca_->SetPwm(i, AngleToPwm(90));
    }

    BaseType_t ret = xTaskCreate(ControlTask, "servo_ctrl", 3072, this, 5, nullptr);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建控制任务失败");
        delete pca_;
        pca_ = nullptr;
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "舵机控制器初始化完成");
    return true;
}

// ---------- FreeRTOS 控制任务 ----------

void ServoController::ControlTask(void* arg) {
    auto* self = static_cast<ServoController*>(arg);
    while (true) {
        self->UpdateServos();
        vTaskDelay(pdMS_TO_TICKS(kTaskUpdateMs));
    }
}

void ServoController::UpdateServos() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    for (int i = 0; i < kMaxServos; i++) {
        ServoState& s = states_[i];
        if (!s.rotating) continue;

        int diff = s.target_angle - s.current_angle;
        if (diff == 0) {
            s.rotating = false;
            continue;
        }

        // speed 0-100 对应每 10ms 步进 0-5°
        int step = (s.speed * 5) / 100;
        if (step == 0) step = 1;

        if (std::abs(diff) <= step) {
            s.current_angle = s.target_angle;
            s.rotating = false;
        } else {
            s.current_angle += (diff > 0) ? step : -step;
        }

        pca_->SetPwm(i, AngleToPwm(s.current_angle));
    }
    xSemaphoreGive(mutex_);
}

// ---------- 角度转 PWM ----------

uint16_t ServoController::AngleToPwm(int angle) {
    if (angle < 0) angle = 0;
    if (angle > kMaxServoAngle) angle = kMaxServoAngle;
    double pulse = kMinPulse + static_cast<double>(angle) * (kMaxPulse - kMinPulse) / kMaxServoAngle;
    uint16_t pwm = static_cast<uint16_t>((pulse / 20.0) * 4096.0);
    if (pwm > 4095) pwm = 4095;
    return pwm;
}

// ---------- 设置角度（需持有 mutex_）----------

void ServoController::SetAngle(int channel, int angle, int speed) {
    if (channel < 0 || channel >= kMaxServos) return;
    if (angle < 0) angle = 0;
    if (angle > kMaxServoAngle) angle = kMaxServoAngle;
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;

    ServoState& s = states_[channel];
    s.target_angle = angle;
    s.speed = speed;
    if (angle != s.current_angle) {
        s.rotating = true;
    }
}

// ---------- 停止所有 ----------

int ServoController::StopAll() {
    int count = 0;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    for (int i = 0; i < kMaxServos; i++) {
        if (states_[i].rotating) {
            states_[i].target_angle = states_[i].current_angle;
            states_[i].rotating = false;
            count++;
        }
    }
    xSemaphoreGive(mutex_);
    return count;
}

// ---------- 获取状态 ----------

std::vector<ServoController::ServoStatus> ServoController::GetStatus() {
    std::vector<ServoStatus> result;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return result;
    for (int i = 0; i < kMaxServos; i++) {
        result.push_back({i, states_[i].current_angle, states_[i].rotating});
    }
    xSemaphoreGive(mutex_);
    return result;
}

// ---------- 列出动作名 ----------

std::vector<std::string> ServoController::ListMovements(const std::string& json_str) {
    std::vector<std::string> names;
    if (json_str.empty()) return names;

    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) return names;

    cJSON* arr = cJSON_GetObjectItem(root, "movement");
    if (arr && cJSON_IsArray(arr)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
            cJSON* name = cJSON_GetObjectItem(item, "name");
            if (name && cJSON_IsString(name)) {
                names.push_back(name->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return names;
}

// ---------- 执行动作 ----------

// 预解析的动作（角度保留原始值，执行时再解析）
struct ParsedAction {
    int channel;
    int raw_start;  // -1 = 保持当前
    int raw_end;    // -1 = 无变化，>180 = 相对角度
    int start_time; // ms
    int duration;   // ms
};

static void ResolveAction(const ParsedAction& a, int current_angle,
                           int* out_start, int* out_end, int* out_speed) {
    int actual_start = (a.raw_start == -1) ? current_angle : a.raw_start;

    int actual_end = a.raw_end;
    if (actual_end == -1) {
        actual_end = actual_start;
    } else if (actual_end > 180) {
        int relative = actual_end - 360;
        actual_end = current_angle + relative;
        if (actual_end < 0) actual_end = 0;
        if (actual_end > 180) actual_end = 180;
    }

    int diff = std::abs(actual_end - actual_start);
    int min_dur = (diff * 1000) / 600; // MG90S 最大速度 600°/s
    if (min_dur < 1) min_dur = 1;

    int speed = 100;
    if (a.duration > min_dur) {
        speed = (min_dur * 100) / a.duration;
        if (speed < 1) speed = 1;
    }

    *out_start = actual_start;
    *out_end   = actual_end;
    *out_speed = speed;
}

bool ServoController::ExecuteMovement(const std::string& json_str,
                                       const std::string& name, int times) {
    if (!initialized_ || json_str.empty()) return false;

    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失败");
        return false;
    }

    // 找到对应动作
    cJSON* arr = cJSON_GetObjectItem(root, "movement");
    cJSON* movement_item = nullptr;
    if (arr && cJSON_IsArray(arr)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
            cJSON* n = cJSON_GetObjectItem(item, "name");
            if (n && cJSON_IsString(n) && name == n->valuestring) {
                movement_item = item;
                break;
            }
        }
    }

    if (!movement_item) {
        ESP_LOGE(TAG, "未找到动作: %s", name.c_str());
        cJSON_Delete(root);
        return false;
    }

    cJSON* actions_arr = cJSON_GetObjectItem(movement_item, "actions");
    if (!actions_arr || !cJSON_IsArray(actions_arr)) {
        ESP_LOGE(TAG, "动作数组无效");
        cJSON_Delete(root);
        return false;
    }

    // 解析所有 action
    std::vector<ParsedAction> actions;
    cJSON* action = nullptr;
    cJSON_ArrayForEach(action, actions_arr) {
        cJSON* ch  = cJSON_GetObjectItem(action, "channel");
        cJSON* sa  = cJSON_GetObjectItem(action, "starting_angle");
        cJSON* ea  = cJSON_GetObjectItem(action, "end_angle");
        cJSON* st  = cJSON_GetObjectItem(action, "starting_time");
        cJSON* dur = cJSON_GetObjectItem(action, "duration");
        if (!ch || !sa || !ea || !st || !dur) continue;
        int channel = ch->valueint;
        if (channel < 0 || channel >= kMaxServos) continue;
        actions.push_back({channel, sa->valueint, ea->valueint,
                           st->valueint, dur->valueint});
    }

    cJSON_Delete(root);

    if (actions.empty()) return true;

    // 按 start_time 排序
    std::sort(actions.begin(), actions.end(),
              [](const ParsedAction& a, const ParsedAction& b) {
                  return a.start_time < b.start_time;
              });

    for (int t = 0; t < times; t++) {
        // 第一遍：准备起始位置（快速移到第一个 action 的 starting_angle）
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
            std::vector<int> prepped_channels;
            for (auto& a : actions) {
                if (a.raw_start == -1) continue;
                if (std::find(prepped_channels.begin(), prepped_channels.end(), a.channel)
                    != prepped_channels.end()) continue;
                prepped_channels.push_back(a.channel);
                SetAngle(a.channel, a.raw_start, 100);
            }
            xSemaphoreGive(mutex_);
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // 等待到达起始位置

        // 第二遍：按时间顺序派发
        int max_end_time = 0;
        TickType_t t0 = xTaskGetTickCount();

        for (auto& a : actions) {
            TickType_t target = t0 + pdMS_TO_TICKS(a.start_time);
            TickType_t now = xTaskGetTickCount();
            if (now < target) vTaskDelay(target - now);

            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
                int cur = states_[a.channel].current_angle;
                int actual_start, actual_end, speed;
                ResolveAction(a, cur, &actual_start, &actual_end, &speed);

                if (actual_start != cur) {
                    SetAngle(a.channel, actual_start, 100);
                }
                SetAngle(a.channel, actual_end, speed);
                ESP_LOGD(TAG, "  派发 ch%d %d°->%d° spd=%d t=%dms",
                         a.channel, actual_start, actual_end, speed, a.start_time);
                xSemaphoreGive(mutex_);
            }

            int end_time = a.start_time + a.duration;
            if (end_time > max_end_time) max_end_time = end_time;
        }

        // 等待所有动作完成
        TickType_t target_end = t0 + pdMS_TO_TICKS(max_end_time);
        TickType_t now = xTaskGetTickCount();
        if (now < target_end) vTaskDelay(target_end - now);
    }

    return true;
}
