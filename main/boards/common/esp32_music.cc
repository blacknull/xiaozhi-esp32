#include "esp32_music.h"
#include "timer_task_manager.h"
#include "board.h"
#include "system_info.h"
#include "server_config.h"
#include "device_manager.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>  // 为isdigit函数
#include <thread>   // 为线程ID比较
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 重载new运算符：从PSRAM分配内存
void* Esp32Music::operator new(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

// 重载delete运算符：释放PSRAM内存
void Esp32Music::operator delete(void *ptr) noexcept {
    if (ptr != nullptr) {
        heap_caps_free(ptr);
    }
}

#define TAG "Esp32Music"

// ========== 简单的ESP32认证函数 ==========

/**
 * @brief 获取设备MAC地址
 * @return MAC地址字符串
 */
static std::string get_device_mac() {
    return SystemInfo::GetMacAddress();
}

/**
 * @brief 获取设备芯片ID
 * @return 芯片ID字符串
 */
static std::string get_device_chip_id() {
    // 使用MAC地址作为芯片ID，去除冒号分隔符
    std::string mac = SystemInfo::GetMacAddress();
    // 去除所有冒号
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return mac;
}

/**
 * @brief 生成动态密钥
 * @param timestamp 时间戳
 * @return 动态密钥字符串
 */
static std::string generate_dynamic_key(int64_t timestamp) {
    // 密钥（请修改为与服务端一致）
    const std::string secret_key = "your-esp32-secret-key-2024";
    
    // 获取设备信息
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // 组合数据：MAC:芯片ID:时间戳:密钥
    std::string data = mac + ":" + chip_id + ":" + std::to_string(timestamp) + ":" + secret_key;
    
    // SHA256哈希
    unsigned char hash[32];
    mbedtls_sha256((unsigned char*)data.c_str(), data.length(), hash, 0);
    
    // 转换为十六进制字符串（前16字节）
    std::string key;
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", hash[i]);
        key += hex;
    }
    
    return key;
}

/**
 * @brief 为HTTP请求添加认证头
 * @param http HTTP客户端指针
 */
static void add_auth_headers(Http* http) {
    // 获取当前时间戳
    int64_t timestamp = esp_timer_get_time() / 1000000;  // 转换为秒
    
    // 生成动态密钥
    std::string dynamic_key = generate_dynamic_key(timestamp);
    
    // 获取设备信息
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // 添加认证头
    if (http) {
        http->SetHeader("X-MAC-Address", mac);
        http->SetHeader("X-Chip-ID", chip_id);
        http->SetHeader("X-Timestamp", std::to_string(timestamp));
        http->SetHeader("X-Dynamic-Key", dynamic_key);

        auto& device_manager = DeviceManager::GetInstance();
        std::string token = device_manager.GetDeviceToken();
        if (!token.empty()) {
            http->SetHeader("X-Device-Token", token);
        }

        ESP_LOGI(TAG, "Added auth headers - MAC: %s, ChipID: %s, Timestamp: %lld",
                 mac.c_str(), chip_id.c_str(), (long long)timestamp);
    }
}

// URL编码函数
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // 空格编码为'+'或'%20'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

