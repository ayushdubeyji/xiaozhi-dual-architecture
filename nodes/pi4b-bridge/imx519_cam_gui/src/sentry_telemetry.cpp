#include "sentry_telemetry.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <csignal>
#include <ctime>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <thread>

std::string SentryTelemetry::s_dsn = "";
std::string SentryTelemetry::s_host = "";
std::string SentryTelemetry::s_project_id = "";
std::string SentryTelemetry::s_public_key = "";
bool SentryTelemetry::s_initialized = false;

static void crashHandler(int sig) {
    const char msg[] = "\n[CRASH] Fatal signal intercepted! Exiting cleanly.\n";
    [[maybe_unused]] auto w = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(128 + sig);
}

void SentryTelemetry::parseDsn(const std::string& dsn) {
    auto at_pos = dsn.find('@');
    auto proto_pos = dsn.find("://");
    if (proto_pos == std::string::npos || at_pos == std::string::npos) return;

    s_public_key = dsn.substr(proto_pos + 3, at_pos - (proto_pos + 3));
    auto slash_pos = dsn.find('/', at_pos);
    if (slash_pos == std::string::npos) return;

    s_host = dsn.substr(at_pos + 1, slash_pos - (at_pos + 1));
    s_project_id = dsn.substr(slash_pos + 1);
}

void SentryTelemetry::init(const std::string& dsn) {
    s_dsn = dsn;
    parseDsn(dsn);
    s_initialized = !s_host.empty() && !s_project_id.empty();

    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);
    signal(SIGILL, crashHandler);
    signal(SIGBUS, crashHandler);

    std::cout << "[Sentry] Initialized with host: " << s_host << " project: " << s_project_id << "\n";
    captureMessage("info", "IMX519 Bare-Bones Camera GUI Started", {
        {"device", "Raspberry Pi 4 Model B"},
        {"display", "SmartElex 4.3 800x480 DPI ST7265"},
        {"subsystem", "Camera & Telemetry"}
    });
}

void SentryTelemetry::sendEnvelope(const std::string& payload) {
    if (!s_initialized) return;

    std::thread([host = s_host, proj = s_project_id, key = s_public_key, payload]() {
        char tmp_path[] = "/tmp/sentry_XXXXXX";
        int fd = mkstemp(tmp_path);
        if (fd < 0) return;
        [[maybe_unused]] auto w = write(fd, payload.data(), payload.size());
        close(fd);

        std::string cmd = "curl -s -m 5 -X POST 'https://" + host + "/api/" + proj + "/envelope/' "
                          "-H 'X-Sentry-Auth: Sentry sentry_version=7, sentry_client=imx519-cam-gui/1.0, sentry_key=" + key + "' "
                          "-H 'Content-Type: application/x-sentry-envelope' "
                          "--data-binary @" + std::string(tmp_path) + " >/dev/null 2>&1; rm -f " + std::string(tmp_path);
        [[maybe_unused]] int r = system(cmd.c_str());
    }).detach();
}

void SentryTelemetry::captureMessage(const std::string& level, const std::string& message, const std::map<std::string, std::string>& tags) {
    if (!s_initialized) return;

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now_c));

    std::stringstream item_ss;
    item_ss << "{"
            << "\"level\":\"" << level << "\","
            << "\"message\":{\"formatted\":\"" << message << "\"},"
            << "\"timestamp\":\"" << buf << "\","
            << "\"platform\":\"native\","
            << "\"tags\":{";
    bool first = true;
    for (const auto& kv : tags) {
        if (!first) item_ss << ",";
        item_ss << "\"" << kv.first << "\":\"" << kv.second << "\"";
        first = false;
    }
    item_ss << "}}";

    std::string item = item_ss.str();
    std::stringstream env_ss;
    env_ss << "{\"sent_at\":\"" << buf << "\"}\n"
           << "{\"type\":\"event\",\"length\":" << item.size() << "}\n"
           << item << "\n";

    sendEnvelope(env_ss.str());
}

void SentryTelemetry::captureCrash(int sig, const std::string& sig_name) {
    captureMessage("fatal", "Fatal process crash received: " + sig_name, {
        {"signal", std::to_string(sig)},
        {"fault", sig_name}
    });
}

void SentryTelemetry::recordMetrics(double fps, double ram_mb, double cpu_percent) {
    if (!s_initialized) return;
    captureMessage("info", "Performance Telemetry Checkpoint", {
        {"fps", std::to_string(fps)},
        {"ram_rss_mb", std::to_string(ram_mb)},
        {"cpu_percent", std::to_string(cpu_percent)}
    });
}

void SentryTelemetry::shutdown() {
    captureMessage("info", "IMX519 Camera GUI cleanly terminated");
}
