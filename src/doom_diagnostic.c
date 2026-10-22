/* Windows-only, opt-in diagnostic executable. Normal builds omit this file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef FORCEINLINE
#undef FORCEINLINE
#endif
#include "doom_diagnostic.h"
#include "desktop/config.h"
#include "crc32.h"
#include "sha256.h"
#include "doom_input.h"
#include "doom_renderer.h"
#include "doom_rotations.h"
#include "mod_runtime.h"

static char directory[2048], archive[2048], report_root[1536];
static DoomVideoSettings video;
static int have_video;
static int modern_controls, vertical_look;
static int rotations_active, infighting_enabled, crosshair_enabled;
static int silent;
static FILE *report;
static unsigned test_work_us;
static LARGE_INTEGER test_frequency;

void DoomDiagnosticSimulationWork(void) {
    if (!test_work_us) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    LONGLONG end = now.QuadPart + test_frequency.QuadPart * test_work_us / 1000000;
    do { QueryPerformanceCounter(&now); } while (now.QuadPart < end);
}

static void Path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s\\%s", directory, name);
}
static int MakeDirectory(const char *path) {
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static void Notice(const char *text, UINT icon) {
    if (!silent) MessageBoxA(NULL, text, "DoomSNESRecomp diagnostic build", MB_OK | icon);
}

int DoomDiagnosticBegin(void) {
    char root[1536], path[2048];
    silent = getenv("DOOM_DIAGNOSTIC_NO_UI") && atoi(getenv("DOOM_DIAGNOSTIC_NO_UI"));
    /* Hide only a console allocated for a double-click launch, never the
     * user's existing terminal. All diagnostic output goes to report files. */
    DWORD console_processes[2];
    if (!silent && GetConsoleProcessList(console_processes, 2) == 1) {
        HWND console = GetConsoleWindow();
        if (console) ShowWindow(console, SW_HIDE);
    }
    const char *override = getenv("DOOM_DIAGNOSTIC_REPORT_ROOT");
    if (override) {
        if (strlen(override) >= sizeof(root)) return 0;
        strcpy(root, override);
    } else {
        DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", root, sizeof(root));
        if (!length || length >= sizeof(root) - 64) return 0;
        strcat(root, "\\DoomSNESRecomp");
        if (!MakeDirectory(root)) return 0;
        strcat(root, "\\Diagnostics");
    }
    if (!MakeDirectory(root)) return 0;
    strcpy(report_root, root);
    SYSTEMTIME now; GetLocalTime(&now);
    snprintf(directory, sizeof(directory), "%s\\report-%04u%02u%02u-%02u%02u%02u-%lu",
        root, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        (unsigned long)GetCurrentProcessId());
    if (!MakeDirectory(directory)) return 0;
    snprintf(archive, sizeof(archive), "%s.zip", directory);
    Path(path, sizeof(path), "report.txt");
    report = fopen(path, "w");
    if (!report) return 0;
    const char *work = getenv("DOOM_DIAGNOSTIC_TEST_WORK_US");
    if (work) {
        char *end; unsigned long value = strtoul(work, &end, 10);
        if (end != work && !*end && value <= 12000 && QueryPerformanceFrequency(&test_frequency))
            test_work_us = (unsigned)value;
    }
    if (test_work_us) fprintf(report, "Developer injected CPU work: %u us/console field (synthetic workload)\n", test_work_us);
    fprintf(report, "DoomSNESRecomp diagnostic build\nStarted: %04u-%02u-%02u %02u:%02u:%02u (local)\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    char executable[2048];
    DWORD exe_length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (exe_length && exe_length < sizeof(executable)) {
        FILE *file = fopen(executable, "rb");
        if (file) {
            if (!fseek(file, 0, SEEK_END)) {
                long size = ftell(file);
                if (size > 0 && size <= 64 * 1024 * 1024 && !fseek(file, 0, SEEK_SET)) {
                    uint8_t *data = malloc((size_t)size);
                    if (data) {
                        if (fread(data, 1, (size_t)size, file) == (size_t)size) {
                            uint8_t hash[32]; sha256_compute(data, (size_t)size, hash);
                            fputs("Executable SHA256: ", report);
                            for (unsigned i = 0; i < sizeof(hash); i++) fprintf(report, "%02x", hash[i]);
                            fputc('\n', report);
                        }
                        free(data);
                    }
                }
            }
            fclose(file);
        }
    }
    char os[256] = {0}; DWORD os_size = sizeof(os);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "CurrentBuildNumber", RRF_RT_REG_SZ, NULL, os, &os_size) == ERROR_SUCCESS)
        fprintf(report, "Windows build: %s\n", os);
    SYSTEM_INFO info; GetNativeSystemInfo(&info);
    MEMORYSTATUSEX memory = {0}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        fprintf(report, "RAM: %.2f GiB physical, %.2f GiB available\n",
            memory.ullTotalPhys / 1073741824.0, memory.ullAvailPhys / 1073741824.0);
    fprintf(report, "Logical processors: %lu\n", (unsigned long)info.dwNumberOfProcessors);
    char cpu[256] = {0}; DWORD bytes = sizeof(cpu);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString", RRF_RT_REG_SZ, NULL, cpu, &bytes) == ERROR_SUCCESS)
        fprintf(report, "CPU: %s\n", cpu);
    DISPLAY_DEVICEA device;
    for (DWORD i = 0; ; i++) {
        memset(&device, 0, sizeof(device)); device.cb = sizeof(device);
        if (!EnumDisplayDevicesA(NULL, i, &device, 0)) break;
        if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            fprintf(report, "Display GPU: %s\n", device.DeviceString);
            const char *prefix = "\\Registry\\Machine\\";
            if (!strncmp(device.DeviceKey, prefix, strlen(prefix))) {
                char driver[256] = {0}; DWORD driver_size = sizeof(driver);
                if (RegGetValueA(HKEY_LOCAL_MACHINE, device.DeviceKey + strlen(prefix),
                    "DriverVersion", RRF_RT_REG_SZ, NULL, driver, &driver_size) == ERROR_SUCCESS)
                    fprintf(report, "Display driver version: %s\n", driver);
            }
        }
    }
    fflush(report);
    Path(path, sizeof(path), "stdout.log");
    if (!freopen(path, "w", stdout)) return 0;
    Path(path, sizeof(path), "stderr.log");
    if (!freopen(path, "w", stderr)) return 0;
    Path(path, sizeof(path), "timing.csv");
    if (_putenv_s("SNESRECOMP_FRAME_TIMING", path) ||
        _putenv_s("SNESRECOMP_HOST_PROFILE", "1") ||
        _putenv_s("SNESRECOMP_HOST_PROFILE_START_FRAME", "120") ||
        _putenv_s("DOOM_RENDER_STATS", "1")) return 0;
    Notice("Play normally with the settings that cause lag for at least one minute.\n\n"
        "Close the game when finished. A report ZIP will be created automatically and its folder will open.\n\n"
        "Please send that ZIP back. It includes performance timings and hardware details, without your ROM or saves.", MB_ICONINFORMATION);
    return 1;
}