// 在文件开头添加一个辅助函数，统一处理URL构建
static std::string buildUrlWithParams(const std::string& base_url, const std::string& path, const std::string& query) {
    std::string result_url = base_url + path + "?";
    size_t pos = 0;
    size_t amp_pos = 0;
    
    while ((amp_pos = query.find("&", pos)) != std::string::npos) {
        std::string param = query.substr(pos, amp_pos - pos);
        size_t eq_pos = param.find("=");
        
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value = param.substr(eq_pos + 1);
            result_url += key + "=" + url_encode(value) + "&";
        } else {
            result_url += param + "&";
        }
        
        pos = amp_pos + 1;
    }
    
    // 处理最后一个参数
    std::string last_param = query.substr(pos);
    size_t eq_pos = last_param.find("=");
    
    if (eq_pos != std::string::npos) {
        std::string key = last_param.substr(0, eq_pos);
        std::string value = last_param.substr(eq_pos + 1);
        result_url += key + "=" + url_encode(value);
    } else {
        result_url += last_param;
    }
    
    return result_url;
}

Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                         song_name_displayed_(false), current_lyric_url_(), lyrics_(), 
                         current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                         display_mode_(DISPLAY_MODE_LYRICS), is_playing_(false), is_downloading_(false), 
                         play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(), 
                         buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(), 
                         mp3_decoder_initialized_(false) {
    // 在 SRAM 任务（主任务）中预初始化 DeviceManager 单例，
    // 确保后续 PSRAM 栈的音乐线程调用 GetInstance() 时不触发 NVS/SPI Flash 读取
    // （PSRAM 栈任务禁缓存时无法访问 PSRAM，会导致 esp_task_stack_is_sane_cache_disabled 断言失败）
    DeviceManager::GetInstance();
    ESP_LOGI(TAG, "Music player initialized, DeviceManager pre-initialized");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");
    
    // 停止所有操作
    is_downloading_ = false;
    // 通知所有线程退出
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (download_thread_.joinable()) {
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread finished");
    }

    if (play_thread_.joinable()) {
        play_thread_.join();
        ESP_LOGI(TAG, "Playback thread finished");
    }
    
    // 等待歌词线程结束
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }
    
    // 清理缓冲区和MP3解码器
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
    ESP_LOGI(TAG, "Starting to get music details for: %s", song_name.c_str());

    // WiFi 已就绪，确保已拿到设备 token（首次无 token 时从服务器拉取）
    DeviceManager::GetInstance().EnsureToken();
    
    // 清空之前的下载数据
    last_downloaded_data_.clear();
    
    // 保存歌名用于后续显示
    current_song_name_ = song_name;
    
    // 第一步：请求stream_pcm接口获取音频信息
    std::string base_url = MUSIC_SERVER_URL;
    std::string full_url = base_url + "/stream_pcm?song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);
    
    ESP_LOGI(TAG, "Request URL: %s", full_url.c_str());
    
    // 使用Board提供的HTTP客户端
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    // 设置基本请求头
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    // 添加ESP32认证头
    add_auth_headers(http.get());
    http->SetTimeout(15000);  // 元数据请求超时15秒（服务器可能需要查找/转码）
    
    // 打开GET连接
    if (!http->Open("GET", full_url)) {
        ESP_LOGE(TAG, "Failed to connect to music API");
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        return false;
    }
    
    // 读取响应数据
    last_downloaded_data_ = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Complete music details response: %s", last_downloaded_data_.c_str());
    
    // 简单的认证响应检查（可选）
    if (last_downloaded_data_.find("ESP32动态密钥验证失败") != std::string::npos) {
        ESP_LOGE(TAG, "Authentication failed for song: %s", song_name.c_str());
        return false;
    }
    
    if (!last_downloaded_data_.empty()) {
        // 解析响应JSON以提取音频URL
        cJSON* response_json = cJSON_Parse(last_downloaded_data_.c_str());
        if (response_json) {
            // 提取关键信息
            cJSON* artist = cJSON_GetObjectItem(response_json, "artist");
            cJSON* title = cJSON_GetObjectItem(response_json, "title");
            cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON* lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
            cJSON* duration = cJSON_GetObjectItem(response_json, "duration");
            cJSON* data_size = cJSON_GetObjectItem(response_json, "data_size");

            if (cJSON_IsString(artist)) {
                ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
            }
            if (cJSON_IsString(title)) {
                ESP_LOGI(TAG, "Title: %s", title->valuestring);
            }

            // 提取歌曲文件大小和时长（用于下载完整性和播放完成判断）
            expected_data_size_ = 0;
            expected_duration_sec_ = 0;
            if (cJSON_IsNumber(data_size) && data_size->valuedouble > 0) {
                expected_data_size_ = (unsigned long)data_size->valuedouble;
                ESP_LOGI(TAG, "Expected data size: %lu bytes", expected_data_size_);
            }
            if (cJSON_IsNumber(duration) && duration->valuedouble > 0) {
                expected_duration_sec_ = (int)duration->valuedouble;
                ESP_LOGI(TAG, "Expected duration: %d seconds", expected_duration_sec_);
            }
            
            // 检查audio_url是否有效
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                ESP_LOGI(TAG, "Audio URL path: %s", audio_url->valuestring);
                
                // 第二步：处理音频下载URL
                std::string audio_path = audio_url->valuestring;
                
                // 检查是否已经是完整URL（以http://或https://开头）
                if (audio_path.find("http://") == 0 || audio_path.find("https://") == 0) {
                    // 已经是完整URL，直接使用
                    current_music_url_ = audio_path;
                } else {
                    // 是相对路径，需要拼接base_url
                    if (audio_path.find("?") != std::string::npos) {
                        size_t query_pos = audio_path.find("?");
                        std::string path = audio_path.substr(0, query_pos);
                        std::string query = audio_path.substr(query_pos + 1);
                        current_music_url_ = buildUrlWithParams(base_url, path, query);
                    } else {
                        current_music_url_ = base_url + audio_path;
                    }
                }
                
                ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
                ESP_LOGI(TAG, "Starting streaming playback for: %s", song_name.c_str());
                ESP_LOGI(TAG, "Full music URL: %s", current_music_url_.c_str());
                song_name_displayed_ = false;  // 重置歌名显示标志
                StartStreaming(current_music_url_);
                
                // 处理歌词URL - 只有在歌词显示模式下才启动歌词
                if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                    // 处理歌词下载URL
                    std::string lyric_path = lyric_url->valuestring;
                    
                    // 检查是否已经是完整URL
                    if (lyric_path.find("http://") == 0 || lyric_path.find("https://") == 0) {
                        current_lyric_url_ = lyric_path;
                    } else {
                        // 是相对路径，需要拼接base_url
                        if (lyric_path.find("?") != std::string::npos) {
                            size_t query_pos = lyric_path.find("?");
                            std::string path = lyric_path.substr(0, query_pos);
                            std::string query = lyric_path.substr(query_pos + 1);
                            current_lyric_url_ = buildUrlWithParams(base_url, path, query);
                        } else {
                            current_lyric_url_ = base_url + lyric_path;
                        }
                    }
                    ESP_LOGI(TAG, "Full lyric URL: %s", current_lyric_url_.c_str());
                    
                    // 根据显示模式决定是否启动歌词
                    if (display_mode_ == DISPLAY_MODE_LYRICS) {
                        ESP_LOGI(TAG, "Loading lyrics for: %s (lyrics display mode)", song_name.c_str());
                        
                        // 先确保之前的歌词线程已停止
                        if (is_lyric_running_) {
                            is_lyric_running_ = false;
                        }
                        // 等待旧线程结束
                        if (lyric_thread_.joinable()) {
                            lyric_thread_.join();
                        }
                        
                        // 重置歌词状态
                        is_lyric_running_ = true;
                        current_lyric_index_ = -1;
                        lyrics_.clear();
                        
                        // 创建新歌词线程
                        try {
                            lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                        } catch (const std::exception& e) {
                            ESP_LOGE(TAG, "Failed to create lyric thread: %s", e.what());
                            is_lyric_running_ = false;
                        }
                    } else {
                        ESP_LOGI(TAG, "Lyric URL found but spectrum display mode is active, skipping lyrics");
                    }
                } else {
                    ESP_LOGW(TAG, "No lyric URL found for this song");
                }
                
                cJSON_Delete(response_json);
                return true;
            } else {
                // audio_url为空或无效
                ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
                ESP_LOGE(TAG, "Failed to find music: 没有找到歌曲 '%s'", song_name.c_str());
                cJSON_Delete(response_json);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "Failed to parse JSON response");
        }
    } else {
        ESP_LOGE(TAG, "Empty response from music API");
    }
    
    return false;
}



std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

// 开始流式播放
bool Esp32Music::StartStreaming(const std::string& music_url) {
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());
    
    // 使用互斥锁保护线程操作
    std::lock_guard<std::mutex> thread_lock(thread_ops_mutex_);
    
    // 停止之前的播放和下载
    is_downloading_ = false;
    is_playing_ = false;
    
    // 等待之前的线程完全结束
    if (download_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // 通知线程退出
        }
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // 通知线程退出
        }
        play_thread_.join();
    }
    
    // 清空缓冲区
    ClearAudioBuffer();
    
    // 重置播放完成检测状态
    was_playing_ = false;
    normal_completion_ = false;
    completion_triggered_ = false;
    
    // 为每个线程分别设置 pthread 配置：将栈分配到 PSRAM 节省 SRAM
    is_downloading_ = true;
    {
        esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
        cfg.stack_size = 8192;
        cfg.prio = 5;
        cfg.thread_name = "music_dl";
        cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        esp_pthread_set_cfg(&cfg);
        download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    }
    
    // 开始播放线程（会等待缓冲区有足够数据）
    is_playing_ = true;
    {
        esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
        cfg.stack_size = 8192;
        cfg.prio = 5;
        cfg.thread_name = "music_play";
        cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        esp_pthread_set_cfg(&cfg);
        play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    }
    
    ESP_LOGI(TAG, "Streaming threads started successfully with PSRAM stack");
    
    return true;
}

