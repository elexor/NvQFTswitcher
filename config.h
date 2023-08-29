#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <vector>
#include <string>
#include "nvapi.h"

struct savedMode
{
    NvU32 dispId = 0;
    double refreshrate = 0;
};

struct baseMode 
{
    NvU32 dispId = 0;
    NvU32 maxpclk = 0;
    int refreshrate = 0;
    int minvt = 0;
};

struct displayInfo
{
    baseMode basemode;
    std::wstring displayString;
    NvU32 dispId = 0;
    bool initDisplaymode = true;
    int minVtotal = 0;
    int maxVtotal = 55000;
    NvU32 maxpclk = 0;
    int reducemaxpclk = 0;
    int oldpclk = 0;
    int maxRefreshrate = 0;
    int selected_mode_idx = -1;
    double desired_refreshrate;
    bool round_pixel_clock = false;
    bool spoof_refreshrate = false;
    bool is_xg2431 = false;
};

std::vector<savedMode> loadModes();
void saveMode(const savedMode& mode);
void deleteMode(const savedMode& mode);

void save_baseMode(baseMode mode);
baseMode load_baseMode(int dispId);

#endif // CONFIG_H
