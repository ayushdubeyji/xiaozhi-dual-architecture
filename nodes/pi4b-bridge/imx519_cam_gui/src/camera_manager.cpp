#include "camera_manager.hpp"
#include "thread_affinity.hpp"
#include "neon_ops.hpp"
#include <libcamera/libcamera.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <iostream>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <span>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

CameraManager::CameraManager() {}

CameraManager::~CameraManager() {
    stop();
}

void CameraManager::writeHardwareVcm(int vcm_val) {
    int clamped = std::clamp(vcm_val, 0, 4095);
    raw_vcm_position_.store(clamped, std::memory_order_relaxed);

    int fd = open("/dev/v4l-subdev1", O_RDWR | O_NONBLOCK);
    if (fd >= 0) {
        struct v4l2_control ctrl{};
        ctrl.id = 0x009a090a; // V4L2_CID_FOCUS_ABSOLUTE
        ctrl.value = clamped;
        ioctl(fd, VIDIOC_S_CTRL, &ctrl);
        close(fd);
    }
}

void CameraManager::setRawVcmFocus(int vcm_val) {
    af_state_.store(AFState::IDLE, std::memory_order_release);
    continuous_af_.store(false, std::memory_order_release);
    target_focus_dpt_.store((static_cast<float>(vcm_val) / 4095.0f) * 10.0f, std::memory_order_relaxed);
    writeHardwareVcm(vcm_val);
}

void CameraManager::setManualFocus(float diopters) {
    af_state_.store(AFState::IDLE, std::memory_order_release);
    continuous_af_.store(false, std::memory_order_release);
    float clamped_dpt = std::clamp(diopters, 0.0f, 10.0f);
    target_focus_dpt_.store(clamped_dpt, std::memory_order_relaxed);
    int vcm_val = static_cast<int>((clamped_dpt / 10.0f) * 4095.0f);
    writeHardwareVcm(vcm_val);
}

void CameraManager::triggerAutofocus() {
    trigger_af_.store(true, std::memory_order_release);
}

void CameraManager::setContinuousAutofocus(bool enable) {
    continuous_af_.store(enable, std::memory_order_release);
    if (enable) {
        trigger_af_.store(true, std::memory_order_release);
    }
}

