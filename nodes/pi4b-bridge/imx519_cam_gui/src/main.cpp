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
#include <filesystem>

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

// Radial Arc Geometry (centered off-screen left for natural thumb sweep)
constexpr float ARC_CENTER_X = -50.0f;
constexpr float ARC_CENTER_Y = 240.0f;
constexpr float ARC_RADIUS   = 210.0f;
constexpr float ARC_MIN_ANG  = -0.85f; // ~ -48.7 deg
constexpr float ARC_MAX_ANG  =  0.85f; // ~ +48.7 deg

inline bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

inline float distSq(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return dx * dx + dy * dy;
}

// -------------------------------------------------------------
// Primitive Drawing Helpers
// -------------------------------------------------------------
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

void drawThickLine(SDL_Renderer* ren, int x1, int y1, int x2, int y2, int thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    if (thickness <= 1) {
        SDL_RenderDrawLine(ren, x1, y1, x2, y2);
        return;
    }
    float dx = static_cast<float>(x2 - x1);
    float dy = static_cast<float>(y2 - y1);
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len;
    float ny = dx / len;
    int half = thickness / 2;
    for (int i = -half; i <= half; ++i) {
        SDL_RenderDrawLine(ren,
            static_cast<int>(x1 + nx * i), static_cast<int>(y1 + ny * i),
            static_cast<int>(x2 + nx * i), static_cast<int>(y2 + ny * i));
    }
}

void drawFillCircleFast(SDL_Renderer* ren, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = static_cast<int>(std::sqrt(radius * radius - dy * dy));
        SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void drawCircle(SDL_Renderer* ren, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(ren, cx + x, cy + y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x);
        SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx - y, cy - x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x);
        SDL_RenderDrawPoint(ren, cx + x, cy - y);
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void drawArc(SDL_Renderer* ren, int cx, int cy, int radius, float start_rad, float end_rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int thickness = 1) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    float step = 0.02f;
    if (start_rad > end_rad) std::swap(start_rad, end_rad);
    for (int t = 0; t < thickness; ++t) {
        int rad = radius + t - thickness / 2;
        int prev_x = cx + static_cast<int>(rad * std::cos(start_rad));
        int prev_y = cy + static_cast<int>(rad * std::sin(start_rad));
        for (float a_cur = start_rad + step; a_cur <= end_rad; a_cur += step) {
            int cur_x = cx + static_cast<int>(rad * std::cos(a_cur));
            int cur_y = cy + static_cast<int>(rad * std::sin(a_cur));
            SDL_RenderDrawLine(ren, prev_x, prev_y, cur_x, cur_y);
            prev_x = cur_x;
            prev_y = cur_y;
        }
    }
}

void drawFillRoundedRect(SDL_Renderer* ren, int x, int y, int w, int h, int rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    if (rad <= 0) {
        SDL_Rect rect = { x, y, w, h };
        SDL_RenderFillRect(ren, &rect);
        return;
    }
    rad = std::min(rad, std::min(w / 2, h / 2));
    SDL_Rect mid = { x + rad, y, w - 2 * rad, h };
    SDL_RenderFillRect(ren, &mid);
    SDL_Rect left_bar = { x, y + rad, rad, h - 2 * rad };
    SDL_Rect right_bar = { x + w - rad, y + rad, rad, h - 2 * rad };
    SDL_RenderFillRect(ren, &left_bar);
    SDL_RenderFillRect(ren, &right_bar);

    for (int dy = 0; dy < rad; ++dy) {
        int dx = static_cast<int>(std::sqrt(rad * rad - dy * dy));
        SDL_RenderDrawLine(ren, x + rad - dx, y + rad - dy, x + rad, y + rad - dy);
        SDL_RenderDrawLine(ren, x + w - rad, y + rad - dy, x + w - rad + dx, y + rad - dy);
        SDL_RenderDrawLine(ren, x + rad - dx, y + h - rad + dy, x + rad, y + h - rad + dy);
        SDL_RenderDrawLine(ren, x + w - rad, y + h - rad + dy, x + w - rad + dx, y + h - rad + dy);
    }
}

void drawRoundedRect(SDL_Renderer* ren, int x, int y, int w, int h, int rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    if (rad <= 0) {
        SDL_Rect rect = { x, y, w, h };
        SDL_RenderDrawRect(ren, &rect);
        return;
    }
    rad = std::min(rad, std::min(w / 2, h / 2));
    SDL_RenderDrawLine(ren, x + rad, y, x + w - rad, y);
    SDL_RenderDrawLine(ren, x + rad, y + h, x + w - rad, y + h);
    SDL_RenderDrawLine(ren, x, y + rad, x, y + h - rad);
    SDL_RenderDrawLine(ren, x + w, y + rad, x + w, y + h - rad);

    for (float a_cur = 0; a_cur <= 1.570796f; a_cur += 0.1f) {
        int cx1 = x + w - rad, cy1 = y + rad;
        int cx2 = x + rad,     cy2 = y + rad;
        int cx3 = x + rad,     cy3 = y + h - rad;
        int cx4 = x + w - rad, cy4 = y + h - rad;
        SDL_RenderDrawPoint(ren, cx1 + static_cast<int>(rad * std::cos(a_cur - 1.570796f)), cy1 + static_cast<int>(rad * std::sin(a_cur - 1.570796f)));
        SDL_RenderDrawPoint(ren, cx2 - static_cast<int>(rad * std::sin(a_cur)), cy2 - static_cast<int>(rad * std::cos(a_cur)));
        SDL_RenderDrawPoint(ren, cx3 - static_cast<int>(rad * std::cos(a_cur)), cy3 + static_cast<int>(rad * std::sin(a_cur)));
        SDL_RenderDrawPoint(ren, cx4 + static_cast<int>(rad * std::sin(a_cur)), cy4 + static_cast<int>(rad * std::cos(a_cur)));
    }
}

// -------------------------------------------------------------
// Vector Geometric Icons
// -------------------------------------------------------------
void drawIconCamera(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int w = size;
    int h = static_cast<int>(size * 0.72f);
    int x = cx - w / 2;
    int y = cy - h / 2 + 1;
    drawFillRoundedRect(ren, x, y, w, h, 3, r, g, b, a);
    int lens_r = static_cast<int>(h * 0.38f);
    drawCircle(ren, cx, cy + 1, lens_r, 10, 15, 26, a);
    drawFillCircleFast(ren, cx, cy + 1, lens_r - 1, 10, 15, 26, a);
    drawCircle(ren, cx, cy + 1, lens_r - 3, r, g, b, a);
    int bw = w / 3;
    drawFillRect(ren, cx - bw / 2, y - 3, bw, 3, r, g, b, a);
}

