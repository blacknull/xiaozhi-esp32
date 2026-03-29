#ifndef ESP32_MUSIC_H
#define ESP32_MUSIC_H

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

#include "music.h"

// MP3解码器支持
extern "C" {
#include "mp3dec.h"
#include "esp_timer.h"
}

// 音频数据块结构
struct AudioChunk {
    uint8_t* data;
    size_t size;
    
    AudioChunk() : data(nullptr), size(0) {}
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
};

class Esp32Music : public Music {
public:
    // 显示模式控制 - 移动到public区域
    enum DisplayMode {
        DISPLAY_MODE_SPECTRUM = 0,  // 默认显示频谱
        DISPLAY_MODE_LYRICS = 1     // 显示歌词
    };

private:
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string current_song_name_;
    bool song_name_displayed_;
    unsigned long expected_data_size_ = 0;   // 服务器提供的歌曲文件字节数
    int expected_duration_sec_ = 0;          // 服务器提供的歌曲时长（秒）
    
    // 歌词相关
    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;  // 时间戳和歌词文本
    std::mutex lyrics_mutex_;  // 保护lyrics_数组的互斥锁
    std::atomic<int> current_lyric_index_;
    std::thread lyric_thread_;
    std::atomic<bool> is_lyric_running_;
    
    std::atomic<DisplayMode> display_mode_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    std::thread play_thread_;
    std::thread download_thread_;
    int64_t current_play_time_ms_;  // 当前播放时间(毫秒)
    int64_t last_frame_time_ms_;    // 上一帧的时间戳
    int total_frames_decoded_;      // 已解码的帧数

    // 线程操作互斥锁，防止 StopStreaming/StartStreaming 并发 join 同一线程
    std::mutex thread_ops_mutex_;

    // 音频缓冲区
    std::queue<AudioChunk> audio_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    static constexpr size_t MAX_BUFFER_SIZE = 512 * 1024;    // 512KB缓冲区（PSRAM充裕，加大以改善播放稳定性）
    static constexpr size_t MIN_BUFFER_SIZE = 32 * 1024;    // 32KB持续播放最小缓冲
    static constexpr size_t INITIAL_BUFFER_SIZE = 256 * 1024; // 256KB初始启动缓冲（避免bit reservoir不足导致开头卡顿）
    
    // MP3解码器相关
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
    
    // 私有方法
    void DownloadAudioStream(const std::string& music_url);
    void PlayAudioStream();
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();  // 重置采样率到原始值
    
    // 歌词相关私有方法
    bool DownloadLyrics(const std::string& lyric_url);
    bool ParseLyrics(const std::string& lyric_content);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);
    
    // ID3标签处理
    size_t SkipId3Tag(uint8_t* data, size_t size);

    int16_t* final_pcm_data_fft = nullptr;
    size_t fft_buffer_samples_ = 0;  // final_pcm_data_fft 分配的样本数
    
    // 播放完成检测相关
    std::atomic<bool> was_playing_{false};           // 之前是否正在播放
    std::atomic<bool> normal_completion_{false};     // 是否正常完成
    std::atomic<bool> completion_triggered_{false};  // 是否已经触发过完成回调
    std::function<void(const std::string& song_name)> on_playback_complete_;  // 播放完成回调
    
    // 播放完成监控定时器 ID
    std::atomic<uint32_t> music_monitor_timer_id_{0};

    // MP3 帧大小计算（用于双同步验证）
    static int CalcMp3FrameSize(const uint8_t* hdr);

public:
    Esp32Music();
    ~Esp32Music();
    
    // 设置播放完成回调
    void SetPlaybackCompleteCallback(std::function<void(const std::string&)> callback) {
        on_playback_complete_ = callback;
    }
    
    // 检查播放完成状态（由 TimerManager 定期调用）
    bool CheckPlaybackCompleted(std::string& out_song_name);
    
    // 获取当前播放的歌曲名
    std::string GetCurrentSongName() const { return current_song_name_; }
    // 获取服务器提供的歌曲文件大小和时长
    unsigned long GetExpectedDataSize() const { return expected_data_size_; }
    int GetExpectedDurationSec() const { return expected_duration_sec_; }

    // 播放完成监控定时器管理
    void SetMonitorTimerId(uint32_t id) { music_monitor_timer_id_ = id; }
    uint32_t GetMonitorTimerId() const { return music_monitor_timer_id_.load(); }
    
    // 重载 new/delete 运算符，使用 PSRAM
    void* operator new(size_t size);
    void operator delete(void *ptr) noexcept;

    virtual bool Download(const std::string& song_name, const std::string& artist_name) override;
  
    virtual std::string GetDownloadResult() override;
    
    // 新增方法
    virtual bool StartStreaming(const std::string& music_url) override;
    virtual bool StopStreaming() override;  // 停止流式播放
    virtual size_t GetBufferSize() const override { return buffer_size_; }
    virtual bool IsDownloading() const override { return is_downloading_; }
    virtual int16_t* GetAudioData() override { return final_pcm_data_fft; }
    
    // 显示模式控制方法
    void SetDisplayMode(DisplayMode mode);
    DisplayMode GetDisplayMode() const { return display_mode_.load(); }
};

#endif // ESP32_MUSIC_H