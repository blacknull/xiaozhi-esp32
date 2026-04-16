#ifndef SERVO_PCA9685_H
#define SERVO_PCA9685_H

#include "boards/common/i2c_device.h"

// PCA9685 16路 PWM 控制器驱动（继承 I2cDevice）
class Pca9685 : public I2cDevice {
public:
    Pca9685(i2c_master_bus_handle_t bus, uint8_t addr = 0x40);

    // 初始化 PCA9685，设置 PWM 频率（Hz），舵机用 50Hz
    void Init(uint8_t freq_hz = 50);

    // 设置指定通道的 PWM OFF 计数（ON 固定为 0）
    // channel: 0-15，off_count: 0-4095
    void SetPwm(uint8_t channel, uint16_t off_count);

private:
    static const uint8_t kMode1     = 0x00;
    static const uint8_t kMode2     = 0x01;
    static const uint8_t kPrescale  = 0xFE;
    static const uint8_t kLed0OnL   = 0x06;
    static const uint8_t kLed0OffL  = 0x08;
};

#endif // SERVO_PCA9685_H
