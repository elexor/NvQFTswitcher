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

std::vector<savedMode> loadModes();
void saveMode(const savedMode& mode);
void deleteMode(const savedMode& mode);

void save_baseMode(baseMode mode);
baseMode load_baseMode(int dispId);

#endif // CONFIG_H
