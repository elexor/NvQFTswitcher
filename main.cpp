#include <windows.h>
#include <windowsx.h>
#include <string.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <regex>
#include <PhysicalMonitorEnumerationAPI.h>
#include <LowLevelMonitorConfigurationAPI.h>

#include "nvapi.h"
#include "config.h"
#include "resource.h"

#pragma comment(lib, "nvapi64.lib")

#define WM_SYSICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1
#define select_display_combo 1001
#define change_refreshrate_btn 1002
#define desired_refreshrate_textbox 1003
#define save_refreshrate_btn 1004
#define saved_modes_combo 1005
#define delete_refreshrate_btn 1006
#define base_mode_text 1007
#define popup_exit 10000


HMENU popmenu;

int disp_idx;
bool quit;
savedMode mode;
HWND hTimingText;
NvAPI_Status ret = NVAPI_OK;

std::vector<savedMode> modes, currentModes;
std::vector<PHYSICAL_MONITOR> physicalMonitors {};
displayInfo dispInfo[NVAPI_MAX_DISPLAYS] {};


NvAPI_Status ApplyCustomDisplay();
void UpdateTimingText();

void Revert()
{
    ret = NvAPI_DISP_RevertCustomDisplayTrial(&dispInfo[disp_idx].dispId, 1);
}

BOOL CALLBACK monitorEnumProcCallback(HMONITOR hMonitor, HDC hDeviceContext, LPRECT rect, LPARAM data)
{
    DWORD numberOfPhysicalMonitors;
    BOOL success = GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &numberOfPhysicalMonitors);
    if (success) {
        auto originalSize = physicalMonitors.size();
        physicalMonitors.resize(physicalMonitors.size() + numberOfPhysicalMonitors);
        success = GetPhysicalMonitorsFromHMONITOR(hMonitor, numberOfPhysicalMonitors, physicalMonitors.data() + originalSize);
    }
    return true;
}

void initDisp()
{
    DISPLAY_DEVICE dd{};
    dd.cb = sizeof(dd);

    int deviceIndex = 0;
    while (EnumDisplayDevices(0, deviceIndex, &dd, 0))
    {
        std::wstring deviceName = dd.DeviceName;
        int monitorIndex = 0;
        while (EnumDisplayDevices(deviceName.c_str(), monitorIndex, &dd, 0))
        {
            NvU32 displayId = 0;
            std::wstring deviceNameW = dd.DeviceName;
            std::wstring displayNameW = deviceNameW.substr(0, deviceNameW.find(L"\\Monitor"));
            std::string displayName(displayNameW.begin(), displayNameW.end());

            std::wstring ws(dd.DeviceID);
            const std::wstring start_delim = L"MONITOR\\";
            const std::wstring stop_delim = L"\\";
            std::wstring displayString = ws.substr(start_delim.length(), ws.find(stop_delim, start_delim.length()) - start_delim.length());

            ret = NvAPI_DISP_GetDisplayIdByDisplayName(displayName.c_str(), &displayId);
            if (ret != NVAPI_OK) {
                ++monitorIndex;
                continue; // when GetDisplay fails the monitor is probably disabled or a clone of another display and can't be used
            }
            dispInfo[deviceIndex].displayString = displayString;
            dispInfo[deviceIndex].dispId = displayId;

            if (std::regex_match(displayString, std::wregex(L"VSC3B3[0-F]"))) // check for active xg2431
            {
                dispInfo[deviceIndex].is_xg2431 = true;
            }

            std::wstring wideDeviceName(displayName.begin(), displayName.end());
            LPCWSTR szDeviceName = wideDeviceName.c_str();

            ChangeDisplaySettingsEx(szDeviceName, NULL, NULL, CDS_ENABLE_UNSAFE_MODES, NULL); // some modes won't work without this. it's the same as "Enable resolutions not exposed by the display" in nvcp
            ++monitorIndex;
        }
        ++deviceIndex;
    }
    EnumDisplayMonitors(NULL, NULL, &monitorEnumProcCallback, 0);
}


void ResizeComboBox(HWND hComboBox, int numElements) {
    RECT rect;
    GetWindowRect(hComboBox, &rect);
    ScreenToClient(GetParent(hComboBox), (LPPOINT)&rect.left);
    ScreenToClient(GetParent(hComboBox), (LPPOINT)&rect.right);
    int comboWidth = rect.right - rect.left;
    int adjustedHeight = comboWidth * numElements;
    MoveWindow(hComboBox, rect.left, rect.top, comboWidth, adjustedHeight, TRUE);
}