void DoomDiagnosticVideo(const DoomVideoSettings *settings) {
    video = *settings; have_video = 1;
    DoomInputStats stats; DoomInputGetStats(&stats);
    modern_controls = stats.enabled; vertical_look = DoomInputLookEnabled();
    rotations_active = DoomRotationsEnabled();
    infighting_enabled = snes_mod_runtime_feature_enabled_c("doom.infighting","monster-infighting");
    crosshair_enabled = snes_mod_runtime_feature_enabled_c("doom.crosshair","autoaim-crosshair");
}

static void SummarizeTimings(void) {
    char path[2048], line[2048]; Path(path, sizeof(path), "timing.csv");
    FILE *file = fopen(path, "r");
    if (!file) { fputs("No timing data collected. Play for at least one minute and close normally.\n", report); return; }
    unsigned first_frame = 0, last_frame = 0, samples = 0;
    unsigned raw_first_frame = 0, raw_last_frame = 0, raw_samples = 0;
    double raw_first_time = 0, raw_last_time = 0;
    double first_time = 0, last_time = 0, guest_sum = 0, compose_sum = 0, present_sum = 0;
    double raster_sum = 0, first_guest = 0, first_raster = 0;
    double first_audio[3] = {0}, last_audio[3] = {0};
    /* Ignore boot/menus by default: retain all raw rows, summarize the late
     * window only. Short sessions receive a clear insufficient-data result. */
    fgets(line, sizeof(line), file);
    while (fgets(line, sizeof(line), file)) {
        double fields[16]; unsigned count = 0; char *cursor = line;
        while (count < 16) {
            char *end; fields[count] = strtod(cursor, &end);
            if (end == cursor) break;
            count++;
            if (*end != ',') break;
            cursor = end + 1;
        }
        if (count != 16) continue;
        if (!raw_samples) { raw_first_frame = (unsigned)fields[0]; raw_first_time = fields[1]; }
        raw_last_frame = (unsigned)fields[0]; raw_last_time = fields[1]; raw_samples++;
        if (fields[0] < 1400) continue;
        if (!samples) {
            first_frame = (unsigned)fields[0]; first_time = fields[1];
            first_guest = fields[3]; first_raster = fields[4];
            for (unsigned i = 0; i < 3; i++) first_audio[i] = fields[12+i];
        }
        last_frame = (unsigned)fields[0]; last_time = fields[1];
        guest_sum += fields[3]; raster_sum += fields[4]; compose_sum += fields[6]; present_sum += fields[7];
        for (unsigned i = 0; i < 3; i++) last_audio[i] = fields[12+i];
        samples++;
    }
    fclose(file);
    if (raw_samples) fprintf(report,
        "\nRaw timing coverage: fields %u..%u, %.3f seconds, %u pictures\n"
        "Long sessions retain the latest pictures in bounded memory; this window may omit the start.\n",
        raw_first_frame, raw_last_frame, raw_last_time - raw_first_time, raw_samples);
    if (samples < 2 || last_time <= first_time) {
        fputs("Session too short for the late-window summary; raw timings remain in timing.csv.\n", report);
        return;
    }
    fprintf(report, "\nLate-window summary (console fields >= 1400; menus/pauses may still be included)\n"
        "Samples: %u\nGuest fields/second: %.3f\nPresented frames/second: %.3f\n"
        "Mean guest work: %.3f ms/presentation\nMean composition: %.3f ms/presentation\n"
        "Mean driver presentation: %.3f ms/presentation\n"
        "Audio underflows added: %.0f\nAudio missing frames added: %.0f\nAudible dropped samples added: %.0f\n",
        samples, (last_frame-first_frame)/(last_time-first_time), (samples-1)/(last_time-first_time),
        guest_sum/samples, compose_sum/samples, present_sum/samples,
        last_audio[0]-first_audio[0], last_audio[1]-first_audio[1], last_audio[2]-first_audio[2]);
    if (last_frame > first_frame) fprintf(report,
        "Normalized simulation work: %.3f ms/console field\n"
        "Normalized raster capture: %.3f ms/console field\n",
        (guest_sum - first_guest)/(last_frame - first_frame),
        (raster_sum - first_raster)/(last_frame - first_frame));
    fprintf(report, "Cumulative audio at last summarized picture: %.0f underflows, %.0f missing frames\n",
        last_audio[0], last_audio[1]);
}

