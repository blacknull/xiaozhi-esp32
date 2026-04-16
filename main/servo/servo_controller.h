#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <string>
#include <vector>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Pca9685;

// 管理 16 路 PCA9685 舵机的控制器（单例）
class ServoController {
public:
    static ServoController& GetInstance() {
        static ServoController instance;
        return instance;
    }

    // 初始化，接受已有的 I2C 总线句柄（与屏幕共用同一总线）
    bool Initialize(i2c_master_bus_handle_t bus, uint8_t pca_addr = 0x40);

    // 在 json_str（movement.json 内容）中按名字查找并执行动作
    // times: 重复次数（默认 1）
    bool ExecuteMovement(const std::string& json_str, const std::string& name, int times = 1);

    // 列出 json_str 中所有动作名
    std::vector<std::string> ListMovements(const std::string& json_str);

    // 停止所有正在旋转的舵机，返回停止数量
    int StopAll();

    struct ServoStatus {
        int channel;
        int angle;
        bool rotating;
    };
    // 返回全部 16 通道的状态
    std::vector<ServoStatus> GetStatus();

private:
    ServoController() = default;
    ServoController(const ServoController&) = delete;
    ServoController& operator=(const ServoController&) = delete;

    struct ServoState {
        int current_angle = 90;
        int target_angle  = 90;
        int speed         = 0;
        bool rotating     = false;
    };

    static constexpr int kMaxServos         = 16;
    static constexpr int kMaxServoAngle     = 180;
    static constexpr int kTaskUpdateMs      = 10;  // 控制任务周期

    Pca9685*            pca_        = nullptr;
    ServoState          states_[kMaxServos];
    SemaphoreHandle_t   mutex_      = nullptr;
    bool                initialized_= false;

    // FreeRTOS 控制任务
    static void ControlTask(void* arg);
    void UpdateServos();

    // 设置指定通道的目标角度和速度（需持有 mutex_）
    void SetAngle(int channel, int angle, int speed);

    // 角度 → PCA9685 OFF 计数
    static uint16_t AngleToPwm(int angle);
};

#endif // SERVO_CONTROLLER_H
