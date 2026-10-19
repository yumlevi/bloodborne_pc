// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: helper thread sizing. Counts follow the hardware threads this process may run on
// (the affinity mask, so `taskset` can emulate a Steam Deck), and speculative helpers run as
// SCHED_IDLE: they use cores the game leaves idle and never take time from its threads.

#pragma once

#include <algorithm>
#include <thread>
#ifndef _WIN32
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace BbThreads {

#ifdef _WIN32
// Windows (bbport_platform.cpp): the process affinity mask; THREAD_PRIORITY_IDLE, NORMAL.
unsigned Available();
void MakeBackground();
void MakeForeground();
#else
/// Hardware threads available to the process.
inline unsigned Available() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        return std::max(1, CPU_COUNT(&set));
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

/// The calling thread only runs on otherwise idle cores (falls back to the lowest nice level).
inline void MakeBackground() {
    sched_param param{};
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    }
}

/// Back to the normal policy, for work another thread may be waiting on (best effort: a
/// thread that fell back to nice 19 may not be allowed to lower it again).
inline void MakeForeground() {
    sched_param param{};
    sched_setscheduler(0, SCHED_OTHER, &param);
}
#endif

} // namespace BbThreads
