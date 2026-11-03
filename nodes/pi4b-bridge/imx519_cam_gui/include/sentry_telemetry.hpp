#pragma once
#include <string>
#include <iostream>

namespace SentryTelemetry {
    inline void init(const std::string& dsn = "") {
        (void)dsn;
        std::cout << "[Sentry] Telemetry initialized.\n";
    }

    inline void recordMetrics(double fps, double ram_mb, double cpu_percent) {
        std::cout << "[Sentry Metric] FPS=" << fps
                  << " RAM=" << ram_mb << "MB CPU=" << cpu_percent << "%\n";
    }

    inline void captureException(const std::string& error_msg) {
        std::cerr << "[Sentry Alert] " << error_msg << "\n";
    }

    inline void shutdown() {
        std::cout << "[Sentry] Shutdown clean.\n";
    }
}
