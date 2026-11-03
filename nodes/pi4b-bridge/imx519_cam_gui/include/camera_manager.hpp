#pragma once
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <map>
#include <mutex>
#include <cstdint>
#include "lockless_ring.hpp"

namespace libcamera {
    class CameraManager;
    class Camera;
    class FrameBufferAllocator;
    class FrameBuffer;
    class Request;
}

enum class CameraMode {
    PHYSICAL_IMX519,
    SYNTHETIC_FALLBACK
};

enum class AFState {
    IDLE,
    SWEEP_COARSE,
    SWEEP_FINE,
    LOCKED
};

struct FrameMetadata {
    uint64_t sequence{0};
    uint64_t timestamp_us{0};
    int width{800};
    int height{480};
    float lens_focus_dpt{0.0f};
    int vcm_position{1000};
    float sharpness_score{0.0f};
    float exposure_ms{16.6f};
    float gain{1.0f};
};

struct CameraFrame {
    std::vector<uint8_t> data;
    FrameMetadata meta;
    int dma_buf_fd{-1};
};

class CameraManager {
public:
    CameraManager();
    ~CameraManager();

    bool start(int width = 800, int height = 480, int fps = 60);
    void stop();

    bool getLatestFrame(CameraFrame& out_frame);

    // Autofocus & Hardware VCM Focus controls
    void triggerAutofocus();
    void setContinuousAutofocus(bool enable);
    bool isAutofocusing() const noexcept { return af_state_.load(std::memory_order_relaxed) != AFState::IDLE && af_state_.load(std::memory_order_relaxed) != AFState::LOCKED; }
    AFState getAFState() const noexcept { return af_state_.load(std::memory_order_relaxed); }
    float getSharpness() const noexcept { return current_sharpness_.load(std::memory_order_relaxed); }
    int getRawVcmPosition() const noexcept { return raw_vcm_position_.load(std::memory_order_relaxed); }

    void setManualFocus(float diopters);
    void setRawVcmFocus(int vcm_val);

    void setExposure(bool auto_exp, uint32_t exposure_us = 16666);
    void setGain(float gain = 1.0f);
    void setAwbMode(int mode_idx);
    bool switchResolution(int width, int height, int fps = 60);

    CameraMode getMode() const noexcept { return mode_; }
    bool isRunning() const noexcept { return is_running_.load(std::memory_order_relaxed); }

private:
    void captureWorkerThread();
    void generateSyntheticFrame(uint8_t* buffer, int w, int h, uint64_t frame_num);
    void requestCompleted(libcamera::Request* request);
    void autofocusWorkerThread();
    void writeHardwareVcm(int vcm_val);

    std::atomic<bool> is_running_{false};
    std::atomic<bool> trigger_af_{false};
    std::atomic<bool> continuous_af_{false};
    std::atomic<AFState> af_state_{AFState::IDLE};
    std::atomic<float> current_sharpness_{0.0f};
    std::atomic<int> raw_vcm_position_{1000};
    std::atomic<float> target_focus_dpt_{0.0f};
    std::atomic<bool> auto_exposure_{true};
    std::atomic<uint32_t> target_exposure_us_{16666};
    std::atomic<float> target_gain_{1.0f};
    std::atomic<int> target_awb_{0};
    std::atomic<uint64_t> frame_seq_{0};

    int req_width_{800};
    int req_height_{480};
    int req_fps_{60};

    CameraMode mode_{CameraMode::SYNTHETIC_FALLBACK};
    std::unique_ptr<std::thread> worker_;
    std::unique_ptr<std::thread> af_thread_;
    std::mutex cam_mutex_;
    LocklessRingBuffer<CameraFrame, 4> frame_ring_;

    std::unique_ptr<libcamera::CameraManager> libcamera_manager_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;
    std::vector<std::unique_ptr<libcamera::Request>> requests_;
    std::map<libcamera::FrameBuffer*, std::pair<void*, size_t>> mapped_buffers_;
};