// 停止流式播放
bool Esp32Music::StopStreaming() {
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d", 
            is_downloading_.load(), is_playing_.load());

    // 删除播放完成监控定时器（被打断时不应触发评论）
    uint32_t timer_id = music_monitor_timer_id_.exchange(0);
    if (timer_id != 0) {
        TimerTaskManager::GetInstance().DeleteTask(timer_id);
        ESP_LOGI(TAG, "Deleted music monitor timer %u", timer_id);
    }

    // 重置采样率到原始值
    ResetSampleRate();

    // 检查是否有流式播放正在进行
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }

    // 停止下载和播放标志
    is_downloading_ = false;
    is_playing_ = false;
    
    // 清空歌名显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");  // 清空歌名显示
        ESP_LOGI(TAG, "Cleared song name display");
    }
    
    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // 等待线程结束（避免重复代码，让StopStreaming也能等待线程完全停止）
    if (download_thread_.joinable()) {
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread joined in StopStreaming");
    }
    
    // 等待播放线程结束
    // 注意：is_playing_ 已在上方设为 false，buffer_cv_ 已 notify_all
    // 播放线程检测到 is_playing_ == false 后会很快退出
    if (play_thread_.joinable()) {
        play_thread_.join();
        ESP_LOGI(TAG, "Play thread joined in StopStreaming");
    }
    
    // 停止歌词线程
    if (is_lyric_running_) {
        is_lyric_running_ = false;
    }
    if (lyric_thread_.joinable()) {
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread joined in StopStreaming");
    }
    
    // 在线程完全结束后，只在频谱模式下停止FFT显示
    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
        display->stopFft();
        ESP_LOGI(TAG, "Stopped FFT display in StopStreaming (spectrum mode)");
    } else if (display) {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop in StopStreaming");
    }
    
    ESP_LOGI(TAG, "Music streaming stop signal sent");
    return true;
}

