#pragma once
#include "potluck_paths.h"

#ifdef POTLUCK_STARTUP_TRACE
#include <cstdio>
#include <sys/stat.h>
inline void StartupTrace(const char* stage, bool reset = false) {
    mkdir("sdmc:/config", 0777);
    mkdir(POTLUCK_LOG_DIR, 0777);
    FILE* file = std::fopen(POTLUCK_OVERLAY_TRACE_PATH, reset ? "w" : "a");
    if (!file) return;
    std::fputs(stage, file);
    std::fputc('\n', file);
    std::fclose(file);
}
#else
inline void StartupTrace(const char*, bool = false) {}
#endif