bool CompareModes(const savedMode& a, const savedMode& b)
{
    return a.refreshrate < b.refreshrate;
}


void ReloadModes(HWND hWnd) {
    HWND hwndCombo = GetDlgItem(hWnd, saved_modes_combo);
    SendMessage(hwndCombo, CB_RESETCONTENT, 0, 0);
    modes.clear();
    currentModes.clear();
    modes = loadModes();

    if (IsMenu(popmenu)) {
        DestroyMenu(popmenu);
        popmenu = NULL;
    }
    popmenu = CreatePopupMenu();

    int modesCount = 0;

    std::sort(modes.begin(), modes.end(), CompareModes);

    for (size_t i = 0; i < modes.size(); i++)
    {
        if (modes[i].dispId == dispInfo[disp_idx].dispId && modes[i].refreshrate > 0) {

            auto rr = std::format("{:.3f}hz", modes[i].refreshrate);

            // populate combobox with modes
            SendMessageA(hwndCombo, CB_ADDSTRING, 0, (LPARAM)rr.c_str());

            // populate popup menu with modes

            if (dispInfo[disp_idx].selected_mode_idx == modesCount)
            {
                AppendMenuA(popmenu, MF_STRING | MF_CHECKED, modesCount + 1, rr.c_str());
            }
            else
            {
                AppendMenuA(popmenu, MF_STRING, modesCount + 1, rr.c_str());
            }
            

            currentModes.push_back({ modes[i].dispId, modes[i].refreshrate });
            modesCount++;
        }
    }
    AppendMenu(popmenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(popmenu, MF_STRING, popup_exit, "Exit");
    int index = 0;
    for (size_t i = 0; i < currentModes.size(); i++)
    {
        if (currentModes[i].refreshrate == dispInfo[disp_idx].desired_refreshrate)
        {
            index = i;
            break;
        }
    }
    UpdateTimingText();
    ResizeComboBox(hwndCombo, currentModes.size());
    SendMessage(hwndCombo, CB_SETCURSEL, index, 0);
    CheckMenuItem(popmenu, index + 1, MF_BYCOMMAND | MF_CHECKED);
}

void OnComboBoxSelectionChanged(HWND hWnd)
{
    HWND hDisplayComboBox = GetDlgItem(hWnd, select_display_combo);
    int index = SendMessage(hDisplayComboBox, CB_GETCURSEL, 0, 0);
    TCHAR text[256]{};
    SendMessage(hDisplayComboBox, CB_GETLBTEXT, index, (LPARAM)text);
    disp_idx = ComboBox_GetCurSel(hDisplayComboBox);
    ReloadModes(hWnd);
}

void OnSavedModesSelected(HWND hWnd)
{
    HWND hSavedModesComboBox = GetDlgItem(hWnd, saved_modes_combo);
    int index = SendMessage(hSavedModesComboBox, CB_GETCURSEL, 0, 0);
    dispInfo[disp_idx].selected_mode_idx = index;
    ApplyCustomDisplay();
    ReloadModes(hWnd);
}

void ChangeRefreshrate(HWND hWnd)
{
    HWND hwndTextbox = GetDlgItem(hWnd, desired_refreshrate_textbox);
    int textLength = GetWindowTextLength(hwndTextbox);
    TCHAR* buffer = new TCHAR[textLength + 1];
    GetWindowText(hwndTextbox, buffer, textLength + 1);
    dispInfo[disp_idx].desired_refreshrate = wcstof(buffer, NULL);
    dispInfo[disp_idx].selected_mode_idx = -1;
    delete[] buffer;
    ApplyCustomDisplay();
}

void SaveRefreshrate(HWND hWnd)
{
    HWND hwndTextbox = GetDlgItem(hWnd, desired_refreshrate_textbox);
    int textLength = GetWindowTextLength(hwndTextbox);
    TCHAR* buffer = new TCHAR[textLength + 1];
    GetWindowText(hwndTextbox, buffer, textLength + 1);
    dispInfo[disp_idx].desired_refreshrate = wcstof(buffer, NULL);
    delete[] buffer;
    mode = { dispInfo[disp_idx].dispId, dispInfo[disp_idx].desired_refreshrate };
    saveMode(mode);
    ReloadModes(hWnd);
}

void DeleteRefreshrate(HWND hWnd)
{
    savedMode selectedMode = { dispInfo[disp_idx].dispId, dispInfo[disp_idx].desired_refreshrate };
    deleteMode(selectedMode);
    ReloadModes(hWnd);
}

void SetBaseMode()
{
    NV_CUSTOM_DISPLAY* display = new NV_CUSTOM_DISPLAY{};

    NV_TIMING_FLAG flag = { 0 };
    NV_TIMING_INPUT timing = { 0 };

    timing.version = NV_TIMING_INPUT_VER;
    timing.flag = flag;
    timing.type = NV_TIMING_OVERRIDE_CURRENT;

    bool vtlimited = false;

    ret = NvAPI_DISP_GetTiming(dispInfo[disp_idx].dispId, &timing, &display[0].timing); // get timings of the current mode to be used as the base mode
    if (ret != NVAPI_OK)
    {
        MessageBox(NULL, L"NvAPI_DISP_GetTiming() failed = ", L"Error", MB_OK | MB_ICONERROR);
        delete display;
        return;
    }
    save_baseMode({ dispInfo[disp_idx].dispId, display[0].timing.pclk, display[0].timing.etc.rr, display[0].timing.VTotal });
    delete display;
}

void UpdateTimingText()
{
    // get current mode timings and generate informational string

    std::string modeString{};
    NV_CUSTOM_DISPLAY* display = new NV_CUSTOM_DISPLAY{};
    NV_TIMING_INPUT timing = { 0 };
    timing.version = NV_TIMING_INPUT_VER;

    ret = NvAPI_DISP_GetTiming(dispInfo[disp_idx].dispId, &timing, &display[0].timing);
    if (ret != NVAPI_OK)
    {
        MessageBox(NULL, L"NvAPI_DISP_GetTiming() failed = ", L"Error", MB_OK | MB_ICONERROR);
        delete display;
        return;
    }

    auto h_polarity = (display[0].timing.HSyncPol == 1) ? "+" : "-";
    auto v_polarity = (display[0].timing.VSyncPol == 1) ? "+" : "-";
    auto scantype =   (display[0].timing.interlaced == 1) ? "i" : "p";

    auto mode =         std::format("{}x{}@{:.3f}hz ",  display[0].timing.HVisible, display[0].timing.VVisible, static_cast<double>(display[0].timing.etc.rrx1k) / 1000);
    auto v_frontporch = std::format("vfp {} ",          display[0].timing.VFrontPorch);
    auto h_frontporch = std::format("hfp {} ",          display[0].timing.HFrontPorch);
    auto h_syncwidth =  std::format("{} hs {} ",        h_polarity, display[0].timing.HSyncWidth);
    auto v_syncwidth =  std::format("{} vs {} ",        v_polarity, display[0].timing.VSyncWidth);
    auto h_backporch =  std::format("hbp {} ",          display[0].timing.HTotal - display[0].timing.HVisible - display[0].timing.HFrontPorch - display[0].timing.HSyncWidth);
    auto v_backporch =  std::format("vbp {} ",          display[0].timing.VTotal - display[0].timing.VVisible - display[0].timing.VFrontPorch - display[0].timing.VSyncWidth);
    auto h_total =      std::format("ht {} ",           display[0].timing.HTotal);
    auto v_total =      std::format("vt {} ",           display[0].timing.VTotal);
    auto h_frequency =  std::format("hfreq {:.3f}khz ", static_cast<double>(display[0].timing.pclk * 10) / display[0].timing.HTotal); 
    auto pixelclock =   std::format("pclk {:.3f}mhz ",  static_cast<double>(display[0].timing.pclk) / 100);

    modeString =
        mode + "\n" +
        h_frequency + pixelclock + "\n" +
        h_frontporch + h_syncwidth + h_backporch + h_total + "\n" +
        v_frontporch + v_syncwidth + v_backporch + v_total + "\n";
        
    SetWindowTextA(hTimingText, modeString.c_str());
    delete display;
    return;
}

baseMode GetBaseMode()
{
    baseMode basemode = load_baseMode(dispInfo[disp_idx].dispId);
    return basemode;
}


NvAPI_Status ApplyCustomDisplay()
{
    if (dispInfo[disp_idx].selected_mode_idx >= 0)
    {
        dispInfo[disp_idx].desired_refreshrate = currentModes[dispInfo[disp_idx].selected_mode_idx].refreshrate;
    }

    NV_CUSTOM_DISPLAY* display = new NV_CUSTOM_DISPLAY{};

    NV_TIMING_FLAG flag = { 0 };
    NV_TIMING_INPUT timing = { 0 };

    timing.version = NV_TIMING_INPUT_VER;
    timing.flag = flag;
    timing.type = NV_TIMING_OVERRIDE_CURRENT;

    bool vtlimited = false;

    ret = NvAPI_DISP_GetTiming(dispInfo[disp_idx].dispId, &timing, &display[0].timing); // get timings of the current mode to be used as the base mode
    if (ret != NVAPI_OK)
    {
        MessageBox(NULL, L"NvAPI_DISP_GetTiming() failed = ", L"Error", MB_OK | MB_ICONERROR);
        return ret;
    }

    if (!dispInfo[disp_idx].initDisplaymode && dispInfo[disp_idx].oldpclk != display[0].timing.pclk) dispInfo[disp_idx].initDisplaymode = true; // pixel clock difference windows must have switched to a new base mode reinit

    // check if we have a base mode else create one from the current videomode.

    if (dispInfo[disp_idx].initDisplaymode) {

        dispInfo[disp_idx].basemode = load_baseMode(dispInfo[disp_idx].dispId);
        if (dispInfo[disp_idx].basemode.dispId == 0)
        {
            SetBaseMode();
            dispInfo[disp_idx].basemode = load_baseMode(dispInfo[disp_idx].dispId);
        }
        dispInfo[disp_idx].minVtotal = dispInfo[disp_idx].basemode.minvt;
        dispInfo[disp_idx].maxpclk = dispInfo[disp_idx].basemode.maxpclk;
        display[0].timing.pclk = dispInfo[disp_idx].basemode.maxpclk;
        dispInfo[disp_idx].initDisplaymode = false;
    }
    display[0].version = NV_CUSTOM_DISPLAY_VER;
    display[0].width = display[0].timing.HVisible;
    display[0].height = display[0].timing.VVisible;
    display[0].depth = 0;
    display[0].colorFormat = NV_FORMAT_UNKNOWN;
    display[0].srcPartition = { 0,0,1.0,1.0 };
    display[0].xRatio = 1;
    display[0].yRatio = 1;
    display[0].hwModeSetOnly = 1;

    auto pclk = display[0].timing.pclk * 10000;
    int adjusted_vtotal = floor((double)pclk / (double)((display[0].timing.HTotal * dispInfo[disp_idx].desired_refreshrate)));

    if (adjusted_vtotal < dispInfo[disp_idx].minVtotal) adjusted_vtotal = dispInfo[disp_idx].minVtotal; // don't exceed the min max vtotals
    if (adjusted_vtotal > dispInfo[disp_idx].maxVtotal)
    {
        adjusted_vtotal = dispInfo[disp_idx].maxVtotal;
        vtlimited = true;
    }

    if (dispInfo[disp_idx].round_pixel_clock || vtlimited) {
        display[0].timing.pclk = round((display[0].timing.HTotal * adjusted_vtotal * dispInfo[disp_idx].desired_refreshrate) / 10000);
        if (display[0].timing.pclk > dispInfo[disp_idx].maxpclk) { // don't go over displays max pixel clock
            display[0].timing.pclk = dispInfo[disp_idx].maxpclk;
        }
        pclk = display[0].timing.pclk * 10000;
    }

    auto real = (double)pclk / (display[0].timing.HTotal * adjusted_vtotal);
    auto real_refreshrate = static_cast<int>(round(real * 1000.0));

    if (!dispInfo[disp_idx].spoof_refreshrate) { // report base mode refreshrate to windows prevents games that force the highest refreshrate changing out of the temp mode could cause side effects
        display[0].timing.etc.rrx1k = real_refreshrate;
        display[0].timing.etc.rr = real;
    }

    display[0].timing.VTotal = adjusted_vtotal;

    POINT mousepos;
    GetCursorPos(&mousepos); // on modeswitch mouse resets remember previous cords and restore them after modeswitch

    ret = NvAPI_DISP_TryCustomDisplay(&dispInfo[disp_idx].dispId, 1, &display[0]);
    if (ret != NVAPI_OK)
    {
        MessageBox(NULL, L"NvAPI_DISP_TryCustomDisplay() failed", L"Error Message", MB_OK | MB_ICONERROR);
        return ret;
        delete display;
    }

    SetCursorPos(mousepos.x, mousepos.y);

    dispInfo[disp_idx].oldpclk = display[0].timing.pclk;
    delete display;

    UpdateTimingText();

    if (dispInfo[disp_idx].is_xg2431) {
        // todo
    }

    return ret;
}

void minimizeToTray(HWND hwnd) {
    ShowWindow(hwnd, SW_HIDE);
}

void restoreFromTray(HWND hwnd) {
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;

    switch (msg)
    {
    case WM_CREATE: {
        HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        NOTIFYICONDATA nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = ID_TRAY_APP_ICON;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_SYSICON; 
        nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
        lstrcpy(nid.szTip, TEXT("NvQFTswitcher"));
        Shell_NotifyIcon(NIM_ADD, &nid);
        break;
    }
    case WM_SYSICON: {
        switch (lParam) {
            case WM_RBUTTONDOWN: 
            {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                int clicked_mode_idx = TrackPopupMenu(popmenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                SendMessage(hwnd, WM_NULL, 0, 0);
                if (clicked_mode_idx == popup_exit)
                {
                    SendMessage(hwnd, WM_DESTROY, 0, 0);
                }
                if (clicked_mode_idx > 0 && clicked_mode_idx <= currentModes.size())
                {
                    dispInfo[disp_idx].selected_mode_idx = clicked_mode_idx - 1;
                    ApplyCustomDisplay();
                    ReloadModes(hwnd);
                }
                break;
            }
            case WM_LBUTTONDBLCLK:
            {
                restoreFromTray(hwnd);
                break;
            }
        }
        break;
    }
    case WM_NCRBUTTONDOWN:
    {
        // ignore right click of window frame
        break;
    }
    case WM_SYSCOMMAND:
        switch (LOWORD(wParam))
        {
        case SC_CLOSE:
            minimizeToTray(hwnd);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    case WM_DESTROY:
        quit = true;
        PostQuitMessage(0);
        break;
    case WM_HOTKEY:
        Revert();
        ReloadModes(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case select_display_combo:
            switch (HIWORD(wParam))
            {
            case CBN_SELCHANGE:
                OnComboBoxSelectionChanged(hwnd);
                break;
            }
            break;
        case saved_modes_combo:
            switch (HIWORD(wParam))
            {
            case CBN_SELCHANGE:
                OnSavedModesSelected(hwnd);
                break;
            }
            break;
        case change_refreshrate_btn:
            switch (HIWORD(wParam))
            {
            case BN_CLICKED:
                ChangeRefreshrate(hwnd);
                break;
            }
            break;
        case save_refreshrate_btn:
            switch (HIWORD(wParam))
            {
            case BN_CLICKED:
                SaveRefreshrate(hwnd);
                break;
            }
            break;
        case delete_refreshrate_btn:
            switch (HIWORD(wParam))
            {
            case BN_CLICKED:
                DeleteRefreshrate(hwnd);
                break;
            }
            break;
        case desired_refreshrate_textbox:
            switch (HIWORD(wParam))
            {
            case EN_CHANGE:
                break;
            }
            break;
        }
        break;

    default:
        result = DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return result;
}

int APIENTRY WinMain(
    _In_     HINSTANCE hInst,
    _In_opt_ HINSTANCE hInstPrev,
    _In_     PSTR      cmdline,
    _In_     int       cmdshow)
{
    const wchar_t lpszClassName[] = L"NvQFTswitcher class";
    const wchar_t lpszWindowName[] = L"NvQFTswitcher";

    HWND hwnd;
    MSG msg{};
    WNDCLASSEX window{
        sizeof(WNDCLASSEX),
        0,
        WndProc,
        0,
        0,
        hInst,
        LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1)),
        LoadCursor(NULL, IDC_ARROW),
        NULL,
        NULL,
        lpszClassName,
        LoadIcon(NULL, IDI_APPLICATION)
    };

    if (!RegisterClassEx(&window))
    {
        MessageBox(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    hwnd = CreateWindowExW(
        0,
        lpszClassName,
        lpszWindowName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 210,
        NULL,
        NULL,
        hInst,
        NULL
    );

    LONG_PTR dwStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    dwStyle &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    SetWindowLongPtr(hwnd, GWL_STYLE, dwStyle);

    // Initialize font
    HDC hDC = GetDC(hwnd);
    int nHeight = -MulDiv(11, GetDeviceCaps(hDC, LOGPIXELSY), 82);
    HFONT hFont = CreateFont(nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    ReleaseDC(hwnd, hDC);

    if (hwnd == NULL)
    {
        MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(hwnd, HWND_TOP, width - 300, height - 300, 300, 210, SWP_NOZORDER);

    ShowWindow(hwnd, cmdshow);

    RegisterHotKey(hwnd, 1, MOD_ALT | MOD_SHIFT, 'R'); // use this combo to recover from a bad modeswitch

    try { initDisp(); }
    catch (const std::exception& ex) { MessageBoxA(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR); }

    HWND hDisplayComboBox = CreateWindowEx(0, L"COMBOBOX", L"Collapsed Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
        0, 0, 285, 0, hwnd, (HMENU)select_display_combo, (HINSTANCE)GetWindowLong(hwnd, GWLP_HINSTANCE), NULL);

    for (int i = 0; i < NVAPI_MAX_DISPLAYS; i++)
    {
        if (dispInfo[i].dispId > 0) {

            SendMessage(hDisplayComboBox, CB_ADDSTRING, 0, (LPARAM)dispInfo[i].displayString.c_str());
        }

    }
    SendMessage(hDisplayComboBox, WM_SETFONT, (LPARAM)hFont, TRUE);
    ResizeComboBox(hDisplayComboBox, sizeof(dispInfo));
    SendMessage(hDisplayComboBox, CB_SETCURSEL, 0, 0);

    HWND hChangeRefreshrateButton = CreateWindow(
        L"BUTTON",
        L"Change refreshrate",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        0, 25, 150, 24,
        hwnd,
        (HMENU)change_refreshrate_btn,
        hInst,
        NULL
    );
    SendMessage(hChangeRefreshrateButton, WM_SETFONT, (LPARAM)hFont, TRUE);

    HWND hdesiredResolutionTextBox = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
        151, 25, 150, 24,
        hwnd,
        (HMENU)desired_refreshrate_textbox,
        hInst,
        NULL
    );
    SendMessage(hdesiredResolutionTextBox, WM_SETFONT, (LPARAM)hFont, TRUE);
    SetWindowText(hdesiredResolutionTextBox, L"60.000");

    HWND hSaveRefreshrateButton = CreateWindow(
        L"BUTTON",
        L"Save refreshrate",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        0, 50, 150, 24,
        hwnd,
        (HMENU)save_refreshrate_btn,
        hInst,
        NULL
    );
    SendMessage(hSaveRefreshrateButton, WM_SETFONT, (LPARAM)hFont, TRUE);

    HWND hDeleteRefreshrateButton = CreateWindow(
        L"BUTTON",
        L"Delete refreshrate",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        151, 50, 140, 24,
        hwnd,
        (HMENU)delete_refreshrate_btn,
        hInst,
        NULL
    );
    SendMessage(hDeleteRefreshrateButton, WM_SETFONT, (LPARAM)hFont, TRUE);

    HWND hModesComboBox = CreateWindowEx(0, L"COMBOBOX", L"Collapsed Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
        0, 75, 285, 0, hwnd, (HMENU)saved_modes_combo, (HINSTANCE)GetWindowLong(hwnd, GWLP_HINSTANCE), NULL);
    SendMessage(hModesComboBox, WM_SETFONT, (LPARAM)hFont, TRUE);

    hTimingText = CreateWindowA(
        "STATIC",                      
        "test",
        WS_VISIBLE | WS_CHILD,        
        1, 100, 300, 140,               
        hwnd,                     
        (HMENU)base_mode_text,            
        hInst,                  
        NULL);
    SendMessage(hTimingText, WM_SETFONT, (LPARAM)hFont, TRUE);

    ReloadModes(hwnd);

    while (!quit) {

        Sleep(1);
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 0;
}