void drawIconFocus(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int rad = size / 3;
    int gx = cx - 2;
    int gy = cy - 2;
    drawCircle(ren, gx, gy, rad, r, g, b, a);
    drawCircle(ren, gx, gy, rad - 1, r, g, b, a);
    drawFillCircleFast(ren, gx, gy, 1, r, g, b, a);
    drawThickLine(ren, gx + rad - 1, gy + rad - 1, cx + size / 2, cy + size / 2, 2, r, g, b, a);
}

void drawIconBolt(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    int h = size;
    int top_y = cy - h / 2;
    int mid_y = cy;
    int bot_y = cy + h / 2;

    for (int y = top_y; y <= mid_y; ++y) {
        float frac = (mid_y > top_y) ? static_cast<float>(y - top_y) / (mid_y - top_y) : 0.0f;
        int x1 = static_cast<int>((cx + 2) + frac * ((cx - 5) - (cx + 2)));
        int x2 = static_cast<int>((cx + 2) + frac * ((cx + 1) - (cx + 2)));
        SDL_RenderDrawLine(ren, std::min(x1, x2), y, std::max(x1, x2), y);
    }
    for (int y = mid_y; y <= bot_y; ++y) {
        float frac = (bot_y > mid_y) ? static_cast<float>(y - mid_y) / (bot_y - mid_y) : 0.0f;
        int x1 = static_cast<int>((cx - 1) + frac * ((cx - 3) - (cx - 1)));
        int x2 = static_cast<int>((cx + 4) + frac * ((cx - 3) - (cx + 4)));
        SDL_RenderDrawLine(ren, std::min(x1, x2), y, std::max(x1, x2), y);
    }
}

void drawIconResolution(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int w = size;
    int h = static_cast<int>(size * 0.65f);
    int hw = w / 2;
    int hh = h / 2;
    int arm = size / 4;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderDrawLine(ren, cx - hw, cy - hh, cx - hw + arm, cy - hh);
    SDL_RenderDrawLine(ren, cx - hw, cy - hh, cx - hw, cy - hh + arm);
    SDL_RenderDrawLine(ren, cx + hw, cy - hh, cx + hw - arm, cy - hh);
    SDL_RenderDrawLine(ren, cx + hw, cy - hh, cx + hw, cy - hh + arm);
    SDL_RenderDrawLine(ren, cx - hw, cy + hh, cx - hw + arm, cy + hh);
    SDL_RenderDrawLine(ren, cx - hw, cy + hh, cx - hw, cy + hh - arm);
    SDL_RenderDrawLine(ren, cx + hw, cy + hh, cx + hw - arm, cy + hh);
    SDL_RenderDrawLine(ren, cx + hw, cy + hh, cx + hw, cy + hh - arm);
}

void drawIconSliders(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int hw = size / 2;
    int y1 = cy - 5, y2 = cy, y3 = cy + 5;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderDrawLine(ren, cx - hw, y1, cx + hw, y1);
    SDL_RenderDrawLine(ren, cx - hw, y2, cx + hw, y2);
    SDL_RenderDrawLine(ren, cx - hw, y3, cx + hw, y3);
    drawFillCircleFast(ren, cx - 2, y1, 2, r, g, b, a);
    drawFillCircleFast(ren, cx + 3, y2, 2, r, g, b, a);
    drawFillCircleFast(ren, cx - 4, y3, 2, r, g, b, a);
}

void drawIconSun(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int rad = size / 4;
    drawCircle(ren, cx, cy, rad, r, g, b, a);
    drawFillCircleFast(ren, cx, cy, rad - 1, r, g, b, a);
    int ray = size / 2;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderDrawLine(ren, cx, cy - ray, cx, cy - rad - 2);
    SDL_RenderDrawLine(ren, cx, cy + ray, cx, cy + rad + 2);
    SDL_RenderDrawLine(ren, cx - ray, cy, cx - rad - 2, cy);
    SDL_RenderDrawLine(ren, cx + ray, cy, cx + rad + 2, cy);
}

