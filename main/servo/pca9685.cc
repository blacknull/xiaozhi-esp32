#include "servo/pca9685.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char* TAG = "Pca9685";

Pca9685::Pca9685(i2c_master_bus_handle_t bus, uint8_t addr)
    : I2cDevice(bus, addr) {
}

void Pca9685::Init(uint8_t freq_hz) {
    // 延时等待设备稳定
    vTaskDelay(pdMS_TO_TICKS(10));

    // 进入 SLEEP 模式（SLEEP=1），才能设置预分频器
    WriteReg(kMode1, 0x10);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 预分频器 = round(25000000 / (4096 * freq)) - 1
    uint8_t prescale = static_cast<uint8_t>(25000000.0 / (4096.0 * freq_hz) - 1.0 + 0.5);
    ESP_LOGI(TAG, "频率=%dHz 预分频=%d", freq_hz, prescale);
    WriteReg(kPrescale, prescale);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 退出 SLEEP 模式
    WriteReg(kMode1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 使能 Auto-Increment
    WriteReg(kMode1, 0xA0);
    vTaskDelay(pdMS_TO_TICKS(10));

    // OUTDRV=1（推挽输出）
    WriteReg(kMode2, 0x04);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "PCA9685 初始化完成");
}

void Pca9685::SetPwm(uint8_t channel, uint16_t off_count) {
    // 每通道 4 个寄存器：ON_L, ON_H, OFF_L, OFF_H
    // ON 固定为 0（从 tick 0 开始高电平）
    uint8_t base = kLed0OnL + channel * 4;
    WriteReg(base,     0x00);                           // LED_ON_L
    WriteReg(base + 1, 0x00);                           // LED_ON_H
    WriteReg(base + 2, static_cast<uint8_t>(off_count & 0xFF));         // LED_OFF_L
    WriteReg(base + 3, static_cast<uint8_t>((off_count >> 8) & 0x0F)); // LED_OFF_H
}