/* Standard ZIP with stored entries: no PowerShell, external compressor or
 * additional runtime dependency. Files are streamed with bounded memory. */
static void U16(FILE *f, uint16_t n) { fputc(n & 255, f); fputc(n >> 8, f); }
static void U32(FILE *f, uint32_t n) { U16(f, n & 65535); U16(f, n >> 16); }
typedef struct ZipEntry { const char *name; uint32_t crc, size, offset; } ZipEntry;
static int WriteZip(void) {
    const char *names[] = {"report.txt", "timing.csv", "stdout.log", "stderr.log"};
    ZipEntry entries[4]; unsigned count = 0;
    FILE *zip = fopen(archive, "wb");
    if (!zip) return 0;
    unsigned char buffer[32768]; char path[2048];
    for (unsigned i = 0; i < 4; i++) {
        Path(path, sizeof(path), names[i]); FILE *file = fopen(path, "rb");
        if (!file) continue;
        ZipEntry entry = {names[i], 0, 0, (uint32_t)ftell(zip)};
        uint32_t crc = CRC32_INIT; size_t length;
        while ((length = fread(buffer, 1, sizeof(buffer), file))) {
            if (length > UINT32_MAX - entry.size) { fclose(file); fclose(zip); return 0; }
            entry.size += (uint32_t)length; crc = crc32_update(crc, buffer, length);
        }
        if (ferror(file) || fseek(file, 0, SEEK_SET)) { fclose(file); fclose(zip); return 0; }
        entry.crc = crc32_final(crc);
        U32(zip, 0x04034b50); U16(zip, 20); U16(zip, 0); U16(zip, 0);
        U16(zip, 0); U16(zip, 0x0021); U32(zip, entry.crc);
        U32(zip, entry.size); U32(zip, entry.size); U16(zip, (uint16_t)strlen(entry.name)); U16(zip, 0);
        fwrite(entry.name, 1, strlen(entry.name), zip);
        while ((length = fread(buffer, 1, sizeof(buffer), file))) fwrite(buffer, 1, length, zip);
        int failed = ferror(file); fclose(file);
        if (failed || ferror(zip)) { fclose(zip); return 0; }
        entries[count++] = entry;
    }
    uint32_t start = (uint32_t)ftell(zip);
    for (unsigned i = 0; i < count; i++) {
        ZipEntry *entry = &entries[i];
        U32(zip, 0x02014b50); U16(zip, 20); U16(zip, 20); U16(zip, 0); U16(zip, 0);
        U16(zip, 0); U16(zip, 0x0021); U32(zip, entry->crc); U32(zip, entry->size); U32(zip, entry->size);
        U16(zip, (uint16_t)strlen(entry->name)); U16(zip, 0); U16(zip, 0); U16(zip, 0); U16(zip, 0);
        U32(zip, 0); U32(zip, entry->offset); fwrite(entry->name, 1, strlen(entry->name), zip);
    }
    uint32_t size = (uint32_t)ftell(zip) - start;
    U32(zip, 0x06054b50); U16(zip, 0); U16(zip, 0); U16(zip, count); U16(zip, count);
    U32(zip, size); U32(zip, start); U16(zip, 0);
    int failed = ferror(zip); if (fclose(zip)) failed = 1;
    return !failed;
}

