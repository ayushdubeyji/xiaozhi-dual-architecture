#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "camera_manager.hpp"
#include "thread_affinity.hpp"
#include "sentry_telemetry.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

inline bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

void drawFillRect(SDL_Renderer* ren, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(ren, &rect);
}

void drawRect(SDL_Renderer* ren, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(ren, &rect);
}

void drawCyberReticle(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float pulse) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);

    int corner_len = size / 3;
    int half = size / 2 + static_cast<int>(pulse * 4.0f);

    // Top-Left
    SDL_RenderDrawLine(ren, cx - half, cy - half, cx - half + corner_len, cy - half);
    SDL_RenderDrawLine(ren, cx - half, cy - half, cx - half, cy - half + corner_len);

    // Top-Right
    SDL_RenderDrawLine(ren, cx + half, cy - half, cx + half - corner_len, cy - half);
    SDL_RenderDrawLine(ren, cx + half, cy - half, cx + half, cy - half + corner_len);

    // Bottom-Left
    SDL_RenderDrawLine(ren, cx - half, cy + half, cx - half + corner_len, cy + half);
    SDL_RenderDrawLine(ren, cx - half, cy + half, cx - half, cy + half - corner_len);

    // Bottom-Right
    SDL_RenderDrawLine(ren, cx + half, cy + half, cx + half - corner_len, cy + half);
    SDL_RenderDrawLine(ren, cx + half, cy + half, cx + half, cy + half - corner_len);

    // Center Crosshair ticks
    SDL_RenderDrawLine(ren, cx - 6, cy, cx - 2, cy);
    SDL_RenderDrawLine(ren, cx + 2, cy, cx + 6, cy);
    SDL_RenderDrawLine(ren, cx, cy - 6, cx, cy - 2);
    SDL_RenderDrawLine(ren, cx, cy + 2, cx, cy + 6);
}

double getCpuUsagePercent() {
    static unsigned long long prev_user = 0, prev_nice = 0, prev_sys = 0, prev_idle = 0;
    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) return 0.0;

    std::string cpu;
    unsigned long long user, nice, sys, idle, iowait, irq, softirq;
    stat_file >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq;

    unsigned long long total = (user - prev_user) + (nice - prev_nice) + (sys - prev_sys) + (idle - prev_idle);
    unsigned long long work = (user - prev_user) + (nice - prev_nice) + (sys - prev_sys);

    prev_user = user; prev_nice = nice; prev_sys = sys; prev_idle = idle;
    if (total == 0) return 0.0;
    return (double)work / (double)total * 100.0;
}

double getMemoryRssMb() {
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) return 0.0;
    long pages = 0;
    statm >> pages >> pages;
    long page_size = sysconf(_SC_PAGESIZE);
    return (double)(pages * page_size) / (1024.0 * 1024.0);
}

struct FastText {
    SDL_Texture* tex{nullptr};
    int w{0}, h{0};
    std::string last_str;

    void update(SDL_Renderer* ren, TTF_Font* font, const std::string& str, SDL_Color col) {
        if (str == last_str && tex) return;
        destroy();
        last_str = str;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, str.c_str(), col);
        if (surf) {
            w = surf->w;
            h = surf->h;
            tex = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
        }
    }

    void draw(SDL_Renderer* ren, int x, int y) {
        if (!tex) return;
        SDL_Rect r = { x, y, w, h };
        SDL_RenderCopy(ren, tex, NULL, &r);
    }

    void destroy() {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
    }
};

struct ResOption {
    int w;
    int h;
    int fps;
    std::string label;
};