// 计算 MP3 Layer3 帧字节数（用于双同步验证，排除假同步字）
// 返回 0 表示帧头无效
int Esp32Music::CalcMp3FrameSize(const uint8_t* hdr) {
    if (!hdr || hdr[0] != 0xFF || (hdr[1] & 0xE0) != 0xE0) return 0;

    int version_bits = (hdr[1] >> 3) & 0x3;  // 11=MPEG1 10=MPEG2 00=MPEG2.5 01=reserved
    int layer_bits   = (hdr[1] >> 1) & 0x3;  // 01=LayerIII(MP3)
    if (layer_bits != 1 || version_bits == 1) return 0;

    int bitrate_idx = (hdr[2] >> 4) & 0xF;
    if (bitrate_idx == 0 || bitrate_idx == 15) return 0;  // free/bad

    int srate_idx = (hdr[2] >> 2) & 0x3;
    if (srate_idx == 3) return 0;  // reserved

    int padding = (hdr[2] >> 1) & 0x1;

    // 比特率表 (kbps)
    static const int br_v1[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int br_v2[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    // 采样率表 (Hz)
    static const int sr[][3] = {{44100,48000,32000},{22050,24000,16000},{11025,12000,8000}};

    int ver_idx = (version_bits == 3) ? 0 : (version_bits == 2) ? 1 : 2;
    int bitrate_kbps = (version_bits == 3) ? br_v1[bitrate_idx] : br_v2[bitrate_idx];
    // MPEG1 Layer3: 1152 样本/帧; MPEG2/2.5 Layer3: 576 样本/帧
    int spf = (version_bits == 3) ? 1152 : 576;  // samples per frame

    int srate = sr[ver_idx][srate_idx];
    if (bitrate_kbps == 0 || srate == 0) return 0;

    return (spf / 8) * (bitrate_kbps * 1000) / srate + padding;
}

// 检查播放完成状态（由 TimerManager 定期调用）
bool Esp32Music::CheckPlaybackCompleted(std::string& out_song_name) {
    bool currently_playing = is_playing_.load();
    bool was_playing_before = was_playing_.load();
    bool completed_normally = normal_completion_.load();
    bool already_triggered = completion_triggered_.load();
    
    // 更新 was_playing_ 状态
    if (currently_playing) {
        was_playing_ = true;
        normal_completion_ = false;
        completion_triggered_ = false;
    }
    
    // 检测播放完成：之前正在播放，现在不在播放
    if (was_playing_before && !currently_playing) {
        if (completed_normally && !already_triggered) {
            completion_triggered_ = true;
            out_song_name = current_song_name_;
            ESP_LOGI(TAG, "Playback completed detected for: %s", out_song_name.c_str());
            return true;
        }
        // 重置状态
        was_playing_ = false;
    }
    return false;
}

// 流式下载音频数据
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str());

    // 验证URL有效性
    if (music_url.empty() || music_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }

    // 断点续传重试逻辑
    // 服务器使用 chunked transfer encoding，HTTP 客户端的 ParseChunkSize 经常在
    // 遇到空行时返回 0（被当作结束标记），导致下载在几十KB处截断。
    // 解决方案：检测到截断后，使用 Range 请求从断点处继续下载。
    const int MAX_RETRIES = 5;
    int attempt = 0;
    unsigned long total_downloaded = 0;
    // 优先使用服务器 JSON 中提供的 data_size，作为预期文件大小
    unsigned long expected_content_length = expected_data_size_;
    bool download_success = false;
    bool format_checked = false;

    if (expected_content_length > 0) {
        ESP_LOGI(TAG, "Using server-provided data_size as expected length: %lu bytes", expected_content_length);
    }

    // 音乐文件的最小合理大小（用于无任何大小信息时的启发式判断）
    // 一首30秒的128kbps MP3 ≈ 480KB，正常歌曲通常 > 1MB
    static const unsigned long MIN_MUSIC_FILE_SIZE = 512 * 1024;  // 512KB

    for (attempt = 0; attempt <= MAX_RETRIES; attempt++) {
        if (!is_downloading_) break;  // 被外部停止

        if (attempt > 0) {
            ESP_LOGI(TAG, "Resuming download from byte %lu (attempt %d/%d)",
                     total_downloaded, attempt + 1, MAX_RETRIES + 1);
            vTaskDelay(pdMS_TO_TICKS(500));  // 重试前短暂等待
        }

        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);

        // 设置基本请求头
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "*/*");
        http->SetHeader("Accept-Encoding", "identity");  // 明确禁止内容编码
        http->SetHeader("Connection", "close");  // 尝试避免 chunked encoding

        // 断点续传：如果之前已下载了部分数据，使用 Range 请求继续
        if (total_downloaded > 0) {
            char range_header[64];
            snprintf(range_header, sizeof(range_header), "bytes=%lu-", total_downloaded);
            http->SetHeader("Range", range_header);
            ESP_LOGI(TAG, "Using Range header: %s", range_header);
        }

        // 添加ESP32认证头
        add_auth_headers(http.get());

        if (!http->Open("GET", music_url)) {
            ESP_LOGE(TAG, "Failed to connect to music stream URL");
            if (attempt < MAX_RETRIES) continue;
            is_downloading_ = false;
            return;
        }

        int status_code = http->GetStatusCode();
        if (status_code != 200 && status_code != 206) {
            ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
            http->Close();
            if (status_code == 416 && total_downloaded > 0) {
                // 416 Range Not Satisfiable — 请求范围超出文件大小，说明已下载完成
                ESP_LOGI(TAG, "Range returned 416, download complete (%lu bytes)", total_downloaded);
                download_success = true;
                break;
            }
            if (attempt < MAX_RETRIES) continue;
            is_downloading_ = false;
            return;
        }

        // 重试时发送了 Range 请求，但服务器返回 200（不支持 Range）而非 206：
        // 服务器会从字节 0 重新发送完整文件，必须清空已有缓冲区，从头开始
        if (total_downloaded > 0 && status_code == 200) {
            ESP_LOGW(TAG, "Server returned 200 instead of 206, Range not supported. "
                     "Clearing buffer and restarting (%lu bytes discarded)", total_downloaded);
            ClearAudioBuffer();
            total_downloaded = 0;
            format_checked = false;
            expected_content_length = 0;
        }

        // 获取预期文件大小
        if (expected_content_length == 0) {
            // 从 Content-Length 响应头获取（非 chunked 时有效）
            std::string cl_str = http->GetResponseHeader("content-length");
            if (!cl_str.empty()) {
                expected_content_length = strtoul(cl_str.c_str(), nullptr, 10);
            }
            if (expected_content_length == 0) {
                expected_content_length = (unsigned long)http->GetBodyLength();
            }
        }

        // 对于206响应，从 Content-Range 获取完整文件大小
        if (status_code == 206) {
            std::string content_range = http->GetResponseHeader("content-range");
            if (!content_range.empty()) {
                ESP_LOGI(TAG, "Content-Range: %s", content_range.c_str());
                // 格式：bytes start-end/total
                size_t slash_pos = content_range.find('/');
                if (slash_pos != std::string::npos) {
                    std::string total_str = content_range.substr(slash_pos + 1);
                    if (total_str != "*") {
                        unsigned long total_size = strtoul(total_str.c_str(), nullptr, 10);
                        if (total_size > expected_content_length) {
                            expected_content_length = total_size;
                        }
                    }
                }
            }
        }

        // 检查 Transfer-Encoding 以了解是否为 chunked 响应
        std::string te = http->GetResponseHeader("transfer-encoding");
        bool is_chunked = (te.find("chunked") != std::string::npos);

        ESP_LOGI(TAG, "Download started: status=%d, attempt=%d, offset=%lu, expected=%lu, chunked=%d",
                 status_code, attempt + 1, total_downloaded, expected_content_length, is_chunked);

        // 分块读取音频数据
        const int read_chunk_size = 4096;  // 4KB每块
        char* buffer = (char*)heap_caps_malloc(read_chunk_size, MALLOC_CAP_SPIRAM);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate memory for download buffer");
            http->Close();
            is_downloading_ = false;
            return;
        }

        unsigned long bytes_this_attempt = 0;
        bool read_error = false;

        while (is_downloading_) {
            int bytes_read = http->Read(buffer, read_chunk_size);
            if (bytes_read < 0) {
                ESP_LOGE(TAG, "Read error %d (this_attempt=%lu, total=%lu)",
                         bytes_read, bytes_this_attempt, total_downloaded);
                read_error = true;
                break;
            }
            if (bytes_read == 0) {
                // 连接关闭 — 判断是正常完成还是 chunked 解析错误导致的截断
                bool likely_complete = false;

                if (expected_content_length > 0) {
                    // 有预期大小：对比实际下载量
                    if (total_downloaded >= expected_content_length) {
                        likely_complete = true;
                    } else {
                        ESP_LOGW(TAG, "Premature close: %lu/%lu bytes", total_downloaded, expected_content_length);
                        read_error = true;
                    }
                } else if (is_chunked) {
                    // chunked 响应无 Content-Length：用启发式判断
                    // 如果下载量太少（< 512KB），很可能是 chunked 解析错误
                    if (total_downloaded < MIN_MUSIC_FILE_SIZE) {
                        ESP_LOGW(TAG, "Chunked response ended too early: %lu bytes (min expected %lu)",
                                 total_downloaded, MIN_MUSIC_FILE_SIZE);
                        read_error = true;
                    } else {
                        // 下载了足够多数据，认为可能完成了
                        likely_complete = true;
                    }
                } else {
                    // 非 chunked 且无 Content-Length：信任 EOF
                    likely_complete = true;
                }

                if (likely_complete) {
                    ESP_LOGI(TAG, "Download completed: %lu bytes", total_downloaded);
                    download_success = true;
                }
                break;
            }
            bytes_this_attempt += bytes_read;

            // 检测文件格式（仅首次）
            if (!format_checked && bytes_read >= 16) {
                format_checked = true;
                ESP_LOGI(TAG, "First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        (unsigned char)buffer[0], (unsigned char)buffer[1],
                        (unsigned char)buffer[2], (unsigned char)buffer[3],
                        (unsigned char)buffer[4], (unsigned char)buffer[5],
                        (unsigned char)buffer[6], (unsigned char)buffer[7],
                        (unsigned char)buffer[8], (unsigned char)buffer[9],
                        (unsigned char)buffer[10], (unsigned char)buffer[11],
                        (unsigned char)buffer[12], (unsigned char)buffer[13],
                        (unsigned char)buffer[14], (unsigned char)buffer[15]);

                // 检查ID3标签
                size_t header_offset = 0;
                if (memcmp(buffer, "ID3", 3) == 0) {
                    uint32_t id3_size = ((uint32_t)(buffer[6] & 0x7F) << 21) |
                                       ((uint32_t)(buffer[7] & 0x7F) << 14) |
                                       ((uint32_t)(buffer[8] & 0x7F) << 7)  |
                                       ((uint32_t)(buffer[9] & 0x7F));
                    header_offset = 10 + id3_size;
                    ESP_LOGI(TAG, "ID3v2 tag detected, size=%u bytes", (unsigned int)header_offset);
                }

                // 检测格式
                uint8_t* audio_start = (uint8_t*)buffer + header_offset;
                if (audio_start[0] == 0xFF && (audio_start[1] & 0xE0) == 0xE0) {
                    ESP_LOGI(TAG, "Format: VALID MP3");
                } else if (memcmp(audio_start, "RIFF", 4) == 0) {
                    ESP_LOGI(TAG, "Format: WAV");
                } else {
                    ESP_LOGW(TAG, "Format: UNKNOWN");
                }
            }

            // 创建音频数据块
            uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
            if (!chunk_data) {
                ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
                read_error = true;
                break;
            }
            memcpy(chunk_data, buffer, bytes_read);

            // 等待缓冲区有空间
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });

                if (is_downloading_) {
                    audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                    buffer_size_ += bytes_read;
                    total_downloaded += bytes_read;

                    // 通知播放线程有新数据
                    buffer_cv_.notify_one();

                    // 每 64KB 打印一次进度
                    const unsigned long LOG_INTERVAL = 64 * 1024;
                    if (total_downloaded / LOG_INTERVAL != (total_downloaded - bytes_read) / LOG_INTERVAL) {
                        ESP_LOGI(TAG, "Downloaded %lu bytes, buffer: %lu",
                                 total_downloaded, (unsigned long)buffer_size_);
                    }
                } else {
                    heap_caps_free(chunk_data);
                    break;
                }
            }
        }  // end while (is_downloading_)

        heap_caps_free(buffer);
        http->Close();

        // 判断是否需要重试
        if (download_success) {
            ESP_LOGI(TAG, "Download completed successfully: %lu bytes", total_downloaded);
            break;
        }

        if (!is_downloading_) {
            ESP_LOGI(TAG, "Download stopped by user");
            break;
        }

        // 下载异常中断，决定是否重试
        if (total_downloaded == 0) {
            ESP_LOGW(TAG, "Got 0 bytes (attempt %d/%d), will retry from scratch",
                     attempt + 1, MAX_RETRIES + 1);
        } else if (expected_content_length > 0 && total_downloaded < expected_content_length) {
            // 已知预期大小，未下完 — Range 断点续传（保留缓冲区）
            ESP_LOGW(TAG, "Incomplete: %lu/%lu bytes (%.0f%%), will resume",
                     total_downloaded, expected_content_length,
                     (float)total_downloaded * 100.0f / (float)expected_content_length);
        } else if (total_downloaded < MIN_MUSIC_FILE_SIZE) {
            // 无 Content-Length 且下载量极少 — 大概率 chunked 解析失败
            // 尝试 Range 断点续传（保留已下载数据，播放线程可以先消费这部分）
            ESP_LOGW(TAG, "Too little data: %lu bytes, will resume from offset", total_downloaded);
        } else {
            // 下载了较多数据后中断，尝试续传获取剩余部分
            ESP_LOGW(TAG, "Interrupted at %lu bytes, will try to resume", total_downloaded);
        }
    }  // end for (attempt)

    is_downloading_ = false;

    // 通知播放线程下载完成
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (expected_content_length > 0 && total_downloaded < expected_content_length) {
        ESP_LOGW(TAG, "Download incomplete: %lu/%lu bytes (%.0f%%)",
                 total_downloaded, expected_content_length,
                 (float)total_downloaded * 100.0f / (float)expected_content_length);
    }

    ESP_LOGI(TAG, "Download thread finished, total: %lu bytes after %d attempts",
             total_downloaded, attempt + 1);
}

