#pragma once
#ifndef XG2431_H
#define XG2431_H

#include "config.h"
#include <LowLevelMonitorConfigurationAPI.h>



byte vcp[4] = {
    0xE8, // purexp mode /0 off /1 Light /2 Normal /3 Extreme /4 Ultra /5 Custom
    0xEF, // dutycyle 1-40
    0xEA, // phase 0-99
    0xE3  // odgain 0-100
};

struct settingsResult
{
    int phase = 0;
    int odgain = 0;
};

struct input
{
    int maxhz_phase = 10;
    int minhz_phase = 90;
    int maxhz_odgain = 0;
    int minhz_odgain = 0;
};

#endif // XG2431_H
