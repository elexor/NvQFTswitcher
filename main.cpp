

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <stdexcept>
#include "nvapi.h"
#include "config.h"
#include <filesystem>

#pragma comment(lib, "nvapi64.lib")

#define select_display_combo 1001
#define change_refreshrate_btn 1002
#define desired_refreshrate_textbox 1003
#define save_refreshrate_btn 1004
#define saved_modes_combo 1005
#define delete_refreshrate_btn 1006
#define base_mode_text 1007


int disp_idx;
bool quit;
savedMode mode;
std::vector<savedMode> modes, currentModes;
NvAPI_Status ret = NVAPI_OK;
NvAPI_Status ApplyCustomDisplay();


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
    double desired_refreshrate;
    bool round_pixel_clock = false;
    bool spoof_refreshrate = false;
};

displayInfo dispInfo[NVAPI_MAX_DISPLAYS]{};

void Revert()
{
    ret = NvAPI_DISP_RevertCustomDisplayTrial(&dispInfo[disp_idx].dispId, 1);
}

void initDisp()
{
    DISPLAY_DEVICE dd{};
    DEVMODEW dm{}, cd{};

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
            ++monitorIndex;
        }
        ++deviceIndex;
    }
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
    int modesCount = 0;

    std::sort(modes.begin(), modes.end(), CompareModes);

    for (size_t i = 0; i < modes.size(); i++)
    {
        if (modes[i].dispId == dispInfo[disp_idx].dispId && modes[i].refreshrate > 0) {
            SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)std::to_wstring(modes[i].refreshrate).c_str());
            currentModes.push_back({ modes[i].dispId, modes[i].refreshrate });
            modesCount++;
        }
    }
    int index = 0;
    for (size_t i = 0; i < currentModes.size(); i++)
    {
        if (currentModes[i].refreshrate == dispInfo[disp_idx].desired_refreshrate)
        {
            index = int(i);
            break;
        }
    }
    ResizeComboBox(hwndCombo, currentModes.size());
    SendMessage(hwndCombo, CB_SETCURSEL, index, 0);
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
    dispInfo[disp_idx].desired_refreshrate = currentModes[index].refreshrate;
    ApplyCustomDisplay();
}

void ChangeRefreshrate(HWND hWnd)
{
    HWND hwndTextbox = GetDlgItem(hWnd, desired_refreshrate_textbox);
    int textLength = GetWindowTextLength(hwndTextbox);
    TCHAR* buffer = new TCHAR[textLength + 1];
    GetWindowText(hwndTextbox, buffer, textLength + 1);
    dispInfo[disp_idx].desired_refreshrate = wcstof(buffer, NULL);
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

baseMode GetBaseMode()
{
    baseMode basemode = load_baseMode(dispInfo[disp_idx].dispId);
    return basemode;
}


NvAPI_Status ApplyCustomDisplay()
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
    return ret;
}



LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;

    switch (msg)
    {
    case WM_DESTROY:
        quit = true;
        PostQuitMessage(0);
        break;
    case WM_HOTKEY:
        Revert();
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
        LoadIcon(NULL, IDI_APPLICATION),
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
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 140,
        NULL,
        NULL,
        hInst,
        NULL
    );

    LONG_PTR dwStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    dwStyle &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtr(hwnd, GWL_STYLE, dwStyle);


    if (hwnd == NULL)
    {
        MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
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

    HWND hModesComboBox = CreateWindowEx(0, L"COMBOBOX", L"Collapsed Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
        0, 75, 285, 0, hwnd, (HMENU)saved_modes_combo, (HINSTANCE)GetWindowLong(hwnd, GWLP_HINSTANCE), NULL);


    HWND hBaseModeText = CreateWindowW(
        L"STATIC",                      
        L"",                            
        WS_VISIBLE | WS_CHILD,        
        0, 110, 300, 20,               
        hwnd,                     
        (HMENU)base_mode_text,            
        hInst,                  
        NULL);                     

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