// 流式播放音频数据
void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting audio stream playback");
    
    // 初始化时间跟踪变量
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        return;
    }

    // 等待 codec 输出可用（可能正被 TTS 占用）
    if (!codec->output_enabled()) {
        ESP_LOGI(TAG, "Waiting for audio codec output to become available...");
        int wait_ms = 0;
        const int max_wait_ms = 10000; // 最多等待10秒
        while (!codec->output_enabled() && is_playing_ && wait_ms < max_wait_ms) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_ms += 100;
        }
        if (!codec->output_enabled()) {
            ESP_LOGE(TAG, "Audio codec output not enabled after %dms", wait_ms);
            is_playing_ = false;
            return;
        }
        ESP_LOGI(TAG, "Audio codec output became available after %dms", wait_ms);
    }
    
    if (!mp3_decoder_initialized_) {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        is_playing_ = false;
        return;
    }
    
    
    // 等待初始缓冲区填充后再开始播放，最多等待8秒
    // 目的：避免下载速度暂时慢于解码速度时 bit reservoir 不足导致开头卡顿和decode错误
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        bool reached = buffer_cv_.wait_until(lock, deadline, [this] {
            return buffer_size_ >= INITIAL_BUFFER_SIZE
                || !is_downloading_   // 下载结束（含0字节情形）
                || !is_playing_;      // 被 StopStreaming/StartStreaming 外部停止
        });
        if (!reached) {
            ESP_LOGI(TAG, "Initial buffer wait timed out (8s), starting with %d bytes buffered", buffer_size_);
        } else {
            ESP_LOGI(TAG, "Initial buffer ready: %d bytes buffered", buffer_size_);
        }
    }
    
    // 下载结束但缓冲区为空（如服务器返回0字节流）或被停止，直接退出
    if (!is_playing_ || (!is_downloading_ && buffer_size_ == 0)) {
        ESP_LOGW(TAG, "Aborting playback: playing=%d, downloading=%d, buffer=%d",
                 is_playing_.load(), is_downloading_.load(), buffer_size_);
        is_playing_ = false;
        return;
    }

    ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);
    
    size_t total_played = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    int consecutive_errors = 0;
    
    // 分配MP3输入缓冲区（16KB，减少帧跨越缓冲区边界的概率）
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }
    
    // 预分配PCM缓冲区，避免每帧重复malloc/free造成PSRAM碎片
    int16_t* pcm_buffer = (int16_t*)heap_caps_malloc(2304 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!pcm_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        heap_caps_free(mp3_input_buffer);
        is_playing_ = false;
        return;
    }
    
    // 标记是否已经处理过ID3标签
    bool id3_processed = false;
    
    while (is_playing_) {
        // 检查设备状态，只有在空闲状态才播放音乐
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        // 等小智把话说完了，变成聆听状态之后，马上转成待机状态，进入音乐播放
        if (current_state == kDeviceStateListening) {
            ESP_LOGI(TAG, "Device is in listening state, switching to idle state for music playback");
            // 切换状态
            app.ToggleChatState(); // 变成待机状态
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        } else if (current_state != kDeviceStateIdle) { // 不是待机状态，就一直卡在这里，不让播放音乐
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            // 如果不是空闲状态，暂停播放
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // 设备状态检查通过，显示当前播放的歌名
        if (!song_name_displayed_ && !current_song_name_.empty()) {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display) {
                // 格式化歌名显示为《歌名》播放中...
                std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
                display->SetMusicInfo(formatted_song_name.c_str());
                ESP_LOGI(TAG, "Displaying song name: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }

            // 根据显示模式启动相应的显示功能
            if (display) {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                    display->start();
                    ESP_LOGI(TAG, "Display start() called for spectrum visualization");
                } else {
                    ESP_LOGI(TAG, "Lyrics display mode active, FFT visualization disabled");
                }
            }
        }
        
        // 当解码缓冲区数据不足时，循环从队列拉取 chunk 尽量填满缓冲区（16KB）。
        // 原来只拉一个 4KB chunk（且 space_available 硬编码 8192 导致只用了半个缓冲区），
        // 在网络抖动时极易产生 underrun。改为：
        //   1. 阈值提高到 8192，提前补充
        //   2. 循环拉取直到缓冲区足够满或队列暂无数据
        //   3. 等待改用 wait_for(50ms)，超时后用剩余数据继续解码而不阻塞
        while (bytes_left < 8192) {
            AudioChunk chunk;
            bool got_chunk = false;

            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (!audio_buffer_.empty()) {
                    chunk = audio_buffer_.front();
                    audio_buffer_.pop();
                    buffer_size_ -= chunk.size;
                    buffer_cv_.notify_one();
                    got_chunk = true;
                } else if (!is_downloading_) {
                    // 下载完成且队列为空，播放结束
                    if (bytes_left == 0) {
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
                        goto playback_done;
                    }
                    break;
                } else {
                    // 队列暂无数据，带超时等待，超时后先用剩余数据继续解码
                    buffer_cv_.wait_for(lock, std::chrono::milliseconds(50),
                        [this] { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty()) break;
                    continue;
                }
            }

            if (got_chunk && chunk.data && chunk.size > 0) {
                // 移动剩余数据到缓冲区开头
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                // 使用完整的 16384 字节缓冲区
                size_t space_available = 16384 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;

                // 检查并跳过ID3标签（仅在开始时处理一次）
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }
                heap_caps_free(chunk.data);
            }
        }
        
        // 尝试找到MP3帧同步
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }
        
        // 跳过到同步位置
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }

        // 双同步验证：计算当前帧的预期字节长度，检查下一帧起始处是否也有有效同步字
        // 目的：过滤掉 main_data 内部出现的假 0xFFEx 字节序列，避免触发 -1/-6 错误级联
        // 仅在有足够数据（>= frame_size + 2）时才校验；数据不足时放行，由解码器处理
        if (bytes_left >= 4) {
            int fsize = CalcMp3FrameSize(read_ptr);
            if (fsize > 0 && bytes_left > fsize + 1) {
                uint8_t n0 = read_ptr[fsize];
                uint8_t n1 = read_ptr[fsize + 1];
                if (n0 != 0xFF || (n1 & 0xE0) != 0xE0) {
                    // 下一帧位置没有合法同步字 → 当前是假同步，跳过
                    ESP_LOGD(TAG, "False sync at offset, skipping (fsize=%d)", fsize);
                    int next_sync = MP3FindSyncWord(read_ptr + 1, bytes_left - 1);
                    if (next_sync >= 0) {
                        read_ptr  += 1 + next_sync;
                        bytes_left -= 1 + next_sync;
                    } else {
                        bytes_left = 0;
                    }
                    continue;
                }
            }
        }

        // 检测并跳过 Xing/LAME/Info VBR 信息帧（通常是ID3之后的第一帧）
        // 这类帧包含VBR元数据而非音频，解码后输出垃圾PCM，是起始破音的来源
        if (total_frames_decoded_ < 5 && bytes_left >= 36) {
            uint8_t hdr1 = read_ptr[1];
            uint8_t hdr3 = read_ptr[3];
            bool is_mpeg1 = (hdr1 & 0x18) == 0x18;
            bool is_mono  = (hdr3 & 0xC0) == 0xC0;
            int side_info_size = is_mpeg1 ? (is_mono ? 17 : 32) : (is_mono ? 9 : 17);
            int tag_offset = 4 + side_info_size;
            if (bytes_left >= tag_offset + 4) {
                const char* tag = (const char*)(read_ptr + tag_offset);
                if (memcmp(tag, "Xing", 4) == 0 || memcmp(tag, "Info", 4) == 0 ||
                    memcmp(tag, "VBRI", 4) == 0) {
                    ESP_LOGI(TAG, "Skipping Xing/Info/VBRI VBR frame at frame %d", total_frames_decoded_);
                    MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
                    CleanupMp3Decoder();
                    InitializeMp3Decoder();
                    continue;
                }
            }
        }
        
        // 解码MP3帧（使用预分配的pcm_buffer）

        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // 解码成功，获取帧信息
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;
            
            // 基本的帧信息有效性检查，防止除零错误
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping", 
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // 计算当前帧的持续时间(毫秒)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) / 
                                  (mp3_frame_info_.samprate * mp3_frame_info_.nChans);
            
            // 更新当前播放时间
            current_play_time_ms_ += frame_duration_ms;
            
            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d", 
                    total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                    mp3_frame_info_.samprate, mp3_frame_info_.nChans);
            
            // 更新歌词显示
            int buffer_latency_ms = 600; // 实测调整值
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);
            
            // 将PCM数据发送到Application的音频解码队列
            if (mp3_frame_info_.outputSamps > 0) {
                int16_t* final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;
                
                // 如果是双通道，转换为单通道混合
                if (mp3_frame_info_.nChans == 2) {
                    // 双通道转单通道：将左右声道混合
                    int stereo_samples = mp3_frame_info_.outputSamps;  // 包含左右声道的总样本数
                    int mono_samples = stereo_samples / 2;  // 实际的单声道样本数
                    
                    mono_buffer.resize(mono_samples);
                    
                    for (int i = 0; i < mono_samples; ++i) {
                        // 混合左右声道 (L + R) / 2
                        int left = pcm_buffer[i * 2];      // 左声道
                        int right = pcm_buffer[i * 2 + 1]; // 右声道
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples", 
                            stereo_samples, mono_samples);
                } else if (mp3_frame_info_.nChans == 1) {
                    // 已经是单声道，无需转换
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                } else {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono", 
                            mp3_frame_info_.nChans);
                }
                
                // 创建AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60;  // 使用Application默认的帧时长
                packet.timestamp = 0;
                
                // 将int16_t PCM数据转换为uint8_t字节数组
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                // FFT 缓冲区：需要检查大小是否足够，不够则重新分配
                if (final_pcm_data_fft != nullptr && fft_buffer_samples_ < final_sample_count) {
                    heap_caps_free(final_pcm_data_fft);
                    final_pcm_data_fft = nullptr;
                }
                if (final_pcm_data_fft == nullptr) {
                    final_pcm_data_fft = (int16_t*)heap_caps_malloc(
                        final_sample_count * sizeof(int16_t),
                        MALLOC_CAP_SPIRAM
                    );
                    fft_buffer_samples_ = final_sample_count;
                }

                if (final_pcm_data_fft != nullptr) {
                    memcpy(
                        final_pcm_data_fft,
                        final_pcm_data,
                        final_sample_count * sizeof(int16_t)
                    );
                }
                
                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application", 
                        final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                
                // 发送到Application的音频解码队列
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;
                
                // 打印播放进度
                if (total_played % (128 * 1024) == 0) {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
            
        } else {
            // 解码失败
            consecutive_errors++;
            ESP_LOGW(TAG, "MP3 decode failed with error: %d (consecutive: %d)", decode_result, consecutive_errors);

            if (decode_result == -2) {
                // ERR_MP3_MAINDATA_UNDERFLOW：bit reservoir 不足（流开始阶段的正常现象）
                // 【不能重置解码器】——重置会清空 reservoir，下一帧 reservoir 仍为空，
                // 继续 -2，造成无限循环。让 reservoir 随解码进行自然积累即可。
                // 仅跳到下一帧避免在 main_data 内的假同步字上死循环。
                if (bytes_left > 1) {
                    int next_sync = MP3FindSyncWord(read_ptr + 1, bytes_left - 1);
                    if (next_sync >= 0) {
                        read_ptr += 1 + next_sync;
                        bytes_left -= 1 + next_sync;
                    } else {
                        bytes_left = 0;
                    }
                } else {
                    bytes_left = 0;
                }
            } else {
                // -1 (INDATA_UNDERFLOW) / -6 (INVALID_FRAMEHEADER) / -9 (INVALID_HUFFCODES) 等：
                // 这些错误可能导致 reservoir 被污染，下一帧用旧 reservoir 数据会产生破音
                // 立即重置解码器，代价是短暂静音，但避免输出垃圾PCM
                CleanupMp3Decoder();
                InitializeMp3Decoder();
                consecutive_errors = 0;

                // 跳到下一个有效sync word
                if (bytes_left > 1) {
                    int next_sync = MP3FindSyncWord(read_ptr + 1, bytes_left - 1);
                    if (next_sync >= 0) {
                        read_ptr += 1 + next_sync;
                        bytes_left -= 1 + next_sync;
                    } else {
                        bytes_left = 0;
                    }
                } else {
                    bytes_left = 0;
                }
            }
        }
    }
    playback_done:
    // 清理
    if (pcm_buffer) {
        heap_caps_free(pcm_buffer);
    }
    if (mp3_input_buffer) {
        heap_caps_free(mp3_input_buffer);
    }
    if (final_pcm_data_fft) {
        heap_caps_free(final_pcm_data_fft);
        final_pcm_data_fft = nullptr;
        fft_buffer_samples_ = 0;
    }
    
    // 播放结束时进行基本清理，但不调用StopStreaming避免线程自我等待
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes, play time: %lld ms",
             total_played, current_play_time_ms_);
    ESP_LOGI(TAG, "Performing basic cleanup from play thread");

    // 判断是否为真正的正常播放完成
    // 条件1：is_playing_ 仍然为 true（不是被 StopStreaming 打断）
    // 条件2：如果服务器提供了 data_size/duration，验证播放量是否达到合理比例
    if (is_playing_.load()) {
        bool genuinely_complete = true;

        // 用服务器提供的 duration 验证（允许 70% 容差，因为 MP3 VBR 时长计算可能有偏差）
        if (expected_duration_sec_ > 0) {
            int played_sec = (int)(current_play_time_ms_ / 1000);
            int min_expected_sec = expected_duration_sec_ * 70 / 100;
            if (played_sec < min_expected_sec) {
                ESP_LOGW(TAG, "Playback too short: %d/%d sec (min %d sec), likely incomplete download",
                         played_sec, expected_duration_sec_, min_expected_sec);
                genuinely_complete = false;
            }
        }

        if (genuinely_complete) {
            normal_completion_ = true;
            ESP_LOGI(TAG, "Playback completed normally, will trigger review");
        } else {
            ESP_LOGW(TAG, "Buffer exhausted but playback incomplete (download truncated)");
        }
    } else {
        ESP_LOGI(TAG, "Playback was interrupted externally");
    }
    
    // 停止播放标志
    is_playing_ = false;
    
    // 只在频谱显示模式下才停止FFT显示
    if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            display->stopFft();
            ESP_LOGI(TAG, "Stopped FFT display from play thread (spectrum mode)");
        }
    } else {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop");
    }
}

