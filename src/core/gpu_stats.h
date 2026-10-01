// NVML readings of the RTX 4080: power, clocks, temperature. Sampled on demand.
#pragma once

struct GpuStats {
    float watts = 0, limitWatts = 0;
    unsigned int smClockMHz = 0, memClockMHz = 0, tempC = 0, utilPct = 0;
};

bool gpuStatsInit();
GpuStats gpuStatsRead();
void gpuStatsShutdown();
