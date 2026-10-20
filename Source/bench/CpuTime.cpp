// CPU time of the calling thread and of the whole process (benchmark build only).
// Kept apart so <windows.h> stays out of the files that use JUCE's names.

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>

namespace
{
double seconds (const FILETIME& kernel, const FILETIME& user)
{
    const auto value = [] (const FILETIME& t) { return (double) (((unsigned long long) t.dwHighDateTime << 32) | t.dwLowDateTime); };
    return (value (kernel) + value (user)) * 1.0e-7;
}
} // namespace

double threadCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user);
    return seconds (kernel, user);
}

double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user);
    return seconds (kernel, user);
}
#else
 #include <ctime>
double threadCpuSeconds()  { timespec t; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1.0e-9; }
double processCpuSeconds() { timespec t; clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1.0e-9; }
#endif
