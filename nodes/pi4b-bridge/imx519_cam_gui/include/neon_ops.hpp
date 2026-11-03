#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace SimdOps {
    inline void neon_memcpy_fast(void* __restrict dest, const void* __restrict src, size_t n_bytes) noexcept {
#if defined(__ARM_NEON) || defined(__aarch64__)
        uint8_t* d = static_cast<uint8_t*>(dest);
        const uint8_t* s = static_cast<const uint8_t*>(src);
        size_t chunks = n_bytes / 64;
        size_t remainder = n_bytes % 64;

        while (chunks--) {
            __builtin_prefetch(s + 128, 0, 0);
            uint8x16x4_t q = vld1q_u8_x4(s);
            vst1q_u8_x4(d, q);
            s += 64;
            d += 64;
        }

        while (remainder--) {
            *d++ = *s++;
        }
#else
        __builtin_memcpy(dest, src, n_bytes);
#endif
    }

    // High-performance Tenengrad Focus Sharpness Score on center reticle region
    inline float compute_focus_sharpness(const uint8_t* buffer, int width, int height) noexcept {
        if (!buffer || width < 220 || height < 180) return 0.0f;

        const int box_w = 200;
        const int box_h = 160;
        const int start_x = (width - box_w) / 2;
        const int start_y = (height - box_h) / 2;
        const int stride = width * 3;

        uint64_t sum_energy = 0;
        int count = 0;

        for (int y = start_y + 1; y < start_y + box_h - 1; y += 2) {
            const uint8_t* row_prev = buffer + ((y - 1) * stride);
            const uint8_t* row_curr = buffer + (y * stride);
            const uint8_t* row_next = buffer + ((y + 1) * stride);

            for (int x = start_x + 1; x < start_x + box_w - 1; x += 2) {
                auto get_y = [](const uint8_t* p) -> int {
                    return (static_cast<int>(p[0]) + (static_cast<int>(p[1]) << 1) + static_cast<int>(p[2])) >> 2;
                };

                int y_c_prev = get_y(row_curr + ((x - 1) * 3));
                int y_c_next = get_y(row_curr + ((x + 1) * 3));
                int y_p_curr = get_y(row_prev + (x * 3));
                int y_n_curr = get_y(row_next + (x * 3));

                int dx = y_c_next - y_c_prev;
                int dy = y_n_curr - y_p_curr;

                sum_energy += static_cast<uint64_t>(dx * dx + dy * dy);
                count++;
            }
        }

        if (count == 0) return 0.0f;
        return static_cast<float>(sum_energy) / static_cast<float>(count);
    }
}