void CameraManager::autofocusWorkerThread() {
    SystemPerf::pinToCore(1);

    while (is_running_.load(std::memory_order_relaxed)) {
        if (!trigger_af_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        af_state_.store(AFState::SWEEP_COARSE, std::memory_order_release);
        int best_coarse_vcm = 1000;
        float max_coarse_sharp = -1.0f;

        // Stage 1: Coarse sweep 200 to 3800
        for (int v = 200; v <= 3800; v += 300) {
            if (!is_running_.load(std::memory_order_relaxed) || !trigger_af_.load(std::memory_order_relaxed)) break;
            writeHardwareVcm(v);
            std::this_thread::sleep_for(std::chrono::milliseconds(28));
            float s = current_sharpness_.load(std::memory_order_relaxed);
            if (s > max_coarse_sharp) {
                max_coarse_sharp = s;
                best_coarse_vcm = v;
            }
        }

        // Stage 2: Fine sweep +/- 220 around peak
        af_state_.store(AFState::SWEEP_FINE, std::memory_order_release);
        int fine_start = std::max(0, best_coarse_vcm - 220);
        int fine_end = std::min(4095, best_coarse_vcm + 220);
        int best_fine_vcm = best_coarse_vcm;
        float max_fine_sharp = -1.0f;

        for (int v = fine_start; v <= fine_end; v += 35) {
            if (!is_running_.load(std::memory_order_relaxed) || !trigger_af_.load(std::memory_order_relaxed)) break;
            writeHardwareVcm(v);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            float s = current_sharpness_.load(std::memory_order_relaxed);
            if (s > max_fine_sharp) {
                max_fine_sharp = s;
                best_fine_vcm = v;
            }
        }

        writeHardwareVcm(best_fine_vcm);
        target_focus_dpt_.store((static_cast<float>(best_fine_vcm) / 4095.0f) * 10.0f, std::memory_order_relaxed);
        af_state_.store(AFState::LOCKED, std::memory_order_release);
        trigger_af_.store(false, std::memory_order_release);
    }
}

bool CameraManager::start(int width, int height, int fps) {
    std::lock_guard<std::mutex> lock(cam_mutex_);
    if (is_running_.load()) return true;

    req_width_ = width;
    req_height_ = height;
    req_fps_ = fps;

    try {
        libcamera_manager_ = std::make_unique<libcamera::CameraManager>();
        if (libcamera_manager_->start() == 0 && !libcamera_manager_->cameras().empty()) {
            camera_ = libcamera_manager_->cameras()[0];
            if (camera_->acquire() == 0) {
                auto config = camera_->generateConfiguration({ libcamera::StreamRole::Viewfinder });
                if (config && !config->empty()) {
                    auto& streamConfig = config->at(0);
                    streamConfig.size.width = req_width_;
                    streamConfig.size.height = req_height_;
                    streamConfig.pixelFormat = libcamera::formats::RGB888;
                    streamConfig.bufferCount = 4;
                    config->validate();

                    if (camera_->configure(config.get()) == 0) {
                        allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
                        libcamera::Stream* stream = streamConfig.stream();
                        if (allocator_->allocate(stream) >= 0) {
                            for (const auto& buffer : allocator_->buffers(stream)) {
                                const auto& planes = buffer->planes();
                                if (planes.empty()) continue;
                                void* mem = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, planes[0].fd.get(), 0);
                                if (mem != MAP_FAILED) {
                                    mapped_buffers_[buffer.get()] = { mem, planes[0].length };
                                }
                                auto request = camera_->createRequest();
                                if (request) {
                                    request->addBuffer(stream, buffer.get());
                                    requests_.push_back(std::move(request));
                                }
                            }

                            camera_->requestCompleted.connect(this, &CameraManager::requestCompleted);

                            libcamera::ControlList controls;
                            int64_t frame_time_us = 1000000 / req_fps_;
                            int64_t frame_limits[2] = {frame_time_us, frame_time_us};
                            controls.set(libcamera::controls::FrameDurationLimits, std::span<const int64_t, 2>(frame_limits));

                            if (camera_->start(&controls) == 0) {
                                for (auto& req : requests_) {
                                    camera_->queueRequest(req.get());
                                }
                                mode_ = CameraMode::PHYSICAL_IMX519;
                                is_running_.store(true);
                                af_thread_ = std::make_unique<std::thread>(&CameraManager::autofocusWorkerThread, this);
                                std::cout << "[CameraManager] Physical IMX519 online at " 
                                          << req_width_ << "x" << req_height_ << " @ " << req_fps_ << " FPS\n";
                                return true;
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CameraManager] libcamera init exception: " << e.what() << "\n";
    }

    mode_ = CameraMode::SYNTHETIC_FALLBACK;
    is_running_.store(true);
    worker_ = std::make_unique<std::thread>(&CameraManager::captureWorkerThread, this);
    af_thread_ = std::make_unique<std::thread>(&CameraManager::autofocusWorkerThread, this);
    return true;
}

void CameraManager::requestCompleted(libcamera::Request* request) {
    if (request->status() == libcamera::Request::RequestCancelled) return;

    for (const auto& [stream, buffer] : request->buffers()) {
        auto it = mapped_buffers_.find(buffer);
        if (it != mapped_buffers_.end() && it->second.first) {
            CameraFrame frame;
            frame.meta.width = req_width_;
            frame.meta.height = req_height_;
            frame.meta.sequence = frame_seq_++;
            frame.meta.lens_focus_dpt = target_focus_dpt_.load(std::memory_order_relaxed);
            frame.meta.vcm_position = raw_vcm_position_.load(std::memory_order_relaxed);
            frame.meta.exposure_ms = target_exposure_us_.load(std::memory_order_relaxed) / 1000.0f;
            frame.meta.gain = target_gain_.load(std::memory_order_relaxed);

            size_t bytes = static_cast<size_t>(req_width_) * req_height_ * 3;
            frame.data.resize(bytes);
            SimdOps::neon_memcpy_fast(frame.data.data(), it->second.first, bytes);

            float sharpness = SimdOps::compute_focus_sharpness(frame.data.data(), req_width_, req_height_);
            current_sharpness_.store(sharpness, std::memory_order_relaxed);
            frame.meta.sharpness_score = sharpness;

            frame_ring_.push(frame);
        }
    }

    request->reuse(libcamera::Request::ReuseBuffers);
    if (camera_) {
        camera_->queueRequest(request);
    }
}

bool CameraManager::switchResolution(int width, int height, int fps) {
    stop();
    return start(width, height, fps);
}

void CameraManager::stop() {
    std::lock_guard<std::mutex> lock(cam_mutex_);
    if (!is_running_.load()) return;
    is_running_.store(false);
    trigger_af_.store(false);

    if (af_thread_ && af_thread_->joinable()) {
        af_thread_->join();
    }
    af_thread_.reset();

    if (camera_) {
        camera_->stop();
        for (auto& [buf, entry] : mapped_buffers_) {
            if (entry.first && entry.first != MAP_FAILED) {
                munmap(entry.first, entry.second);
            }
        }
        mapped_buffers_.clear();
        requests_.clear();
        allocator_.reset();
        camera_->release();
        camera_.reset();
    }
    if (libcamera_manager_) {
        libcamera_manager_->stop();
        libcamera_manager_.reset();
    }

    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
    std::cout << "[CameraManager] Pipeline stopped.\n";
}

bool CameraManager::getLatestFrame(CameraFrame& out_frame) {
    return frame_ring_.peek_latest(out_frame);
}

void CameraManager::setExposure(bool auto_exp, uint32_t exposure_us) {
    auto_exposure_.store(auto_exp, std::memory_order_release);
    target_exposure_us_.store(exposure_us, std::memory_order_release);
}

void CameraManager::setGain(float gain) {
    target_gain_.store(gain, std::memory_order_release);
}

void CameraManager::setAwbMode(int mode_idx) {
    target_awb_.store(mode_idx, std::memory_order_release);
}

void CameraManager::captureWorkerThread() {
    SystemPerf::pinToCore(2);
    SystemPerf::setRealtimePriority(20);

    const int target_interval_us = 1000000 / (req_fps_ > 0 ? req_fps_ : 60);
    uint64_t seq = 0;
    std::vector<uint8_t> scratch_buffer(req_width_ * req_height_ * 3);

    while (is_running_.load(std::memory_order_relaxed)) {
        auto t_start = std::chrono::steady_clock::now();
        int cur_w = req_width_;
        int cur_h = req_height_;
        if ((int)scratch_buffer.size() != cur_w * cur_h * 3) {
            scratch_buffer.resize(cur_w * cur_h * 3);
        }

        generateSyntheticFrame(scratch_buffer.data(), cur_w, cur_h, seq);

        CameraFrame frame;
        frame.data.resize(scratch_buffer.size());
        SimdOps::neon_memcpy_fast(frame.data.data(), scratch_buffer.data(), scratch_buffer.size());

        frame.meta.sequence = seq++;
        frame.meta.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(t_start.time_since_epoch()).count();
        frame.meta.width = cur_w;
        frame.meta.height = cur_h;
        frame.meta.lens_focus_dpt = target_focus_dpt_.load(std::memory_order_relaxed);
        frame.meta.vcm_position = raw_vcm_position_.load(std::memory_order_relaxed);
        frame.meta.exposure_ms = target_exposure_us_.load(std::memory_order_relaxed) / 1000.0f;
        frame.meta.gain = target_gain_.load(std::memory_order_relaxed);

        float sharpness = SimdOps::compute_focus_sharpness(frame.data.data(), cur_w, cur_h);
        current_sharpness_.store(sharpness, std::memory_order_relaxed);
        frame.meta.sharpness_score = sharpness;

        frame_ring_.push(frame);

        auto t_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
        if (elapsed_us < target_interval_us) {
            std::this_thread::sleep_for(std::chrono::microseconds(target_interval_us - elapsed_us));
        }
    }
}

void CameraManager::generateSyntheticFrame(uint8_t* buffer, int w, int h, uint64_t frame_num) {
    float phase = (float)(frame_num % 360) * (3.14159265f / 180.0f);
    uint8_t base_r = (uint8_t)(15.0f + 10.0f * std::sin(phase));
    uint8_t base_g = (uint8_t)(25.0f + 15.0f * std::cos(phase * 0.7f));
    uint8_t base_b = (uint8_t)(40.0f + 20.0f * std::sin(phase * 0.5f));

    for (int y = 0; y < h; ++y) {
        uint8_t* row = buffer + (y * w * 3);
        bool grid_y = ((y % 40) == 0);
        for (int x = 0; x < w; ++x) {
            bool grid_x = ((x % 40) == 0);
            int idx = x * 3;
            if (grid_x || grid_y) {
                row[idx + 0] = 60;
                row[idx + 1] = 120;
                row[idx + 2] = 180;
            } else {
                row[idx + 0] = base_r;
                row[idx + 1] = base_g;
                row[idx + 2] = base_b;
            }
        }
    }
}