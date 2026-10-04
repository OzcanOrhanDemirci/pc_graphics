#include "core/gpu_stats.h"

#include <nvml.h>

static nvmlDevice_t g_gpu = nullptr;

bool gpuStatsInit() {
    if (nvmlInit_v2() != NVML_SUCCESS) return false;
    return nvmlDeviceGetHandleByIndex_v2(0, &g_gpu) == NVML_SUCCESS;
}

GpuStats gpuStatsRead() {
    GpuStats s;
    if (!g_gpu) return s;
    unsigned int mw = 0, limit = 0;
    if (nvmlDeviceGetPowerUsage(g_gpu, &mw) == NVML_SUCCESS) s.watts = mw / 1000.0f;
    if (nvmlDeviceGetEnforcedPowerLimit(g_gpu, &limit) == NVML_SUCCESS) s.limitWatts = limit / 1000.0f;
    nvmlDeviceGetClockInfo(g_gpu, NVML_CLOCK_SM, &s.smClockMHz);
    nvmlDeviceGetClockInfo(g_gpu, NVML_CLOCK_MEM, &s.memClockMHz);
    nvmlDeviceGetTemperature(g_gpu, NVML_TEMPERATURE_GPU, &s.tempC);
    nvmlUtilization_t u{};
    if (nvmlDeviceGetUtilizationRates(g_gpu, &u) == NVML_SUCCESS) s.utilPct = u.gpu;
    nvmlMemory_t m{};
    if (nvmlDeviceGetMemoryInfo(g_gpu, &m) == NVML_SUCCESS) {
        s.memUsedGB = float(double(m.used) / 1073741824.0);
        s.memTotalGB = float(double(m.total) / 1073741824.0);
    }
    return s;
}

void gpuStatsShutdown() {
    if (g_gpu) nvmlShutdown();
    g_gpu = nullptr;
}
