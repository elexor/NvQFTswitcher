#include "xg2431.h"
#include "config.h"

DWORD ddcCommit(byte address, DWORD value, PHYSICAL_MONITOR monitor)
{
    DWORD cvalue;
    SetVCPFeature(&monitor, address, value);
    bool read = GetVCPFeatureAndVCPFeatureReply(&monitor, address, NULL, &cvalue, NULL);

    if (read && cvalue == value) {
        return value;
    }
    return -1;
}

