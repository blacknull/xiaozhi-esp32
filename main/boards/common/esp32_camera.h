#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    void* operator new(size_t size) {
        // 分配内存时指定使用PSRAM
        void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr == nullptr) {
            // 分配失败时抛出bad_alloc异常（符合C++标准）
            throw std::bad_alloc();
        }
        return ptr;
    }
    void operator delete(void *ptr) noexcept {
        if (ptr != nullptr) {
            // 使用对应的heap_caps_free释放PSRAM内存
            heap_caps_free(ptr);
        }
    }

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
};

#endif // ESP32_CAMERA_H