void drawIconClose(SDL_Renderer* ren, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int h = size / 2;
    drawThickLine(ren, cx - h, cy - h, cx + h, cy + h, 2, r, g, b, a);
    drawThickLine(ren, cx - h, cy + h, cx + h, cy - h, 2, r, g, b, a);
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

// -------------------------------------------------------------
// System Telemetry
// -------------------------------------------------------------
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

// -------------------------------------------------------------
// Cached FastText Renderer
// -------------------------------------------------------------
struct FastText {
    SDL_Texture* tex{nullptr};
    int w{0}, h{0};
    std::string last_str;
    SDL_Color last_col{0, 0, 0, 0};

    FastText() = default;
    ~FastText() { destroy(); }

    FastText(const FastText&) = delete;
    FastText& operator=(const FastText&) = delete;

    FastText(FastText&& other) noexcept
        : tex(other.tex), w(other.w), h(other.h), last_str(std::move(other.last_str)), last_col(other.last_col) {
        other.tex = nullptr;
        other.w = 0;
        other.h = 0;
    }

    FastText& operator=(FastText&& other) noexcept {
        if (this != &other) {
            destroy();
            tex = other.tex;
            w = other.w;
            h = other.h;
            last_str = std::move(other.last_str);
            last_col = other.last_col;
            other.tex = nullptr;
            other.w = 0;
            other.h = 0;
        }
        return *this;
    }

    void update(SDL_Renderer* ren, TTF_Font* font, const std::string& str, SDL_Color col) {
        if (!font) return;
        if (str == last_str && tex && col.r == last_col.r && col.g == last_col.g && col.b == last_col.b && col.a == last_col.a) return;
        destroy();
        last_str = str;
        last_col = col;
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

    void drawCentered(SDL_Renderer* ren, int cx, int cy) {
        if (!tex) return;
        SDL_Rect r = { cx - w / 2, cy - h / 2, w, h };
        SDL_RenderCopy(ren, tex, NULL, &r);
    }

    void destroy() {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
        w = 0;
        h = 0;
        last_str.clear();
    }
};

// -------------------------------------------------------------
// Navigation Modes & Data Models
// -------------------------------------------------------------
enum class LeftViewMode {
    CAROUSEL = 0,
    FOCUS_JOG = 1
};

enum class DragTarget {
    DRAG_NONE = 0,
    DRAG_CAROUSEL_WHEEL,
    DRAG_FOCUS_JOG,
    DRAG_EXP_SLIDER,
    DRAG_GAIN_SLIDER
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

const std::vector<std::string> AWB_NAMES = {
    "Auto AWB", "Daylight", "Fluorescent", "Tungsten"
};

// -------------------------------------------------------------
// Main Application
// -------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SystemPerf::pinToCore(3);
    SentryTelemetry::init();

    // Ensure Wayland & Clean Touch support in SDL2 without synthetic duplication
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "0");

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
        "Arducam IMX519 AI-Optics Instrument",
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

    // Load Clean Inter / DejaVu Typography
    TTF_Font* font_tiny = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Medium.otf", 11);
    if (!font_tiny) font_tiny = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11);

    TTF_Font* font_small = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Medium.otf", 13);
    if (!font_small) font_small = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13);

    TTF_Font* font_medium = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Bold.otf", 15);
    if (!font_medium) font_medium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15);

    TTF_Font* font_large = TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-Bold.otf", 18);
    if (!font_large) font_large = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18);

    int cur_res_idx = 0;
    CameraManager camera;
    camera.start(RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h, RES_MODES[cur_res_idx].fps);

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

    // Exposure & Gain State
    bool auto_exposure_enabled = true;
    uint32_t exp_val_us = 16666;
    float gain_val = 1.0f;
    int cur_awb_idx = 0;

    // Navigation & Interaction State
    LeftViewMode active_view = LeftViewMode::CAROUSEL;
    DragTarget active_drag = DragTarget::DRAG_NONE;
    int drag_last_x = 0, drag_last_y = 0;
    float drag_start_angle = 0.0f, drag_last_angle = 0.0f;
    bool is_dragging_carousel = false;
    SDL_FingerID active_finger_id = -1;

    float carousel_angle = 0.0f;
    float carousel_target_angle = 0.0f;
    int current_carousel_idx = 0;

    std::string toast_msg = "IMX519 AI-Optics Online :: Radial Arc Active";
    auto toast_time = std::chrono::steady_clock::now();

    // FastText cache handles (persistent to prevent GPU texture leakage)
    FastText txt_logo, txt_telemetry, txt_lens_info, txt_toast, txt_exit;
    FastText txt_shutter_label, txt_af_label, txt_modes_btn, txt_af_btn;
    FastText txt_carousel_items[5];
    FastText txt_focus_val, txt_vcm_val, txt_dpt_inf, txt_dpt_mid, txt_dpt_near;
    FastText txt_set_title, txt_ae_btn, txt_exp_label, txt_gain_label, txt_awb_title, txt_awb_btn[4];
    FastText txt_m, txt_p, txt_set_btn_lbl, txt_modes_tab, txt_focus_tab;

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

    // Lambda: Trigger Snapshot
    auto triggerSnapshot = [&]() {
        if (!current_frame.data.empty()) {
            shutter_flash = true;
            shutter_flash_time = std::chrono::steady_clock::now();
            std::time_t now_c = std::time(nullptr);
            char tbuf[64];
            std::strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", std::localtime(&now_c));
            std::error_code ec;
            std::filesystem::create_directories("/home/ayush/Pictures", ec);
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
                toast_msg = "Saved Snapshot: " + fname;
                toast_time = std::chrono::steady_clock::now();
            }
        }
    };

    // Lambda: Unified Pointer Down (Touch / Mouse)
    auto handlePointerDown = [&](int x, int y) {
        // 1. Dedicated Exit Button (Top-Right: 715, 8, 75, 36)
        if (pointInRect(x, y, 715, 8, 75, 36)) {
            running = false;
            return;
        }

        // 2. Right Action Pod: Shutter Button (Center: 745, 240, R=36)
        if (distSq(x, y, 745, 240) <= (36 * 36)) {
            triggerSnapshot();
            return;
        }

        // 3. Right Action Pod: Quick AF Button (Center: 745, 140, R=28)
        if (distSq(x, y, 745, 140) <= (28 * 28)) {
            auto_focus_active = true;
            camera.triggerAutofocus();
            toast_msg = "⚡ Autofocus Sweeping VCM Gradient...";
            toast_time = std::chrono::steady_clock::now();
            return;
        }

        // 4. Right Action Pod: Settings Drawer Button (Center: 745, 340, R=28)
        if (distSq(x, y, 745, 340) <= (28 * 28)) {
            show_settings = !show_settings;
            return;
        }

        // 5. Settings Drawer (if open: x in [440, 715], y in [50, 430])
        if (show_settings) {
            if (pointInRect(x, y, 440, 50, 275, 380)) {
                // Close button at (665, 55, 45, 40)
                if (pointInRect(x, y, 665, 55, 45, 40)) {
                    show_settings = false;
                    return;
                }
                // Auto-Exposure Toggle (455, 88, 245, 36)
                if (pointInRect(x, y, 455, 88, 245, 36)) {
                    auto_exposure_enabled = !auto_exposure_enabled;
                    camera.setExposure(auto_exposure_enabled, exp_val_us);
                    toast_msg = auto_exposure_enabled ? "Auto Exposure: Enabled" : "Auto Exposure: Manual Mode";
                    toast_time = std::chrono::steady_clock::now();
                    return;
                }
                // Shutter Speed Slider Touch Track (Generous hit zone: y in [135, 190])
                if (pointInRect(x, y, 445, 135, 265, 55)) {
                    active_drag = DragTarget::DRAG_EXP_SLIDER;
                    float rel = std::clamp(static_cast<float>(x - 470) / 220.0f, 0.0f, 1.0f);
                    exp_val_us = static_cast<uint32_t>(1000 + rel * 32000);
                    auto_exposure_enabled = false;
                    camera.setExposure(false, exp_val_us);
                    return;
                }
                // Analog Gain Slider Touch Track (Generous hit zone: y in [205, 260])
                if (pointInRect(x, y, 445, 205, 265, 55)) {
                    active_drag = DragTarget::DRAG_GAIN_SLIDER;
                    float rel = std::clamp(static_cast<float>(x - 470) / 220.0f, 0.0f, 1.0f);
                    gain_val = 1.0f + rel * 15.0f;
                    camera.setGain(gain_val);
                    return;
                }
                // AWB Mode Buttons (4 buttons at y: 285..335)
                for (int b = 0; b < 4; ++b) {
                    int bx = 465 + b * 59;
                    if (pointInRect(x, y, bx - 3, 285, 60, 45)) {
                        cur_awb_idx = b;
                        camera.setAwbMode(cur_awb_idx);
                        toast_msg = "AWB Mode: " + AWB_NAMES[cur_awb_idx];
                        toast_time = std::chrono::steady_clock::now();
                        return;
                    }
                }
                return;
            } else if (!pointInRect(x, y, 715, 310, 60, 60)) {
                // Tapping outside drawer dismisses it
                show_settings = false;
            }
        }

        // 6. Left Pod Segmented View Toggle Tabs
        // Tab 1: [🎠 MODES] (16, 50, 98, 34)
        if (pointInRect(x, y, 16, 50, 98, 34)) {
            active_view = LeftViewMode::CAROUSEL;
            toast_msg = "Mode: Radial Carousel";
            toast_time = std::chrono::steady_clock::now();
            return;
        }
        // Tab 2: [🔍 FOCUS] (118, 50, 98, 34)
        if (pointInRect(x, y, 118, 50, 98, 34)) {
            active_view = LeftViewMode::FOCUS_JOG;
            toast_msg = "Mode: Focus Jog Dial";
            toast_time = std::chrono::steady_clock::now();
            return;
        }

        // 7. Left Zone: Focus Jog View Controls
        if (active_view == LeftViewMode::FOCUS_JOG) {
            // Fine Nudge [-] (16, 396, 60, 36)
            if (pointInRect(x, y, 16, 396, 60, 36)) {
                slider_focus_dpt = std::clamp(slider_focus_dpt - 0.1f, 0.0f, 10.0f);
                auto_focus_active = false;
                camera.setManualFocus(slider_focus_dpt);
                return;
            }
            // Quick AF [⚡ AF] (80, 396, 70, 36)
            if (pointInRect(x, y, 80, 396, 70, 36)) {
                auto_focus_active = true;
                camera.triggerAutofocus();
                toast_msg = "⚡ Autofocus Sweeping VCM Gradient...";
                toast_time = std::chrono::steady_clock::now();
                return;
            }
            // Fine Nudge [+] (154, 396, 60, 36)
            if (pointInRect(x, y, 154, 396, 60, 36)) {
                slider_focus_dpt = std::clamp(slider_focus_dpt + 0.1f, 0.0f, 10.0f);
                auto_focus_active = false;
                camera.setManualFocus(slider_focus_dpt);
                return;
            }
            // Focus Radial Arc Drag Hit-Test (x in [0, 240], y in [88, 395])
            if (x <= 240 && y >= 88 && y <= 395) {
                active_drag = DragTarget::DRAG_FOCUS_JOG;
                float angle = std::atan2(static_cast<float>(y - ARC_CENTER_Y), static_cast<float>(x - ARC_CENTER_X));
                float norm = std::clamp((angle - ARC_MIN_ANG) / (ARC_MAX_ANG - ARC_MIN_ANG), 0.0f, 1.0f);
                slider_focus_dpt = norm * 10.0f;
                auto_focus_active = false;
                camera.setManualFocus(slider_focus_dpt);
                return;
            }
        } else {
            // 8. Left Zone: Carousel Mode Controls
            constexpr float ITEM_SPACING = 0.42f;
            // Check if tapped directly on an orbiting card (precise bounding box test)
            for (int i = 0; i < 5; ++i) {
                float theta = carousel_angle + i * ITEM_SPACING;
                if (std::abs(theta) < 1.05f) {
                    float ix = ARC_CENTER_X + ARC_RADIUS * std::cos(theta);
                    float iy = ARC_CENTER_Y + ARC_RADIUS * std::sin(theta);
                    bool is_sel = (std::abs(theta) < 0.20f);
                    int card_w = is_sel ? 122 : 102;
                    int card_h = is_sel ? 36 : 30;
                    if (pointInRect(x, y, static_cast<int>(ix) - 20, static_cast<int>(iy) - card_h / 2 - 5, card_w + 15, card_h + 10)) {
                        carousel_target_angle = -i * ITEM_SPACING;
                        current_carousel_idx = i;
                        if (i == 0) { // FOCUS
                            active_view = LeftViewMode::FOCUS_JOG;
                            toast_msg = "🔍 Focus Jog Dial Activated";
                        } else if (i == 1) { // AF SWEEP
                            auto_focus_active = true;
                            camera.triggerAutofocus();
                            toast_msg = "⚡ Autofocus Sweeping...";
                        } else if (i == 2) { // RESOLUTION
                            cur_res_idx = (cur_res_idx + 1) % RES_MODES.size();
                            camera.switchResolution(RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h, RES_MODES[cur_res_idx].fps);
                            if (video_texture) SDL_DestroyTexture(video_texture);
                            video_texture = SDL_CreateTexture(
                                renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING,
                                RES_MODES[cur_res_idx].w, RES_MODES[cur_res_idx].h
                            );
                            toast_msg = "Mode: " + RES_MODES[cur_res_idx].label;
                        } else if (i == 3) { // EXPOSURE
                            show_settings = !show_settings;
                            toast_msg = show_settings ? "Parameters Opened" : "Parameters Closed";
                        } else if (i == 4) { // WHITE BALANCE
                            cur_awb_idx = (cur_awb_idx + 1) % 4;
                            camera.setAwbMode(cur_awb_idx);
                            toast_msg = "AWB: " + AWB_NAMES[cur_awb_idx];
                        }
                        toast_time = std::chrono::steady_clock::now();
                        return;
                    }
                }
            }
            // If not card tap, begin carousel angular drag along arc
            if (x <= 280) {
                active_drag = DragTarget::DRAG_CAROUSEL_WHEEL;
                drag_start_angle = std::atan2(static_cast<float>(y - ARC_CENTER_Y), static_cast<float>(x - ARC_CENTER_X));
                drag_last_angle = drag_start_angle;
                is_dragging_carousel = false;
                return;
            }
        }
    };

    // Lambda: Unified Pointer Motion (Real-Time Touch Dragging)
    auto handlePointerMotion = [&](int x, int y) {
        if (active_drag == DragTarget::DRAG_FOCUS_JOG) {
            float angle = std::atan2(static_cast<float>(y - ARC_CENTER_Y), static_cast<float>(x - ARC_CENTER_X));
            float norm = std::clamp((angle - ARC_MIN_ANG) / (ARC_MAX_ANG - ARC_MIN_ANG), 0.0f, 1.0f);
            slider_focus_dpt = norm * 10.0f;
            auto_focus_active = false;
            camera.setManualFocus(slider_focus_dpt);
        } else if (active_drag == DragTarget::DRAG_CAROUSEL_WHEEL) {
            float cur_ang = std::atan2(static_cast<float>(y - ARC_CENTER_Y), static_cast<float>(x - ARC_CENTER_X));
            float d_theta = cur_ang - drag_last_angle;
            if (std::abs(cur_ang - drag_start_angle) > 0.04f) {
                is_dragging_carousel = true;
            }
            carousel_angle += d_theta;
            constexpr float ITEM_SPACING = 0.42f;
            carousel_angle = std::clamp(carousel_angle, -4.0f * ITEM_SPACING - 0.25f, 0.25f);
            drag_last_angle = cur_ang;
        } else if (active_drag == DragTarget::DRAG_EXP_SLIDER) {
            float rel = std::clamp(static_cast<float>(x - 470) / 220.0f, 0.0f, 1.0f);
            exp_val_us = static_cast<uint32_t>(1000 + rel * 32000);
            auto_exposure_enabled = false;
            camera.setExposure(false, exp_val_us);
        } else if (active_drag == DragTarget::DRAG_GAIN_SLIDER) {
            float rel = std::clamp(static_cast<float>(x - 470) / 220.0f, 0.0f, 1.0f);
            gain_val = 1.0f + rel * 15.0f;
            camera.setGain(gain_val);
        }
    };

    // Lambda: Unified Pointer Up
    auto handlePointerUp = [&](int x, int y) {
        (void)x;
        (void)y;
        if (active_drag == DragTarget::DRAG_CAROUSEL_WHEEL) {
            constexpr float ITEM_SPACING = 0.42f;
            float nearest_idx = std::round(-carousel_angle / ITEM_SPACING);
            nearest_idx = std::clamp(nearest_idx, 0.0f, 4.0f);
            carousel_target_angle = -nearest_idx * ITEM_SPACING;
            current_carousel_idx = static_cast<int>(nearest_idx);
        }
        active_drag = DragTarget::DRAG_NONE;
    };

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
                    active_view = LeftViewMode::FOCUS_JOG;
                    std::stringstream ss;
                    ss << "Focus: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt ("
                       << camera.getRawVcmPosition() << ")";
                    toast_msg = ss.str();
                    toast_time = std::chrono::steady_clock::now();
                }
            }
        }

        // 2. Poll SDL events (Full Touch & Mouse Dragging Engine)
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_RETURN) {
                    triggerSnapshot();
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                float step_dir = (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
                float delta = (float)ev.wheel.y * step_dir;
                if (active_view == LeftViewMode::CAROUSEL) {
                    constexpr float ITEM_SPACING = 0.42f;
                    carousel_target_angle = std::clamp(carousel_target_angle - delta * ITEM_SPACING, -4.0f * ITEM_SPACING, 0.0f);
                    current_carousel_idx = static_cast<int>(std::round(-carousel_target_angle / ITEM_SPACING));
                } else {
                    slider_focus_dpt = std::clamp(slider_focus_dpt + delta * 0.20f, 0.0f, 10.0f);
                    auto_focus_active = false;
                    camera.setManualFocus(slider_focus_dpt);
                    std::stringstream ss;
                    ss << "Focus: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt";
                    toast_msg = ss.str();
                    toast_time = std::chrono::steady_clock::now();
                }
            } else if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_LEAVE || ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    active_drag = DragTarget::DRAG_NONE;
                    active_finger_id = -1;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT) {
                    handlePointerDown(ev.button.x, ev.button.y);
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                if (ev.motion.which != SDL_TOUCH_MOUSEID) {
                    if (active_drag != DragTarget::DRAG_NONE || (ev.motion.state & SDL_BUTTON_LMASK)) {
                        handlePointerMotion(ev.motion.x, ev.motion.y);
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT) {
                    handlePointerUp(ev.button.x, ev.button.y);
                }
            } else if (ev.type == SDL_FINGERDOWN) {
                if (active_finger_id == -1) {
                    active_finger_id = ev.tfinger.fingerId;
                    int px = static_cast<int>(ev.tfinger.x * SCREEN_WIDTH);
                    int py = static_cast<int>(ev.tfinger.y * SCREEN_HEIGHT);
                    handlePointerDown(px, py);
                }
            } else if (ev.type == SDL_FINGERMOTION) {
                if (ev.tfinger.fingerId == active_finger_id) {
                    int px = static_cast<int>(ev.tfinger.x * SCREEN_WIDTH);
                    int py = static_cast<int>(ev.tfinger.y * SCREEN_HEIGHT);
                    handlePointerMotion(px, py);
                }
            } else if (ev.type == SDL_FINGERUP) {
                if (ev.tfinger.fingerId == active_finger_id) {
                    active_finger_id = -1;
                    int px = static_cast<int>(ev.tfinger.x * SCREEN_WIDTH);
                    int py = static_cast<int>(ev.tfinger.y * SCREEN_HEIGHT);
                    handlePointerUp(px, py);
                }
            }
        }

        // Smooth Carousel spring snap animation
        if (active_drag != DragTarget::DRAG_CAROUSEL_WHEEL) {
            carousel_angle += (carousel_target_angle - carousel_angle) * 0.25f;
        }

        // 3. Fetch latest camera frame
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

        // Clear and draw video viewfinder
        SDL_RenderClear(renderer);
        if (video_texture) {
            SDL_RenderCopy(renderer, video_texture, NULL, NULL);
        }

        // 4. Center Cyber Reticle & Dynamic Sharpness Meter
        float pulse = std::sin(anim_t * 5.0f);
        AFState af_st = camera.getAFState();
        uint8_t ret_r = 0, ret_g = 245, ret_b = 255;
        if (af_st == AFState::SWEEP_COARSE || af_st == AFState::SWEEP_FINE) {
            ret_r = 245; ret_g = 158; ret_b = 11;
        } else if (af_st == AFState::LOCKED) {
            ret_r = 16; ret_g = 185; ret_b = 129;
        }
        drawCyberReticle(renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 76, ret_r, ret_g, ret_b, 220, pulse);

        float sharpness = camera.getSharpness();
        int meter_w = 130;
        int meter_h = 5;
        int meter_x = (SCREEN_WIDTH - meter_w) / 2;
        int meter_y = (SCREEN_HEIGHT / 2) + 48;
        drawFillRect(renderer, meter_x - 1, meter_y - 1, meter_w + 2, meter_h + 2, 6, 10, 20, 200);
        int fill_w = std::clamp(static_cast<int>((sharpness / 800.0f) * meter_w), 2, meter_w);
        drawFillRect(renderer, meter_x, meter_y, fill_w, meter_h, ret_r, ret_g, ret_b, 240);

        // 5. Left-Edge Radial Curved Arc Jog-Wheel System
        // Frosted pod backdrop along left
        drawFillRoundedRect(renderer, -40, 38, 305, 404, 24, 8, 14, 24, 210);
        drawRoundedRect(renderer, -40, 38, 305, 404, 24, 0, 245, 255, 60);

        // Sweeping guide arcs
        drawArc(renderer, static_cast<int>(ARC_CENTER_X), static_cast<int>(ARC_CENTER_Y), static_cast<int>(ARC_RADIUS), ARC_MIN_ANG, ARC_MAX_ANG, 0, 245, 255, 70, 2);
        drawArc(renderer, static_cast<int>(ARC_CENTER_X), static_cast<int>(ARC_CENTER_Y), static_cast<int>(ARC_RADIUS - 30.0f), ARC_MIN_ANG, ARC_MAX_ANG, 255, 255, 255, 30, 1);
        drawArc(renderer, static_cast<int>(ARC_CENTER_X), static_cast<int>(ARC_CENTER_Y), static_cast<int>(ARC_RADIUS + 30.0f), ARC_MIN_ANG, ARC_MAX_ANG, 255, 255, 255, 30, 1);

        if (active_view == LeftViewMode::FOCUS_JOG) {
            // View Mode 1: Precision Focus Scrub Arc
            // Draw radial tick marks along the arc
            constexpr int NUM_TICKS = 21;
            for (int t = 0; t <= NUM_TICKS; ++t) {
                float frac = static_cast<float>(t) / static_cast<float>(NUM_TICKS);
                float a_tick = ARC_MIN_ANG + frac * (ARC_MAX_ANG - ARC_MIN_ANG);
                bool is_major = (t % 5 == 0);
                float r_in = is_major ? (ARC_RADIUS - 16.0f) : (ARC_RADIUS - 8.0f);
                float r_out = is_major ? (ARC_RADIUS + 16.0f) : (ARC_RADIUS + 8.0f);

                int x1 = static_cast<int>(ARC_CENTER_X + r_in * std::cos(a_tick));
                int y1 = static_cast<int>(ARC_CENTER_Y + r_in * std::sin(a_tick));
                int x2 = static_cast<int>(ARC_CENTER_X + r_out * std::cos(a_tick));
                int y2 = static_cast<int>(ARC_CENTER_Y + r_out * std::sin(a_tick));

                uint8_t tr = is_major ? 0 : 148;
                uint8_t tg = is_major ? 245 : 163;
                uint8_t tb = is_major ? 255 : 184;
                uint8_t ta = is_major ? 220 : 120;
                drawThickLine(renderer, x1, y1, x2, y2, is_major ? 2 : 1, tr, tg, tb, ta);
            }

            // Calibrated labels
            txt_dpt_inf.update(renderer, font_tiny, "∞ INF", {0, 245, 255, 220});
            txt_dpt_inf.draw(renderer, 92, 75);

            txt_dpt_mid.update(renderer, font_tiny, "0.5m", {148, 163, 184, 200});
            txt_dpt_mid.draw(renderer, 168, 232);

            txt_dpt_near.update(renderer, font_tiny, "0.1m", {0, 245, 255, 220});
            txt_dpt_near.draw(renderer, 92, 385);

            // Glowing Focus Cursor Thumb Knob on Arc
            float cur_thumb_norm = slider_focus_dpt / 10.0f;
            float cur_thumb_ang = ARC_MIN_ANG + cur_thumb_norm * (ARC_MAX_ANG - ARC_MIN_ANG);
            int thumb_x = static_cast<int>(ARC_CENTER_X + ARC_RADIUS * std::cos(cur_thumb_ang));
            int thumb_y = static_cast<int>(ARC_CENTER_Y + ARC_RADIUS * std::sin(cur_thumb_ang));

            // Cursor Outer Glow & Core
            drawFillCircleFast(renderer, thumb_x, thumb_y, 16, 0, 245, 255, 70);
            drawFillCircleFast(renderer, thumb_x, thumb_y, 11, 10, 15, 26, 240);
            drawCircle(renderer, thumb_x, thumb_y, 11, 0, 245, 255, 255);
            drawIconFocus(renderer, thumb_x, thumb_y, 12, 0, 245, 255, 255);

            // Active readout pill next to thumb
            std::stringstream ss_foc;
            float dist_m = (slider_focus_dpt > 0.05f) ? (1.0f / slider_focus_dpt) : 99.0f;
            if (dist_m > 20.0f) {
                ss_foc << "∞ (" << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt)";
            } else {
                ss_foc << std::fixed << std::setprecision(2) << dist_m << "m (" << std::setprecision(1) << slider_focus_dpt << " dpt)";
            }
            txt_focus_val.update(renderer, font_small, ss_foc.str(), {255, 255, 255, 255});
            int r_box_x = std::clamp(thumb_x + 22, 100, 230);
            int r_box_y = std::clamp(thumb_y - 12, 55, 395);
            drawFillRoundedRect(renderer, r_box_x - 4, r_box_y - 3, txt_focus_val.w + 10, 22, 4, 10, 18, 30, 230);
            drawRoundedRect(renderer, r_box_x - 4, r_box_y - 3, txt_focus_val.w + 10, 22, 4, 0, 245, 255, 140);
            txt_focus_val.draw(renderer, r_box_x + 1, r_box_y + 1);

            // Segmented Mode Tabs: [MODES] and [FOCUS]
            drawFillRoundedRect(renderer, 16, 50, 98, 32, 5, 20, 30, 48, 220);
            drawRoundedRect(renderer, 16, 50, 98, 32, 5, 0, 245, 255, 100);
            drawIconSliders(renderer, 28, 66, 12, 148, 163, 184, 255);
            txt_modes_tab.update(renderer, font_tiny, "MODES", {148, 163, 184, 255});
            txt_modes_tab.draw(renderer, 44, 59);

            drawFillRoundedRect(renderer, 118, 50, 98, 32, 5, 0, 245, 255, 60);
            drawRoundedRect(renderer, 118, 50, 98, 32, 5, 0, 245, 255, 255);
            drawIconFocus(renderer, 130, 66, 12, 0, 245, 255, 255);
            txt_focus_tab.update(renderer, font_tiny, "FOCUS", {0, 245, 255, 255});
            txt_focus_tab.draw(renderer, 146, 59);

            // Fine Nudge [-], AF, and [+]
            drawFillRoundedRect(renderer, 16, 396, 60, 34, 5, 20, 30, 48, 220);
            drawRoundedRect(renderer, 16, 396, 60, 34, 5, 0, 245, 255, 120);
            txt_m.update(renderer, font_small, "- 0.1", {255, 255, 255, 255});
            txt_m.drawCentered(renderer, 46, 413);

            drawFillRoundedRect(renderer, 80, 396, 70, 34, 5, 16, 185, 129, auto_focus_active ? 220 : 120);
            drawRoundedRect(renderer, 80, 396, 70, 34, 5, 16, 185, 129, 255);
            txt_af_btn.update(renderer, font_small, "⚡ AF", {255, 255, 255, 255});
            txt_af_btn.drawCentered(renderer, 115, 413);

            drawFillRoundedRect(renderer, 154, 396, 60, 34, 5, 20, 30, 48, 220);
            drawRoundedRect(renderer, 154, 396, 60, 34, 5, 0, 245, 255, 120);
            txt_p.update(renderer, font_small, "+ 0.1", {255, 255, 255, 255});
            txt_p.drawCentered(renderer, 184, 413);
        } else {
            // View Mode 2: Radial Curved Arc Carousel Navigation
            constexpr float ITEM_SPACING = 0.42f;
            const char* ITEM_NAMES[5] = { "FOCUS", "AF SWEEP", "MODE / FPS", "EXPOSURE", "WHITE BAL" };

            for (int i = 0; i < 5; ++i) {
                float theta = carousel_angle + i * ITEM_SPACING;
                if (std::abs(theta) > 1.05f) continue;

                int ix = static_cast<int>(ARC_CENTER_X + ARC_RADIUS * std::cos(theta));
                int iy = static_cast<int>(ARC_CENTER_Y + ARC_RADIUS * std::sin(theta));

                bool is_sel = (std::abs(theta) < 0.20f);
                int card_w = is_sel ? 122 : 102;
                int card_h = is_sel ? 36 : 30;

                uint8_t bg_a = is_sel ? 230 : 150;
                uint8_t border_r = is_sel ? 0 : 71;
                uint8_t border_g = is_sel ? 245 : 85;
                uint8_t border_b = is_sel ? 255 : 105;

                drawFillRoundedRect(renderer, ix - 16, iy - card_h / 2, card_w, card_h, 6, 12, 20, 32, bg_a);
                drawRoundedRect(renderer, ix - 16, iy - card_h / 2, card_w, card_h, 6, border_r, border_g, border_b, is_sel ? 240 : 120);

                // Draw Vector Icon in card
                int icon_x = ix - 2;
                int icon_y = iy;
                if (i == 0) drawIconFocus(renderer, icon_x, icon_y, 14, border_r, border_g, border_b, 255);
                else if (i == 1) drawIconBolt(renderer, icon_x, icon_y, 14, 16, 185, 129, 255);
                else if (i == 2) drawIconResolution(renderer, icon_x, icon_y, 14, border_r, border_g, border_b, 255);
                else if (i == 3) drawIconSliders(renderer, icon_x, icon_y, 14, border_r, border_g, border_b, 255);
                else if (i == 4) drawIconSun(renderer, icon_x, icon_y, 14, 245, 158, 11, 255);

                txt_carousel_items[i].update(renderer, font_tiny, ITEM_NAMES[i], is_sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{148, 163, 184, 220});
                txt_carousel_items[i].draw(renderer, ix + 12, iy - txt_carousel_items[i].h / 2);
            }

            // Segmented Mode Tabs: [MODES] and [FOCUS]
            drawFillRoundedRect(renderer, 16, 50, 98, 32, 5, 0, 245, 255, 60);
            drawRoundedRect(renderer, 16, 50, 98, 32, 5, 0, 245, 255, 255);
            drawIconSliders(renderer, 28, 66, 12, 0, 245, 255, 255);
            txt_modes_tab.update(renderer, font_tiny, "MODES", {0, 245, 255, 255});
            txt_modes_tab.draw(renderer, 44, 59);

            drawFillRoundedRect(renderer, 118, 50, 98, 32, 5, 20, 30, 48, 220);
            drawRoundedRect(renderer, 118, 50, 98, 32, 5, 0, 245, 255, 100);
            drawIconFocus(renderer, 130, 66, 12, 148, 163, 184, 255);
            txt_focus_tab.update(renderer, font_tiny, "FOCUS", {148, 163, 184, 255});
            txt_focus_tab.draw(renderer, 146, 59);
        }

        // 6. Right-Edge Ergonomic Action Pod
        // Primary Shutter Button (Center: 745, 240, Outer R=34, Inner R=26)
        drawFillCircleFast(renderer, 745, 240, 35, 10, 15, 26, 230);
        drawCircle(renderer, 745, 240, 35, 255, 255, 255, 160);
        drawFillCircleFast(renderer, 745, 240, 28, 239, 68, 68, 240);
        drawCircle(renderer, 745, 240, 28, 255, 255, 255, 255);
        drawIconCamera(renderer, 745, 240, 18, 255, 255, 255, 255);

        // Quick AF Button (Center: 745, 140, R=26)
        drawFillCircleFast(renderer, 745, 140, 26, 12, 20, 32, 230);
        drawCircle(renderer, 745, 140, 26, 16, 185, 129, 220);
        drawIconBolt(renderer, 745, 137, 14, 16, 185, 129, 255);
        txt_af_label.update(renderer, font_tiny, "AF", {16, 185, 129, 255});
        txt_af_label.drawCentered(renderer, 745, 154);

        // Settings Drawer Button (Center: 745, 340, R=26)
        drawFillCircleFast(renderer, 745, 340, 26, 12, 20, 32, 230);
        drawCircle(renderer, 745, 340, 26, 0, 245, 255, 180);
        drawIconSliders(renderer, 745, 337, 16, 0, 245, 255, 255);
        txt_set_btn_lbl.update(renderer, font_tiny, "SET", {0, 245, 255, 255});
        txt_set_btn_lbl.drawCentered(renderer, 745, 354);

        // 7. Dedicated Touch Exit Button in Top-Right Corner (715, 10, 75, 36)
        drawFillRoundedRect(renderer, 715, 10, 75, 36, 6, 239, 68, 68, 230);
        drawRoundedRect(renderer, 715, 10, 75, 36, 6, 255, 255, 255, 220);
        drawIconClose(renderer, 730, 28, 10, 255, 255, 255, 255);
        txt_exit.update(renderer, font_small, "EXIT", {255, 255, 255, 255});
        txt_exit.draw(renderer, 744, 20);

        // 8. Top Glass HUD Header Bar (10, 10, 695, 36)
        drawFillRoundedRect(renderer, 10, 10, 695, 36, 6, 8, 14, 24, 215);
        drawRoundedRect(renderer, 10, 10, 695, 36, 6, 0, 245, 255, 80);

        // Title & Logo
        drawIconCamera(renderer, 26, 28, 16, 0, 245, 255, 255);
        txt_logo.update(renderer, font_medium, "IMX519 AI-OPTICS", {0, 245, 255, 255});
        txt_logo.draw(renderer, 42, 19);

        // Lens Readout
        std::stringstream ss_hdr;
        ss_hdr << RES_MODES[cur_res_idx].w << "x" << RES_MODES[cur_res_idx].h << " @" << RES_MODES[cur_res_idx].fps << "fps | ";
        if (af_st == AFState::SWEEP_COARSE || af_st == AFState::SWEEP_FINE) {
            ss_hdr << "AF: SWEEPING...";
        } else if (af_st == AFState::LOCKED) {
            ss_hdr << "AF: LOCKED";
        } else {
            ss_hdr << "FOC: " << std::fixed << std::setprecision(1) << slider_focus_dpt << " dpt";
        }
        ss_hdr << " (VCM " << camera.getRawVcmPosition() << ")";
        txt_lens_info.update(renderer, font_small, ss_hdr.str(), {148, 163, 184, 255});
        txt_lens_info.draw(renderer, 205, 20);

        // Performance Telemetry
        std::stringstream ss_tel;
        ss_tel << std::fixed << std::setprecision(0) << smoothed_fps << " FPS | "
               << (int)getCpuUsagePercent() << "% CPU | " << (int)getMemoryRssMb() << " MB";
        txt_telemetry.update(renderer, font_tiny, ss_tel.str(), {16, 185, 129, 255});
        txt_telemetry.draw(renderer, 535, 22);

        // 9. Modern Parameter Flyout Drawer (if show_settings is true)
        if (show_settings) {
            drawFillRoundedRect(renderer, 450, 55, 260, 365, 8, 10, 18, 30, 245);
            drawRoundedRect(renderer, 450, 55, 260, 365, 8, 0, 245, 255, 140);

            // Title & Close Button
            txt_set_title.update(renderer, font_medium, "CAMERA PARAMETERS", {0, 245, 255, 255});
            txt_set_title.draw(renderer, 465, 68);
            drawIconClose(renderer, 688, 77, 12, 239, 68, 68, 255);

            // Auto Exposure Toggle
            drawFillRoundedRect(renderer, 465, 95, 230, 32, 5, auto_exposure_enabled ? 16 : 30, auto_exposure_enabled ? 185 : 41, auto_exposure_enabled ? 129 : 59, 220);
            drawRoundedRect(renderer, 465, 95, 230, 32, 5, 255, 255, 255, 100);
            txt_ae_btn.update(renderer, font_small, auto_exposure_enabled ? "AUTO EXPOSURE: [ACTIVE]" : "AUTO EXPOSURE: [MANUAL]", {255, 255, 255, 255});
            txt_ae_btn.drawCentered(renderer, 580, 111);

            // Shutter Speed Slider
            std::stringstream ss_shutter;
            ss_shutter << "SHUTTER: " << (exp_val_us / 1000.0f) << "ms";
            txt_exp_label.update(renderer, font_tiny, ss_shutter.str(), {148, 163, 184, 255});
            txt_exp_label.draw(renderer, 465, 140);

            drawFillRoundedRect(renderer, 465, 158, 230, 22, 4, 15, 23, 42, 220);
            drawRoundedRect(renderer, 465, 158, 230, 22, 4, 0, 245, 255, 100);
            float exp_norm = std::clamp((exp_val_us - 1000.0f) / 32000.0f, 0.0f, 1.0f);
            int exp_knob_x = 465 + static_cast<int>(exp_norm * 220.0f);
            drawFillCircleFast(renderer, exp_knob_x + 5, 169, 8, 0, 245, 255, 255);

            // Analog Gain Slider
            std::stringstream ss_gain;
            ss_gain << "ANALOG GAIN: " << std::fixed << std::setprecision(1) << gain_val << "x";
            txt_gain_label.update(renderer, font_tiny, ss_gain.str(), {148, 163, 184, 255});
            txt_gain_label.draw(renderer, 465, 208);

            drawFillRoundedRect(renderer, 465, 228, 230, 22, 4, 15, 23, 42, 220);
            drawRoundedRect(renderer, 465, 228, 230, 22, 4, 0, 245, 255, 100);
            float gain_norm = std::clamp((gain_val - 1.0f) / 15.0f, 0.0f, 1.0f);
            int gain_knob_x = 465 + static_cast<int>(gain_norm * 220.0f);
            drawFillCircleFast(renderer, gain_knob_x + 5, 239, 8, 16, 185, 129, 255);

            // White Balance Presets
            txt_awb_title.update(renderer, font_tiny, "WHITE BALANCE PRESETS", {148, 163, 184, 255});
            txt_awb_title.draw(renderer, 465, 275);

            const char* awb_chips[4] = {"AUTO", "SUN", "FLUO", "TUNG"};
            for (int b = 0; b < 4; ++b) {
                int bx = 465 + b * 59;
                bool is_cur = (cur_awb_idx == b);
                drawFillRoundedRect(renderer, bx, 295, 54, 30, 4, is_cur ? 0 : 20, is_cur ? 245 : 30, is_cur ? 255 : 48, is_cur ? 220 : 160);
                drawRoundedRect(renderer, bx, 295, 54, 30, 4, 0, 245, 255, is_cur ? 255 : 80);
                txt_awb_btn[b].update(renderer, font_tiny, awb_chips[b], is_cur ? SDL_Color{10, 15, 26, 255} : SDL_Color{255, 255, 255, 255});
                txt_awb_btn[b].drawCentered(renderer, bx + 27, 310);
            }
        }

        // 10. Shutter Flash Animation (fade out over 150ms)
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

        // 11. Toast Message Banner (Bottom Center)
        auto now_t = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now_t - toast_time).count() < 3.0f) {
            txt_toast.update(renderer, font_small, toast_msg, {255, 255, 255, 255});
            int tw = txt_toast.w + 28;
            int tx = (SCREEN_WIDTH - tw) / 2;
            drawFillRoundedRect(renderer, tx, 428, tw, 30, 6, 8, 14, 24, 230);
            drawRoundedRect(renderer, tx, 428, tw, 30, 6, 0, 245, 255, 180);
            txt_toast.draw(renderer, tx + 14, 434);
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
    txt_telemetry.destroy();
    txt_lens_info.destroy();
    txt_toast.destroy();
    txt_exit.destroy();
    txt_shutter_label.destroy();
    txt_af_label.destroy();
    txt_modes_btn.destroy();
    txt_af_btn.destroy();
    for (int i = 0; i < 5; ++i) txt_carousel_items[i].destroy();
    txt_focus_val.destroy();
    txt_vcm_val.destroy();
    txt_dpt_inf.destroy();
    txt_dpt_mid.destroy();
    txt_dpt_near.destroy();
    txt_set_title.destroy();
    txt_ae_btn.destroy();
    txt_exp_label.destroy();
    txt_gain_label.destroy();
    for (int b = 0; b < 4; ++b) txt_awb_btn[b].destroy();
    txt_awb_title.destroy();
    txt_m.destroy();
    txt_p.destroy();
    txt_set_btn_lbl.destroy();
    txt_modes_tab.destroy();
    txt_focus_tab.destroy();

    if (video_texture) SDL_DestroyTexture(video_texture);
    if (font_tiny) TTF_CloseFont(font_tiny);
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
