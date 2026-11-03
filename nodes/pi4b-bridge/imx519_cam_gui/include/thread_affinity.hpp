#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <sched.h>
#include <iostream>

namespace SystemPerf {
    // Pin calling thread to specific CPU core to prevent core-hopping and cache invalidation
    inline bool pinToCore(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_t current_thread = pthread_self();
        int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            std::cerr << "[ThreadAffinity] Failed to bind to Core " << core_id << "\n";
            return false;
        }
        return true;
    }

    // Set real-time FIFO priority for time-critical camera frame ingestion
    inline bool setRealtimePriority(int priority = 20) {
        struct sched_param param;
        param.sched_priority = priority;
        int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        return (rc == 0);
    }
}