const std::vector<ResOption> RES_MODES = {
    {800, 480, 60, "800x480 @ 60FPS (Native View)"},
    {1280, 720, 80, "1280x720 @ 80FPS (High-Speed)"},
    {1920, 1080, 60, "1920x1080 @ 60FPS (Cinema FHD)"}
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SystemPerf::pinToCore(3);
    SentryTelemetry::init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
        return 1;
    }

    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init Error: " << TTF_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    mkdir("/home/ayush/Pictures", 0755);

    SDL_Window* window = SDL_CreateWindow(
        "Arducam IMX519 Workbench Instrument",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        std::cerr << "Window Error: " << SDL_GetError() << "\n";
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        std::cerr << "Renderer Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    TTF_Font* font_small = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Medium.otf", 12);
    if (!font_small) font_small = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12);

    TTF_Font* font_medium = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Bold.otf", 14);
    if (!font_medium) font_medium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14);

    TTF_Font* font_large = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Bold.otf", 16);
    if (!font_large) font_large = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16);

    int cur_res_idx = 0;
    CameraManager camera;
    camera.start(RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h, RES_MODES[cur_res_idx].fps);

    // BGR24 fixes the inverted color swap (blue was orange)
    SDL_Texture* video_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_BGR24,
        SDL_TEXTUREACCESS_STREAMING,
        RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h
    );

    bool running = true;
    bool show_settings = false;
    bool shutter_flash = false;
    auto shutter_flash_time = std::chrono::steady_clock::now();

    float slider_focus_dpt = 0.0f;
    bool auto_focus_active = true;

    std::string toast_msg = "IMX519 Hardware Online :: Tap EXIT to return to desktop";
    auto toast_time = std::chrono::steady_clock::now();

    FastText txt_logo, txt_res, txt_af, txt_sharp, txt_fps, txt_telemetry, txt_toast, txt_exit;
    FastText btn_af_txt, btn_res_txt, btn_snap_txt, btn_foc_minus, btn_foc_plus, btn_set_txt;

    uint64_t frame_count = 0;
    double smoothed_fps = 60.0;
    auto last_fps_time = std::chrono::steady_clock::now();
    CameraFrame current_frame;

    // UDP Port 8086 Listener for ESP32-S3 hollow rotary encoder
    int focus_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (focus_udp_fd >= 0) {
        int flags = fcntl(focus_udp_fd, F_GETFL, 0);
        fcntl(focus_udp_fd, F_SETFL, flags | O_NONBLOCK);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(8086);
        bind(focus_udp_fd, (struct sockaddr*)&addr, sizeof(addr));
    }

    auto start_time = std::chrono::steady_clock::now();

    while (running) {
        auto loop_start = std::chrono::steady_clock::now();
        float anim_t = std::chrono::duration<float>(loop_start - start_time).count();

        // 1. Poll UDP socket for focus adjustments from hollow rotary encoder
        if (focus_udp_fd >= 0) {
            char ubuf[64];
            ssize_t n = recv(focus_udp_fd, ubuf, sizeof(ubuf) - 1, 0);
            if (n > 0) {
                ubuf[n] = '\0';
                if (strncmp(ubuf, "CAM_FOCUS:", 10) == 0) {
                    float val = std::strtof(ubuf + 10, nullptr);
                    slider_focus_dpt = std::clamp(val, 0.0f, 10.0f);
                    auto_focus_active = false;
                    camera.setManualFocus(slider_focus_dpt);
                    std::stringstream ss;
                    ss << "Lens Focus: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt ("
                       << camera.getRawVcmPosition() << ")";
                    toast_msg = ss.str();
                    toast_time = std::chrono::steady_clock::now();
                }
            }
        }

        // 2. Poll SDL events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_RETURN) {
                    // Trigger snapshot
                    if (!current_frame.data.empty()) {
                        shutter_flash = true;
                        shutter_flash_time = std::chrono::steady_clock::now();
                        std::time_t now_c = std::time(nullptr);
                        char tbuf[64];
                        std::strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", std::localtime(&now_c));
                        std::string fname = std::string("/home/ayush/Pictures/IMX519_") + tbuf + ".bmp";

                        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                            current_frame.data.data(),
                            current_frame.meta.width,
                            current_frame.meta.height,
                            24,
                            current_frame.meta.width * 3,
                            SDL_PIXELFORMAT_BGR24
                        );
                        if (surf) {
                            SDL_SaveBMP(surf, fname.c_str());
                            SDL_FreeSurface(surf);
                            toast_msg = "Snapshot Saved: " + fname;
                            toast_time = std::chrono::steady_clock::now();
                        }
                    }
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                float step_dir = (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
                float delta = (float)ev.wheel.y * step_dir * 0.20f;
                slider_focus_dpt = std::clamp(slider_focus_dpt + delta, 0.0f, 10.0f);
                auto_focus_active = false;
                camera.setManualFocus(slider_focus_dpt);
                std::stringstream ss;
                ss << "Focus: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt";
                toast_msg = ss.str();
                toast_time = std::chrono::steady_clock::now();
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                int mx = ev.button.x;
                int my = ev.button.y;

                // Dedicated Touch Exit Button (x: 730, y: 12, w: 56, h: 32)
                if (pointInRect(mx, my, 730, 12, 56, 32)) {
                    running = false;
                }
                // Button 1: Autofocus Trigger / Toggle (x: 20, y: 424, w: 120, h: 42)
                else if (pointInRect(mx, my, 20, 424, 120, 42)) {
                    auto_focus_active = true;
                    camera.triggerAutofocus();
                    toast_msg = "Autofocus Sweeping VCM Gradient...";
                    toast_time = std::chrono::steady_clock::now();
                }
                // Button 2: Resolution Switcher (x: 150, y: 424, w: 140, h: 42)
                else if (pointInRect(mx, my, 150, 424, 140, 42)) {
                    cur_res_idx = (cur_res_idx + 1) % RES_MODES.size();
                    camera.switchResolution(RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h, RES_MODES[cur_res_idx].fps);
                    if (video_texture) SDL_DestroyTexture(video_texture);
                    video_texture = SDL_CreateTexture(
                        renderer,
                        SDL_PIXELFORMAT_BGR24,
                        SDL_TEXTUREACCESS_STREAMING,
                        RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h
                    );
                    toast_msg = "Mode Switched: " + RES_MODES[cur_res_idx].label;
                    toast_time = std::chrono::steady_clock::now();
                }
                // Button 3: Snapshot Capture (x: 300, y: 424, w: 110, h: 42)
                else if (pointInRect(mx, my, 300, 424, 110, 42)) {
                    if (!current_frame.data.empty()) {
                        shutter_flash = true;
                        shutter_flash_time = std::chrono::steady_clock::now();
                        std::time_t now_c = std::time(nullptr);
                        char tbuf[64];
                        std::strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", std::localtime(&now_c));
                        std::string fname = std::string("/home/ayush/Pictures/IMX519_") + tbuf + ".bmp";

                        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                            current_frame.data.data(),
                            current_frame.meta.width,
                            current_frame.meta.height,
                            24,
                            current_frame.meta.width * 3,
                            SDL_PIXELFORMAT_BGR24
                        );
                        if (surf) {
                            SDL_SaveBMP(surf, fname.c_str());
                            SDL_FreeSurface(surf);
                            toast_msg = "Snapshot Saved: " + fname;
                            toast_time = std::chrono::steady_clock::now();
                        }
                    }
                }
                // Button 4: Focus Step Minus (x: 420, y: 424, w: 60, h: 42)
                else if (pointInRect(mx, my, 420, 424, 60, 42)) {
                    slider_focus_dpt = std::clamp(slider_focus_dpt - 0.25f, 0.0f, 10.0f);
                    auto_focus_active = false;
                    camera.setManualFocus(slider_focus_dpt);
                }
                // Button 5: Focus Step Plus (x: 490, y: 424, w: 60, h: 42)
                else if (pointInRect(mx, my, 490, 424, 60, 42)) {
                    slider_focus_dpt = std::clamp(slider_focus_dpt + 0.25f, 0.0f, 10.0f);
                    auto_focus_active = false;
                    camera.setManualFocus(slider_focus_dpt);
                }
                // Button 6: Settings Toggle (x: 560, y: 424, w: 100, h: 42)
                else if (pointInRect(mx, my, 560, 424, 100, 42)) {
                    show_settings = !show_settings;
                }
                // Quick Focus Slider touch track (x: 670 to 780, y: 424 to 466)
                else if (pointInRect(mx, my, 670, 424, 110, 42)) {
                    float rel = (float)(mx - 670) / 110.0f;
                    slider_focus_dpt = std::clamp(rel * 10.0f, 0.0f, 10.0f);
                    auto_focus_active = false;
                    camera.setManualFocus(slider_focus_dpt);
                }
            }
        }

        // 3. Fetch latest hardware frame
        if (camera.getLatestFrame(current_frame) && !current_frame.data.empty() && video_texture) {
            SDL_UpdateTexture(video_texture, NULL, current_frame.data.data(), current_frame.meta.width * 3);
            frame_count++;
        }

        // FPS calculation
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_fps_time).count();
        if (elapsed_sec >= 0.5) {
            smoothed_fps = (double)frame_count / elapsed_sec;
            frame_count = 0;
            last_fps_time = now;
        }

        // Clear and draw video feed
        SDL_RenderClear(renderer);
        if (video_texture) {
            SDL_RenderCopy(renderer, video_texture, NULL, NULL);
        }

        // 4. Draw Center Cyber Reticle
        float pulse = std::sin(anim_t * 5.0f);
        AFState af_st = camera.getAFState();
        uint8_t ret_r = 0, ret_g = 245, ret_b = 255;
        if (af_st == AFState::SWEEP_COARSE || af_st == AFState::SWEEP_FINE) {
            ret_r = 245; ret_g = 158; ret_b = 11;
        } else if (af_st == AFState::LOCKED) {
            ret_r = 16; ret_g = 185; ret_b = 129;
        }
        drawCyberReticle(renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 80, ret_r, ret_g, ret_b, 220, pulse);

        // Center Sharpness Meter Bar under Reticle
        float sharpness = camera.getSharpness();
        int meter_w = 140;
        int meter_h = 6;
        int meter_x = (SCREEN_WIDTH - meter_w) / 2;
        int meter_y = (SCREEN_HEIGHT / 2) + 52;
        drawFillRect(renderer, meter_x - 1, meter_y - 1, meter_w + 2, meter_h + 2, 6, 10, 20, 200);
        int fill_w = std::clamp(static_cast<int>((sharpness / 800.0f) * meter_w), 2, meter_w);
        drawFillRect(renderer, meter_x, meter_y, fill_w, meter_h, ret_r, ret_g, ret_b, 230);

        // 5. Draw Top Glass HUD Header
        drawFillRect(renderer, 10, 10, SCREEN_WIDTH - 20, 36, 6, 10, 20, 210);
        drawRect(renderer, 10, 10, SCREEN_WIDTH - 20, 36, 0, 245, 255, 80);

        // Logo
        txt_logo.update(renderer, font_medium, "IMX519 AI-OPTICS", {0, 245, 255, 255});
        txt_logo.draw(renderer, 24, 18);

        // Resolution Tag
        std::stringstream ss_res;
        ss_res << RES_MODES[cur_res_idx].w << "x" << RES_MODES[cur_res_idx].h << " @" << RES_MODES[cur_res_idx].fps << "FPS";
        txt_res.update(renderer, font_small, ss_res.str(), {148, 163, 184, 255});
        txt_res.draw(renderer, 185, 20);

        // Focus Status Badge
        std::string af_label;
        SDL_Color af_col;
        if (af_st == AFState::SWEEP_COARSE || af_st == AFState::SWEEP_FINE) {
            af_label = "AF: SEEKING PEAK...";
            af_col = {245, 158, 11, 255};
        } else if (af_st == AFState::LOCKED) {
            af_label = "AF: LOCKED";
            af_col = {16, 185, 129, 255};
        } else {
            std::stringstream ss;
            ss << "MF: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt";
            af_label = ss.str();
            af_col = {0, 245, 255, 255};
        }
        txt_af.update(renderer, font_medium, af_label, af_col);
        txt_af.draw(renderer, 335, 18);

        // Telemetry Chips
        std::stringstream ss_tel;
        ss_tel << std::fixed << std::setprecision(0) << smoothed_fps << " FPS | "
               << (int)getCpuUsagePercent() << "% CPU | " << (int)getMemoryRssMb() << " MB";
        txt_telemetry.update(renderer, font_small, ss_tel.str(), {16, 185, 129, 255});
        txt_telemetry.draw(renderer, 510, 20);

        // Dedicated Touch Exit Button in Top-Right
        drawFillRect(renderer, 730, 12, 56, 32, 239, 68, 68, 220);
        drawRect(renderer, 730, 12, 56, 32, 255, 255, 255, 200);
        txt_exit.update(renderer, font_small, "✕ EXIT", {255, 255, 255, 255});
        txt_exit.draw(renderer, 737, 20);

        // 6. Draw Bottom Touch Control Bar
        drawFillRect(renderer, 10, 416, SCREEN_WIDTH - 20, 56, 6, 10, 20, 220);
        drawRect(renderer, 10, 416, SCREEN_WIDTH - 20, 56, 0, 245, 255, 80);

        // Btn 1: Autofocus
        drawFillRect(renderer, 20, 424, 120, 40, 16, 185, 129, auto_focus_active ? 200 : 80);
        drawRect(renderer, 20, 424, 120, 40, 16, 185, 129, 255);
        btn_af_txt.update(renderer, font_small, auto_focus_active ? "⚡ AF: ACTIVE" : "⚡ AUTO FOCUS", {255, 255, 255, 255});
        btn_af_txt.draw(renderer, 30, 436);

        // Btn 2: Resolution
        drawFillRect(renderer, 150, 424, 140, 40, 30, 41, 59, 200);
        drawRect(renderer, 150, 424, 140, 40, 0, 245, 255, 140);
        std::string res_short = std::to_string(RES_MODES[cur_res_idx].w) + "x" + std::to_string(RES_MODES[cur_res_idx].h);
        btn_res_txt.update(renderer, font_small, "📐 " + res_short, {255, 255, 255, 255});
        btn_res_txt.draw(renderer, 165, 436);

        // Btn 3: Snapshot
        drawFillRect(renderer, 300, 424, 110, 40, 239, 68, 68, 200);
        drawRect(renderer, 300, 424, 110, 40, 255, 255, 255, 220);
        btn_snap_txt.update(renderer, font_small, "📷 SNAPSHOT", {255, 255, 255, 255});
        btn_snap_txt.draw(renderer, 312, 436);

        // Btn 4: Focus -
        drawFillRect(renderer, 420, 424, 60, 40, 30, 41, 59, 200);
        drawRect(renderer, 420, 424, 60, 40, 0, 245, 255, 120);
        btn_foc_minus.update(renderer, font_medium, "F -", {255, 255, 255, 255});
        btn_foc_minus.draw(renderer, 438, 434);

        // Btn 5: Focus +
        drawFillRect(renderer, 490, 424, 60, 40, 30, 41, 59, 200);
        drawRect(renderer, 490, 424, 60, 40, 0, 245, 255, 120);
        btn_foc_plus.update(renderer, font_medium, "F +", {255, 255, 255, 255});
        btn_foc_plus.draw(renderer, 508, 434);

        // Btn 6: Settings Toggle
        drawFillRect(renderer, 560, 424, 100, 40, 30, 41, 59, 200);
        drawRect(renderer, 560, 424, 100, 40, 0, 245, 255, 120);
        btn_set_txt.update(renderer, font_small, show_settings ? "✕ CLOSE" : "⚙ SETTINGS", {255, 255, 255, 255});
        btn_set_txt.draw(renderer, 575, 436);

        // Focus Quick Touch Track
        drawFillRect(renderer, 670, 432, 110, 24, 15, 23, 42, 220);
        drawRect(renderer, 670, 432, 110, 24, 0, 245, 255, 100);
        int thumb_x = 670 + static_cast<int>((slider_focus_dpt / 10.0f) * 102.0f);
        drawFillRect(renderer, thumb_x, 430, 8, 28, 0, 245, 255, 255);

        // 7. Shutter Flash Animation (fade out over 150ms)
        if (shutter_flash) {
            auto now_f = std::chrono::steady_clock::now();
            float flash_elapsed = std::chrono::duration<float>(now_f - shutter_flash_time).count();
            if (flash_elapsed < 0.15f) {
                uint8_t alpha = static_cast<uint8_t>((1.0f - (flash_elapsed / 0.15f)) * 220.0f);
                drawFillRect(renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 255, 255, alpha);
            } else {
                shutter_flash = false;
            }
        }

        // 8. Toast Message Banner
        auto now_t = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now_t - toast_time).count() < 3.0f) {
            txt_toast.update(renderer, font_small, toast_msg, {255, 255, 255, 255});
            int tw = txt_toast.w + 24;
            int tx = (SCREEN_WIDTH - tw) / 2;
            drawFillRect(renderer, tx, 56, tw, 26, 6, 10, 20, 220);
            drawRect(renderer, tx, 56, tw, 26, 0, 245, 255, 180);
            txt_toast.draw(renderer, tx + 12, 60);
        }

        SDL_RenderPresent(renderer);

        auto loop_end = std::chrono::steady_clock::now();
        auto loop_dur = std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start);
        if (loop_dur < std::chrono::microseconds(16666)) {
            std::this_thread::sleep_for(std::chrono::microseconds(16666) - loop_dur);
        }
    }

    if (focus_udp_fd >= 0) close(focus_udp_fd);
    camera.stop();

    txt_logo.destroy();
    txt_res.destroy();
    txt_af.destroy();
    txt_telemetry.destroy();
    txt_toast.destroy();
    txt_exit.destroy();
    btn_af_txt.destroy();
    btn_res_txt.destroy();
    btn_snap_txt.destroy();
    btn_foc_minus.destroy();
    btn_foc_plus.destroy();
    btn_set_txt.destroy();

    if (video_texture) SDL_DestroyTexture(video_texture);
    if (font_small) TTF_CloseFont(font_small);
    if (font_medium) TTF_CloseFont(font_medium);
    if (font_large) TTF_CloseFont(font_large);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    SentryTelemetry::shutdown();
    return 0;
}
