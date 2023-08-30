#pragma once
#ifndef XG2431_H
#define XG2431_H

#include "config.h"
#include <LowLevelMonitorConfigurationAPI.h>

struct VCPaddress {
    byte purexp = 0xE8;
    byte pulsewidth = 0xEF;
    byte phase = 0xEA;
    byte odgain = 0xE3;
};

struct DDCsettings {
    double refreshrate;
    DWORD purexp_mode;
    DWORD pulsewidth;
    DWORD phase;
    DWORD odgain;
};

struct DataPoint {
    double refreshrate;
    double pulsewidth;
    double phase;
    double odgain;
};

// tuning data provided by Discorz

inline std::vector<DataPoint> tuningData = {
    {60, 40, 62, 4},
    {60, 30, 72, 4},
    {60, 20, 81, 4},
    {60, 10, 91, 4},
    {90, 40, 70, 8},
    {90, 30, 78, 8},
    {90, 20, 85, 8},
    {90, 10, 92, 8},
    {100, 40, 73, 10},
    {100, 30, 81, 10},
    {100, 20, 87, 10},
    {100, 10, 94, 10},
    {120, 40, 79, 13},
    {120, 30, 86, 13},
    {120, 20, 92, 13},
    {120, 10, 99, 13},
    {144, 40, 86, 18},
    {144, 30, 92, 18},
    {144, 20, 97, 18},
    {144, 10, 102, 18},
    {165, 40, 92, 24},
    {165, 30, 96, 24},
    {165, 20, 102, 24},
    {165, 10, 108, 24},
    {180, 40, 98, 26},
    {180, 30, 100, 26},
    {180, 20, 107, 26},
    {180, 10, 111, 26},
    {200, 40, 102, 30},
    {200, 30, 107, 30},
    {200, 20, 112, 30},
    {200, 10, 116, 30},
    {240, 40, 115, 38},
    {240, 30, 120, 38},
    {240, 20, 123, 38},
    {240, 10, 126, 38}
};

bool tuneXG2431(PHYSICAL_MONITOR monitor, double refreshrate);
bool predictPhaseOdGain(std::vector<DataPoint>& dataPoints, DDCsettings& settings);
bool ddcCommit(byte address, DWORD value, PHYSICAL_MONITOR monitor);

#endif // XG2431_H