void DoomDiagnosticFinish(int exit_code) {
    fprintf(report, "Exit code: %d\nRenderer selection: %s\nRequested VSync: %u\n"
        "Frame delay disabled: %u\nShader enabled: %u\nAudio enabled: %u\nAudio frequency: %u\n",
        exit_code, g_config.renderer, g_config.vsync, g_config.disable_frame_delay,
        g_config.shader && g_config.shader[0], g_config.enable_audio, g_config.audio_freq);
    if (have_video) fprintf(report, "Interpolation enabled: %u\nRequested FPS: %u (0 = Auto)\n"
        "Widescreen: %u\nAspect: %s\nResolution enabled: %u\nResolution scale: %u\n",
        video.fps_enabled && video.interpolate, video.fps, video.widescreen,
        DoomAspectName(video.aspect), video.resolution_enabled, video.resolution_scale);
    else fputs("No gameplay settings sampled; game may have been closed before Play.\n", report);
    fprintf(report, "Modern controls: %d\nVertical look: %d\n", modern_controls, vertical_look);
    fprintf(report, "Enemy rotations active: %d\nMonster infighting enabled: %d\nAuto-aim crosshair enabled: %d\n",
        rotations_active, infighting_enabled, crosshair_enabled);
    SummarizeTimings();
    DoomRendererStats renderer; DoomRendererGetStats(&renderer);
    if (renderer.async_submitted || renderer.async_fallbacks) fprintf(report,
        "\nAsync camera pipeline: submitted=%llu accepted=%llu stale=%llu cancelled=%llu reused=%llu\n"
        "Worker equivalence checks: verified=%llu mismatches=%llu fallbacks=%llu\n"
        "Presented FPS includes reused pictures; worker replay overlaps simulation and is excluded from main composition time.\n",
        (unsigned long long)renderer.async_submitted, (unsigned long long)renderer.async_accepted,
        (unsigned long long)renderer.async_stale, (unsigned long long)renderer.async_cancelled,
        (unsigned long long)renderer.async_reused, (unsigned long long)renderer.async_verified,
        (unsigned long long)renderer.async_mismatches,(unsigned long long)renderer.async_fallbacks);
    if (renderer.async_submitted || renderer.async_fallbacks) fprintf(report,
        "Worker recovery: temporary_fallbacks=%llu retries=%llu\n",
        (unsigned long long)renderer.async_temporary_fallbacks,
        (unsigned long long)renderer.async_retries);
    if (renderer.profile_jobs) fprintf(report,
        "\nRenderer phase breakdown (wall time, cumulative; camera workers may overlap)\n"
        "Camera jobs: %llu\nMean camera job: %.3f ms/job\n"
        "Mean GSU geometry replay: %.3f ms/job\nMean GSU draw replay: %.3f ms/job\n"
        "Mean geometry/UV/sprite capture: %.3f ms/job\nMean native picture decode: %.3f ms/job\n",
        (unsigned long long)renderer.profile_jobs, renderer.profile_job_ms/renderer.profile_jobs,
        renderer.profile_build_ms/renderer.profile_jobs, renderer.profile_draw_ms/renderer.profile_jobs,
        renderer.profile_metadata_ms/renderer.profile_jobs, renderer.profile_decode_ms/renderer.profile_jobs);
    if (renderer.profile_raster_calls) fprintf(report,
        "Host raster calls: %llu\nMean host raster: %.3f ms/call\n",
        (unsigned long long)renderer.profile_raster_calls,
        renderer.profile_raster_ms/renderer.profile_raster_calls);
    fputs("Boot, menus and pauses may be included; the retained timing range is listed above.\n"
        "High-frame-rate mode uses host pacing and disables driver VSync.\n"
        "Raw phase timings and audio counters are in timing.csv; actual renderer details are in stdout.log.\n", report);
    fclose(report); report = NULL;
    fflush(stdout); fflush(stderr); fclose(stdout); fclose(stderr);
    if (WriteZip()) {
        char message[2300]; snprintf(message, sizeof(message), "Your report is ready:\n\n%s\n\n"
            "Send this ZIP back for diagnosis. Its folder will now open.", archive);
        Notice(message, MB_ICONINFORMATION);
        if (!silent) ShellExecuteA(NULL, "open", report_root, NULL, NULL, SW_SHOWNORMAL);
    } else {
        char message[2300]; snprintf(message, sizeof(message), "Could not create the report ZIP.\n"
            "The report files are still here:\n\n%s", directory);
        Notice(message, MB_ICONERROR);
    }
}
