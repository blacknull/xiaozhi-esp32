#ifndef __LED_STRIP_CONTROLLER_H__
#define __LED_STRIP_CONTROLLER_H__

#include <esp_log.h>
#include "mcp_server.h"
#include "led/circular_strip.h"

class LedStripController {
private:
    CircularStrip* led_strip_ = nullptr;

public:
    LedStripController(CircularStrip* led_strip) {
        led_strip_ = led_strip;

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.led_strip.get_color", 
            "Get the color of every led in the strip.\n"
            "The color of led is the result of mixing three color components with values ranging from 0 to 255,\n"
            "We use \"rgb\" stand for a color, namely r for red components, g for green components, and b for blue components.\n"
            "for example, \"rgb\": [255,0,0] means red, \"rgb\": [0,255,0] means green, and \"rgb\": [255,255,0] means yellow, and so on.",
            PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            std::string result = "{\"led_strip\":{\"led_count\":" + std::to_string(led_strip_->GetLedCount()) + ",\"colors\":[";
            for (int i = 0; i < this->led_strip_->GetLedCount(); i++) {
                auto color = this->led_strip_->GetSingleColor(i);
                result += "{\"index\":" + std::to_string(i) + ",\"rgb\":[" + std::to_string(color.red) + "," + std::to_string(color.green) + "," + std::to_string(color.blue) + "]}";
                if (i < this->led_strip_->GetLedCount() - 1) {
                    result += ",";
                }                
            }
            result += "],";
            return result + "\"message\":\"led strip colors retrieved successfully\"}}";
        });

        mcp_server.AddTool("self.led_strip.set_color", 
            "Set the color of specific led in the strip, index must be between 0 and led_count - 1\n"
            "We use \"rgb\" stand for a color, namely r for red components, g for green components, and b for blue components.\n"
            "for example, set parameters to {\"index\":0,\"rgb\": [255,255,0]} means set the first led to yellow color.",
            PropertyList({
                Property("index", kPropertyTypeInteger, 0, led_strip_->GetLedCount() - 1),
                Property("rgb", kPropertyTypeString)
            }), 
            [this](const PropertyList& properties) -> ReturnValue {
            try {
                ESP_LOGI("led_strip_controller", "set_color called:%s", properties.to_json().c_str());
                const Property& idxProp = properties["index"];
                const Property& rgbProp = properties["rgb"];

                int index = idxProp.value<int>();
                std::string rgb = rgbProp.value<std::string>();
                int r, g, b;
                if (sscanf(rgb.c_str(), "[%d,%d,%d]", &r, &g, &b) != 3) {
                    return "{\"success\": false, \"message\": \"Failed to parse RGB values\"}";
                }
                StripColor color = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
                led_strip_->SetSingleColor(index, color);
                return "{\"success\": true, \"message\": \"LED color set successfully\"}";
            }
            catch (const std::runtime_error& e) {
                return "{\"success\": false, \"message\": \"" + std::string(e.what()) + "\"}";
            }
        });

        mcp_server.AddTool("self.led_strip.set_all_color", 
            "set all color on the led strip with one color.\n"
            "We use \"rgb\" stand for a color, namely r for red components, g for green components, and b for blue components.\n"
            "for example, set parameters to {\"rgb\": [255,255,0]} means set all leds to yellow color.",
            PropertyList({
                Property("rgb", kPropertyTypeString)
            }), 
            [this](const PropertyList& properties) -> ReturnValue {
            try {
                const Property& rgbProp = properties["rgb"];
                std::string rgb = rgbProp.value<std::string>();
                int r, g, b;
                if (sscanf(rgb.c_str(), "[%d,%d,%d]", &r, &g, &b) != 3) {
                    return "{\"success\": false, \"message\": \"Failed to parse RGB values\"}";
                }

                StripColor color = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
                led_strip_->SetAllColor(color);
                return "{\"success\": true, \"message\": \"All LED colors set successfully\"}";
            }
            catch (const std::runtime_error& e) {
                return "{\"success\": false, \"message\": \"" + std::string(e.what()) + "\"}";
            }
        });

        mcp_server.AddTool("self.led_strip.blink", 
            "blink the led strip with specific color and microseconds interval .\n"
            "by default, the interval is 500ms.\n"
            "for example, \"rgb\": [255,255,0] stand for yellow color with 255 red component, 255 green component and 0 blue component.\n",
            PropertyList({
                Property("rgb", kPropertyTypeString),
                Property("interval", kPropertyTypeInteger)
            }), 
            [this](const PropertyList& properties) -> ReturnValue {
            try {
                const Property& rgbProp = properties["rgb"];
                std::string rgb = rgbProp.value<std::string>();
                int r, g, b;
                if (sscanf(rgb.c_str(), "[%d,%d,%d]", &r, &g, &b) != 3) {
                    return "{\"success\": false, \"message\": \"Failed to parse RGB values\"}";
                }
                StripColor color = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};

                const Property& intervalProp = properties["interval"];
                int interval = intervalProp.value<int>();

                led_strip_->Blink(color, interval);
                return "{\"success\": true, \"message\": \"LED strip blinking started successfully\"}";
            }
            catch (const std::runtime_error& e) {
                return "{\"success\": false, \"message\": \"" + std::string(e.what()) + "\"}";
            }
        });

    }
};


#endif // __LED_STRIP_CONTROLLER_H__

