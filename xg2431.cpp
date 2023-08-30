#include "xg2431.h"
#include "config.h"
#include <Eigen/Dense>

using namespace Eigen;


bool ddcCommit(byte address, DWORD value, PHYSICAL_MONITOR monitor)
{
    DWORD cvalue = 0;
    SetVCPFeature(monitor.hPhysicalMonitor, address, value);
    bool read = GetVCPFeatureAndVCPFeatureReply(monitor.hPhysicalMonitor, address, NULL, &cvalue, NULL);

    if (read && cvalue == value) {
        return 1;
    }
    return 0;
}

// use linear regression to predict phase and odgain

bool predictPhaseOdGain(std::vector<DataPoint>& dataPoints, DDCsettings& settings) {
    int num_samples = dataPoints.size();

    Eigen::MatrixXd X(num_samples, 2);  // refreshrate, pulsewidth
    Eigen::MatrixXd Y(num_samples, 2);  // phase, odgain

    for (int i = 0; i < num_samples; ++i) {
        X(i, 0) = dataPoints[i].refreshrate;
        X(i, 1) = dataPoints[i].pulsewidth;
        Y(i, 0) = dataPoints[i].phase;
        Y(i, 1) = dataPoints[i].odgain;
    }

    Eigen::MatrixXd X_bias(num_samples, 3); 
    X_bias << Eigen::MatrixXd::Ones(num_samples, 1), X;
    Eigen::MatrixXd theta = (X_bias.transpose() * X_bias).inverse() * X_bias.transpose() * Y;

    Eigen::VectorXd new_point(3);
    new_point << 1, settings.refreshrate, settings.pulsewidth;

    // predict phase and odgain for the new data point
    Eigen::VectorXd prediction = new_point.transpose() * theta;
    
    // convert phase and odgain to xg2431 valid values
    if (prediction(0) > 99) {
        settings.phase = static_cast<DWORD>(round(fmod(prediction(0), 100)));
    }
    else {
        settings.phase = static_cast<DWORD>(round(prediction(0))); 
    }   
    settings.odgain = static_cast<DWORD>(round(prediction(1)));

    // check values are valid for xg2431 ddc
    if (settings.odgain > 100 || settings.odgain < 0) {
        MessageBox(NULL, L"Predicted odgain isn't in a valid range", L"XG2431 Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (settings.phase > 99 || settings.odgain < 0) {
        MessageBox(NULL, L"Predicted phase isn't in a valid range", L"XG2431 Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    return 1;
}


bool tuneXG2431(PHYSICAL_MONITOR monitor, double refreshrate)
{
    DWORD value = 0;
    DWORD pulsewidth = 0;
    DDCsettings settings{};
    VCPaddress vcp{};

    //if purexp preset is enabled switch to custom with the same pulsewidth
    if (!GetVCPFeatureAndVCPFeatureReply(monitor.hPhysicalMonitor, vcp.purexp, NULL, &value, NULL)) {
        MessageBox(NULL, L"DDC failed to read purexp mode", L"XG2431 Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    switch (value)
    {
        case 1: // light
        { 
            pulsewidth = 40;
            break;
        }
        case 2: // normal
        {
            pulsewidth = 30;
            break;
        }
        case 3: // extreme
        {
            pulsewidth = 20;
            break;
        }
        case 4: // ultra
        {
            pulsewidth = 10;
            break;
        }
    }
    if (pulsewidth > 0)
    {
        if (!ddcCommit(vcp.purexp, 5, monitor)) { // enable purexp custom
            return 0;
            MessageBox(NULL, L"DDC failed to write purexp mode to custom", L"XG2431 Error", MB_OK | MB_ICONERROR);
        }
        Sleep(500); // need a delay after switching to custom for custom settings to apply correctly?
        if (!ddcCommit(vcp.pulsewidth, pulsewidth, monitor)) {
            return 0;
            MessageBox(NULL, L"DDC failed to write pulsewidth", L"XG2431 Error", MB_OK | MB_ICONERROR);
        }
    }

    settings.refreshrate = refreshrate;
    if (!GetVCPFeatureAndVCPFeatureReply(monitor.hPhysicalMonitor, vcp.pulsewidth, NULL, &value, NULL)) {
        MessageBox(NULL, L"DDC failed to read pulsewidth", L"XG2431 Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    settings.pulsewidth = value;

    if (!predictPhaseOdGain(tuningData, settings)) {
        return 0;
    }

    if (!ddcCommit(vcp.phase, settings.phase, monitor)) {
        return 0;
        MessageBox(NULL, L"DDC failed to write phase", L"XG2431 Error", MB_OK | MB_ICONERROR);
    }
    if (!ddcCommit(vcp.odgain, settings.odgain, monitor)) {
        MessageBox(NULL, L"DDC failed to write odgain", L"XG2431 Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    return 1;
}


