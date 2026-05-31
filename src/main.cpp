#include <windows.h>
#include <commdlg.h>
#include <string>
#include <iostream>
#include <fstream>
#include "Png2Tlg.h"
#include "TlgConverter.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

HINSTANCE g_hInst;
HWND g_hwndTab;
HWND g_hwndStatic1, g_hwndEdit1, g_hwndBtn1;
HWND g_hwndStatic2, g_hwndEdit2, g_hwndBtn2;
HWND g_hwndStatic3, g_hwndEdit3, g_hwndBtn3;
HWND g_hwndStatic4, g_hwndEdit4, g_hwndBtn4;
HWND g_hwndBtnConvert;
HWND g_hwndStatus;

#define ID_BTN_BROWSE1    101
#define ID_BTN_BROWSE2    102
#define ID_BTN_BROWSE3    103
#define ID_BTN_BROWSE4    104
#define ID_BTN_CONVERT    105

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ShowFileDialog(HWND hwnd, HWND hwndEdit, const char* filter, const char* defExt);
void DoConvert(int mode);
std::string W2A(LPCWSTR pwstr);

const char* g_filterPng = "PNG Files\0*.png\0All Files\0*.*\0";
const char* g_filterTlg = "TLG Files\0*.tlg;*.tlg5;*.tlg6\0All Files\0*.*\0";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    g_hInst = hInstance;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "TLGTool";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        "TLGTool",
        "TLG/PNG Converter",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 380,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                10, 135, 460, 1, hwnd, NULL, g_hInst, NULL);

            CreateWindowA("STATIC", "PNG -> TLG", WS_CHILD | WS_VISIBLE,
                20, 20, 200, 20, hwnd, NULL, g_hInst, NULL);

            CreateWindowA("STATIC", "PNG File:", WS_CHILD | WS_VISIBLE,
                20, 50, 80, 20, hwnd, NULL, g_hInst, NULL);
            g_hwndEdit1 = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                100, 48, 280, 24, hwnd, NULL, g_hInst, NULL);
            g_hwndBtn1 = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 47, 70, 26, hwnd, (HMENU)ID_BTN_BROWSE1, g_hInst, NULL);

            CreateWindowA("STATIC", "TLG File:", WS_CHILD | WS_VISIBLE,
                20, 80, 80, 20, hwnd, NULL, g_hInst, NULL);
            g_hwndEdit2 = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                100, 78, 280, 24, hwnd, NULL, g_hInst, NULL);
            g_hwndBtn2 = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 77, 70, 26, hwnd, (HMENU)ID_BTN_BROWSE2, g_hInst, NULL);

            CreateWindowA("STATIC", "TLG -> PNG", WS_CHILD | WS_VISIBLE,
                20, 150, 200, 20, hwnd, NULL, g_hInst, NULL);

            CreateWindowA("STATIC", "TLG File:", WS_CHILD | WS_VISIBLE,
                20, 180, 80, 20, hwnd, NULL, g_hInst, NULL);
            g_hwndEdit3 = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                100, 178, 280, 24, hwnd, NULL, g_hInst, NULL);
            g_hwndBtn3 = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 177, 70, 26, hwnd, (HMENU)ID_BTN_BROWSE3, g_hInst, NULL);

            CreateWindowA("STATIC", "PNG File:", WS_CHILD | WS_VISIBLE,
                20, 210, 80, 20, hwnd, NULL, g_hInst, NULL);
            g_hwndEdit4 = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                100, 208, 280, 24, hwnd, NULL, g_hInst, NULL);
            g_hwndBtn4 = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 207, 70, 26, hwnd, (HMENU)ID_BTN_BROWSE4, g_hInst, NULL);

            g_hwndBtnConvert = CreateWindowA("BUTTON", "Convert", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_CENTER,
                180, 255, 120, 35, hwnd, (HMENU)ID_BTN_CONVERT, g_hInst, NULL);

            g_hwndStatus = CreateWindowA("STATIC", "Ready", WS_CHILD | WS_VISIBLE | SS_LEFT,
                20, 305, 440, 20, hwnd, NULL, g_hInst, NULL);
        }
        break;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case ID_BTN_BROWSE1:
                ShowFileDialog(hwnd, g_hwndEdit1, g_filterPng, "png");
                break;
            case ID_BTN_BROWSE2:
                ShowFileDialog(hwnd, g_hwndEdit2, g_filterTlg, "tlg");
                break;
            case ID_BTN_BROWSE3:
                ShowFileDialog(hwnd, g_hwndEdit3, g_filterTlg, "tlg");
                break;
            case ID_BTN_BROWSE4:
                ShowFileDialog(hwnd, g_hwndEdit4, g_filterPng, "png");
                break;
            case ID_BTN_CONVERT:
                {
                    char path1[MAX_PATH] = {0};
                    char path2[MAX_PATH] = {0};
                    char path3[MAX_PATH] = {0};
                    char path4[MAX_PATH] = {0};

                    GetWindowTextA(g_hwndEdit1, path1, MAX_PATH);
                    GetWindowTextA(g_hwndEdit2, path2, MAX_PATH);
                    GetWindowTextA(g_hwndEdit3, path3, MAX_PATH);
                    GetWindowTextA(g_hwndEdit4, path4, MAX_PATH);

                    if (path1[0] && path2[0])
                    {
                        SetWindowTextA(g_hwndStatus, "Converting PNG -> TLG ...");
                        DoConvert(0);
                    }
                    else if (path3[0] && path4[0])
                    {
                        SetWindowTextA(g_hwndStatus, "Converting TLG -> PNG ...");
                        DoConvert(1);
                    }
                    else
                    {
                        MessageBoxA(hwnd, "Please select input and output files", "Info", MB_OK | MB_ICONINFORMATION);
                    }
                }
                break;
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowFileDialog(HWND hwnd, HWND hwndEdit, const char* filter, const char* defExt)
{
    OPENFILENAMEA ofn = {0};
    char szFile[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
    {
        SetWindowTextA(hwndEdit, szFile);
    }
}

void DoConvert(int mode)
{
    try
    {
        if (mode == 0)
        {
            char path1[MAX_PATH] = {0};
            char path2[MAX_PATH] = {0};
            GetWindowTextA(g_hwndEdit1, path1, MAX_PATH);
            GetWindowTextA(g_hwndEdit2, path2, MAX_PATH);

            Png2TlgConverter::convert(path1, path2);
            SetWindowTextA(g_hwndStatus, "PNG -> TLG conversion completed!");
        }
        else
        {
            char path1[MAX_PATH] = {0};
            char path2[MAX_PATH] = {0};
            GetWindowTextA(g_hwndEdit3, path1, MAX_PATH);
            GetWindowTextA(g_hwndEdit4, path2, MAX_PATH);

            TlgConverter converter;
            auto image = converter.read(path1);
            converter.save(image, path2);
            SetWindowTextA(g_hwndStatus, "TLG -> PNG conversion completed!");
        }

        MessageBoxA(NULL, "Conversion successful!", "Done", MB_OK | MB_ICONINFORMATION);
    }
    catch (const std::exception& e)
    {
        std::string err = "Conversion failed: ";
        err += e.what();
        SetWindowTextA(g_hwndStatus, err.c_str());
        MessageBoxA(NULL, err.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        SetWindowTextA(g_hwndStatus, "Conversion failed: Unknown error");
        MessageBoxA(NULL, "Conversion failed: Unknown error", "Error", MB_OK | MB_ICONERROR);
    }
}

std::string W2A(LPCWSTR pwstr)
{
    if (!pwstr) return "";
    int len = WideCharToMultiByte(CP_ACP, 0, pwstr, -1, NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_ACP, 0, pwstr, -1, &s[0], len, NULL, NULL);
    return s;
}