// 清空音频缓冲区
void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// 初始化MP3解码器
bool Esp32Music::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// 清理MP3解码器
void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// 重置采样率到原始值
void Esp32Music::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 && 
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
        ESP_LOGI(TAG, "重置采样率：从 %d Hz 重置到原始值 %d Hz", 
                codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1)) {  // -1 表示重置到原始值
            ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
        } else {
            ESP_LOGW(TAG, "无法重置采样率到原始值");
        }
    }
}

// 跳过MP3文件开头的ID3标签
size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // 检查ID3v2标签头 "ID3"
    if (memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    
    // 计算标签大小（synchsafe integer格式）
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    
    // ID3v2头部(10字节) + 标签内容
    size_t total_skip = 10 + tag_size;
    
    // 确保不超过可用数据大小
    if (total_skip > size) {
        total_skip = size;
    }
    
    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// 下载歌词
bool Esp32Music::DownloadLyrics(const std::string& lyric_url) {
    ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str());
    
    // 检查URL是否为空
    if (lyric_url.empty()) {
        ESP_LOGE(TAG, "Lyric URL is empty!");
        return false;
    }
    
    // 添加重试逻辑
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5;  // 最多允许5次重定向
    
    while (retry_count < max_retries && !success && redirect_count < max_redirects) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
            // 重试前暂停一下
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // 使用Board提供的HTTP客户端
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }
        
        // 设置基本请求头
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");
        
        // 添加ESP32认证头
        add_auth_headers(http.get());
        
        // 打开GET连接
        ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
        if (!http->Open("GET", current_url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics");
            // 移除delete http; 因为unique_ptr会自动管理内存
            retry_count++;
            continue;
        }
        
        // 检查HTTP状态码
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
        
        // 处理重定向 - 由于Http类没有GetHeader方法，我们只能根据状态码判断
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            // 由于无法获取Location头，只能报告重定向但无法继续
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // 非200系列状态码视为错误
        if (status_code < 200 || status_code >= 300) {
            ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // 读取响应
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;
        
        // 由于无法获取Content-Length和Content-Type头，我们不知道预期大小和内容类型
        ESP_LOGD(TAG, "Starting to read lyric content");
        
        while (true) {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // 注释掉以减少日志输出
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;
                
                // 定期打印下载进度 - 改为DEBUG级别减少输出
                if (total_read % 4096 == 0) {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            } else if (bytes_read == 0) {
                // 正常结束，没有更多数据
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            } else {
                // bytes_read < 0，可能是ESP-IDF的已知问题
                // 如果已经读取到了一些数据，则认为下载成功
                if (!lyric_content.empty()) {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                } else {
                    ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }
        
        http->Close();
        
        if (read_error) {
            retry_count++;
            continue;
        }
        
        // 如果成功读取数据，跳出重试循环
        if (success) {
            break;
        }
    }
    
    // 检查是否超过了最大重试次数
    if (retry_count >= max_retries) {
        ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
        return false;
    }
    
    // 记录前几个字节的数据，帮助调试
    if (!lyric_content.empty()) {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
        return false;
    }
    
    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// 解析歌词
bool Esp32Music::ParseLyrics(const std::string& lyric_content) {
    ESP_LOGI(TAG, "Parsing lyrics content");
    
    // 使用锁保护lyrics_数组访问
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    lyrics_.clear();
    
    // 按行分割歌词内容
    std::istringstream stream(lyric_content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // 跳过空行
        if (line.empty()) {
            continue;
        }
        
        // 解析LRC格式: [mm:ss.xx]歌词文本
        if (line.length() > 10 && line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos) {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);
                
                // 检查是否是元数据标签而不是时间戳
                // 元数据标签通常是 [ti:标题], [ar:艺术家], [al:专辑] 等
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos) {
                    std::string left_part = tag_or_time.substr(0, colon_pos);
                    
                    // 检查冒号左边是否是时间（数字）
                    bool is_time_format = true;
                    for (char c : left_part) {
                        if (!isdigit(c)) {
                            is_time_format = false;
                            break;
                        }
                    }
                    
                    // 如果不是时间格式，跳过这一行（元数据标签）
                    if (!is_time_format) {
                        // 可以在这里处理元数据，例如提取标题、艺术家等信息
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }
                    
                    // 是时间格式，解析时间戳
                    try {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);
                        
                        // 安全处理歌词文本，确保UTF-8编码正确
                        std::string safe_lyric_text;
                        if (!content.empty()) {
                            // 创建安全副本并验证字符串
                            safe_lyric_text = content;
                            // 确保字符串以null结尾
                            safe_lyric_text.shrink_to_fit();
                        }
                        
                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));
                        
                        if (!safe_lyric_text.empty()) {
                            // 限制日志输出长度，避免中文字符截断问题
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        } else {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }
    
    // 按时间戳排序
    std::sort(lyrics_.begin(), lyrics_.end());
    
    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// 歌词显示线程
void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric display thread started");
    
    if (!DownloadLyrics(current_lyric_url_)) {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }
    
    // 定期检查是否需要更新显示(频率可以降低)
    while (is_lyric_running_ && is_playing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) {
        return;
    }
    
    // 查找当前应该显示的歌词
    int new_lyric_index = -1;
    
    // 从当前歌词索引开始查找，提高效率
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;
    
    // 正向查找：找到最后一个时间戳小于等于当前时间的歌词
    for (int i = start_index; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= current_time_ms) {
            new_lyric_index = i;
        } else {
            break;  // 时间戳已超过当前时间
        }
    }
    
    // 如果没有找到(可能当前时间比第一句歌词还早)，显示空
    if (new_lyric_index == -1) {
        new_lyric_index = -1;
    }
    
    // 如果歌词索引发生变化，更新显示
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string lyric_text;
            
            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
                lyric_text = lyrics_[current_lyric_index_].second;
            }
            
            // 显示歌词
            display->SetChatMessage("lyric", lyric_text.c_str());
            
            ESP_LOGD(TAG, "Lyric update at %lldms: %s", 
                    current_time_ms, 
                    lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

// 删除复杂的认证初始化方法，使用简单的静态函数

// 删除复杂的类方法，使用简单的静态函数

/**
 * @brief 添加认证头到HTTP请求
 * @param http_client HTTP客户端指针
 * 
 * 添加的认证头包括：
 * - X-MAC-Address: 设备MAC地址
 * - X-Chip-ID: 设备芯片ID
 * - X-Timestamp: 当前时间戳
 * - X-Dynamic-Key: 动态生成的密钥
 */
// 删除复杂的AddAuthHeaders方法，使用简单的静态函数

// 删除复杂的认证验证和配置方法，使用简单的静态函数

// 显示模式控制方法实现
void Esp32Music::SetDisplayMode(DisplayMode mode) {
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;
    
    ESP_LOGI(TAG, "Display mode changed from %s to %s", 
            (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS",
            (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS");
}
