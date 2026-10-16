// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the few OS facilities the statistics and diagnostics use (thread ids, CPU time and
// resource usage), for Linux and Windows. Windows versions live in bbport_platform.cpp so that
// <windows.h> and its macros (near, MemoryBarrier, ...) stay out of the renderer's headers.

#pragma once

#include <cstddef>
#include <cstdint>
#ifndef _WIN32
#include <pthread.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

namespace BbPlatform {

struct Usage {
    std::uint64_t user_us = 0, sys_us = 0, invol_switches = 0, vol_switches = 0, minor_faults = 0;
};

#ifdef _WIN32
/// CPU time and scheduling counters of the calling thread (thread = true) or of the process.
/// Windows has no per-thread context switch or fault counters: those stay 0 for threads.
bool GetUsage(bool thread, Usage& out);
/// OS id of the calling thread.
std::uint32_t CurrentThreadId();
/// A CPU clock of the calling thread that other threads can read (-1: unavailable): a
/// duplicated thread handle (handles fit in 32 bits).
int ThreadCpuClock();
/// CPU time in nanoseconds of the thread behind a ThreadCpuClock() value.
std::uint64_t ReadThreadCpuClockNs(int clock);
/// "module+0xoffset" for a code address (diagnostics).
void DescribeAddress(const void* address, char* out, std::size_t size);
/// The calling thread's name (diagnostics); valid while the thread lives.
const char* CurrentThreadName();
#else
inline bool GetUsage(bool thread, Usage& out) {
    rusage usage{};
    if (getrusage(thread ? RUSAGE_THREAD : RUSAGE_SELF, &usage) != 0) {
        return false;
    }
    out.user_us = std::uint64_t(usage.ru_utime.tv_sec) * 1000000 + usage.ru_utime.tv_usec;
    out.sys_us = std::uint64_t(usage.ru_stime.tv_sec) * 1000000 + usage.ru_stime.tv_usec;
    out.invol_switches = usage.ru_nivcsw;
    out.vol_switches = usage.ru_nvcsw;
    out.minor_faults = usage.ru_minflt;
    return true;
}

inline std::uint32_t CurrentThreadId() {
    return static_cast<std::uint32_t>(gettid());
}

/// Linux: the thread's clockid_t.
inline int ThreadCpuClock() {
    if (clockid_t clock; pthread_getcpuclockid(pthread_self(), &clock) == 0) {
        return static_cast<int>(clock);
    }
    return -1;
}

inline std::uint64_t ReadThreadCpuClockNs(int clock) {
    timespec ts{};
    clock_gettime(static_cast<clockid_t>(clock), &ts);
    return std::uint64_t(ts.tv_sec) * 1000000000ull + std::uint64_t(ts.tv_nsec);
}

inline const char* CurrentThreadName() {
    thread_local char name[64] = {};
    if (!name[0] && pthread_getname_np(pthread_self(), name, sizeof(name)) != 0) {
        name[0] = '?';
    }
    return name;
}
#endif

} // namespace BbPlatform
