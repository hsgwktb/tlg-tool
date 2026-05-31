#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <iostream>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <thread>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "Png2Tlg.h"
#include "TlgConverter.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ole32.lib")

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pImmediateContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ProcessCommandLineArgs();
void RunCliBatchConvert();

char g_inputPng[MAX_PATH] = {0};
char g_outputTlg[MAX_PATH] = {0};
char g_inputTlg[MAX_PATH] = {0};
char g_outputPng[MAX_PATH] = {0};

// Check for command line arguments (batch mode)
bool g_cliMode = false;
bool g_cliBatchMode = false;
bool g_cliPngToTlg = false;
std::string g_cliInputFolder;
std::string g_cliOutputFolder;
bool g_cliStarted = false;
bool g_cliFinished = false;
int g_cliSuccessCount = 0;
int g_cliFailCount = 0;

char g_inputFolder[MAX_PATH] = {0};
char g_outputFolder[MAX_PATH] = {0};
int g_batchMode = 0;

std::string g_statusText = "Ready";
ImVec4 g_statusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
std::mutex g_statusMutex;
std::atomic_bool g_batchRunning(false);

void SetStatus(const std::string& text, ImVec4 color)
{
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_statusText = text;
    g_statusColor = color;
}

void GetStatus(std::string& text, ImVec4& color)
{
    std::lock_guard<std::mutex> lock(g_statusMutex);
    text = g_statusText;
    color = g_statusColor;
}

std::string WideToAnsi(LPCWSTR value)
{
    if (!value) return std::string();
    int needed = WideCharToMultiByte(CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string result(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value, -1, result.data(), needed, nullptr, nullptr);
    return result;
}

bool EnsureDirectory(const std::string& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return std::filesystem::is_directory(path, ec);
    }
    return std::filesystem::create_directories(path, ec) || std::filesystem::is_directory(path, ec);
}

void ProcessCommandLineArgs()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    if (argc >= 4 && WideToAnsi(argv[1]) == "--batch") {
        g_cliMode = true;
        g_cliBatchMode = true;
        g_cliInputFolder = WideToAnsi(argv[2]);
        g_cliOutputFolder = WideToAnsi(argv[3]);
        g_cliPngToTlg = false;
        for (int i = 4; i < argc; ++i) {
            if (WideToAnsi(argv[i]) == "--png2tlg") {
                g_cliPngToTlg = true;
            }
        }
        g_cliStarted = false;
        g_cliFinished = false;

        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    LocalFree(argv);
}

bool OpenFileDialog(HWND hwnd, char* outPath, const char* filter, const char* defExt)
{
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileOpen));
    if (FAILED(hr)) return false;

    COMDLG_FILTERSPEC rgSpec[] = {
        { L"All Files", L"*.*" }
    };
    pFileOpen->SetFileTypes(1, rgSpec);
    pFileOpen->SetTitle(L"Select File");

    hr = pFileOpen->Show(hwnd);
    if (FAILED(hr)) {
        pFileOpen->Release();
        return false;
    }

    IShellItem* pItem = nullptr;
    hr = pFileOpen->GetResult(&pItem);
    if (FAILED(hr)) {
        pFileOpen->Release();
        return false;
    }

    PWSTR pszFilePath = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, outPath, MAX_PATH, nullptr, nullptr);
        CoTaskMemFree(pszFilePath);
    }

    pItem->Release();
    pFileOpen->Release();
    return true;
}

bool OpenFolderDialog(HWND hwnd, char* outPath)
{
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileOpen));
    if (FAILED(hr)) return false;

    DWORD dwOptions;
    pFileOpen->GetOptions(&dwOptions);
    pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
    pFileOpen->SetTitle(L"Select Folder");

    hr = pFileOpen->Show(hwnd);
    if (FAILED(hr)) {
        pFileOpen->Release();
        return false;
    }

    IShellItem* pItem = nullptr;
    hr = pFileOpen->GetResult(&pItem);
    if (FAILED(hr)) {
        pFileOpen->Release();
        return false;
    }

    PWSTR pszFilePath = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, outPath, MAX_PATH, nullptr, nullptr);
        CoTaskMemFree(pszFilePath);
    }

    pItem->Release();
    pFileOpen->Release();
    return true;
}

void UpdateDefaultOutput(const char* inputPath, char* outputPath, const char* newExt)
{
    if (!inputPath[0]) return;

    char drive[MAX_PATH];
    char dir[MAX_PATH];
    char fname[MAX_PATH];
    char ext[MAX_PATH];
    _splitpath(inputPath, drive, dir, fname, ext);

    char newFullPath[MAX_PATH];
    _makepath(newFullPath, drive, dir, fname, newExt);

    strncpy(outputPath, newFullPath, MAX_PATH - 1);
}

void UpdateDefaultOutputFolder(const char* inputPath, char* outputPath)
{
    if (!inputPath[0]) return;

    // Simply copy the input path to output path
    strncpy(outputPath, inputPath, MAX_PATH - 1);
}

void RefreshShellForFile(const char* filePath)
{
    if (!filePath || !filePath[0]) return;

    // Get the parent directory
    char drive[MAX_PATH];
    char dir[MAX_PATH];
    char fname[MAX_PATH];
    char ext[MAX_PATH];
    _splitpath(filePath, drive, dir, fname, ext);

    char folderPath[MAX_PATH];
    _makepath(folderPath, drive, dir, nullptr, nullptr);

    if (folderPath[0]) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH, folderPath, nullptr);
    }
}

void DoConvertPngToTlg()
{
    try {
        SetStatus("Converting PNG -> TLG...", ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

        // Create output directory if not exists
        char drive[MAX_PATH];
        char dir[MAX_PATH];
        char fname[MAX_PATH];
        char ext[MAX_PATH];
        _splitpath(g_outputTlg, drive, dir, fname, ext);
        char outDir[MAX_PATH];
        _makepath(outDir, drive, dir, nullptr, nullptr);
        if (outDir[0]) {
            CreateDirectoryA(outDir, nullptr);
        }

        Png2TlgConverter::convert(g_inputPng, g_outputTlg);

        // Verify file was written
        WIN32_FILE_ATTRIBUTE_DATA fileData;
        if (GetFileAttributesExA(g_outputTlg, GetFileExInfoStandard, &fileData) &&
            fileData.nFileSizeLow > 0) {
            RefreshShellForFile(g_outputTlg);
            SetStatus("PNG -> TLG conversion completed!", ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        } else {
            SetStatus("Error: Output file not created", ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
    }
    catch (const std::exception& e) {
        SetStatus(std::string("Error: ") + e.what(), ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
}

void DoConvertTlgToPng()
{
    try {
        SetStatus("Converting TLG -> PNG...", ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

        // Create output directory if not exists
        char drive[MAX_PATH];
        char dir[MAX_PATH];
        char fname[MAX_PATH];
        char ext[MAX_PATH];
        _splitpath(g_outputPng, drive, dir, fname, ext);
        char outDir[MAX_PATH];
        _makepath(outDir, drive, dir, nullptr, nullptr);
        if (outDir[0]) {
            CreateDirectoryA(outDir, nullptr);
        }

        TlgConverter converter;
        auto image = converter.read(g_inputTlg);
        converter.save(image, g_outputPng);
        delete[] image.pixels;

        // Verify file was written
        WIN32_FILE_ATTRIBUTE_DATA fileData;
        if (GetFileAttributesExA(g_outputPng, GetFileExInfoStandard, &fileData) &&
            fileData.nFileSizeLow > 0) {
            RefreshShellForFile(g_outputPng);
            SetStatus("TLG -> PNG conversion completed!", ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        } else {
            SetStatus("Error: Output file not created", ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
    }
    catch (const std::exception& e) {
        SetStatus(std::string("Error: ") + e.what(), ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
}

std::vector<std::string> GetFilesInFolder(const char* folder, const char* extension)
{
    std::vector<std::string> files;
    std::string searchPath = std::string(folder) + "\\*." + extension;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(std::string(folder) + "\\" + fd.cFileName);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return files;
}

void DoBatchConvert()
{
    try {
        if (!g_inputFolder[0] || !g_outputFolder[0]) {
            SetStatus("Please select input and output folders", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            return;
        }

        if (!EnsureDirectory(g_outputFolder)) {
            SetStatus(std::string("Error: Could not create output folder: ") + g_outputFolder, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            return;
        }

        const char* ext = (g_batchMode == 0) ? "png" : "tlg";
        const char* outExt = (g_batchMode == 0) ? "tlg" : "png";

        std::vector<std::string> files = GetFilesInFolder(g_inputFolder, ext);

        if (files.empty()) {
            SetStatus(std::string("No .") + ext + " files found in input folder", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            return;
        }

        SetStatus("Batch converting...", ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

        int successCount = 0;
        int failCount = 0;

        for (size_t i = 0; i < files.size(); i++) {
            const std::string& inputFile = files[i];

            char drive[MAX_PATH];
            char dir[MAX_PATH];
            char fname[MAX_PATH];
            char fileExt[MAX_PATH];
            _splitpath(inputFile.c_str(), drive, dir, fname, fileExt);

            std::string outputFile = std::string(g_outputFolder) + "\\" + fname + "." + outExt;

            try {
                char progress[512];
                sprintf(progress, "Batch converting %zu/%zu: %s", i + 1, files.size(), fname);
                SetStatus(progress, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

                if (g_batchMode == 0) {
                    Png2TlgConverter::convert(inputFile.c_str(), outputFile.c_str());
                } else {
                    TlgConverter converter;
                    auto image = converter.read(inputFile.c_str());
                    converter.save(image, outputFile.c_str());
                    delete[] image.pixels;
                }

                // Verify file was written
                WIN32_FILE_ATTRIBUTE_DATA fileData;
                if (GetFileAttributesExA(outputFile.c_str(), GetFileExInfoStandard, &fileData) &&
                    fileData.nFileSizeLow > 0) {
                    successCount++;
                } else {
                    failCount++;
                }
            }
            catch (const std::exception& e) {
                fprintf(stderr, "Error converting %s: %s\n", inputFile.c_str(), e.what());
                failCount++;
            }
            catch (...) {
                fprintf(stderr, "Error converting %s: unknown exception\n", inputFile.c_str());
                failCount++;
            }
        }

        char status[256];
        sprintf(status, "Batch conversion completed: %d succeeded, %d failed", successCount, failCount);
        SetStatus(status, (failCount == 0) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.5f, 0.0f, 1.0f));

        // Refresh shell to show new files
        if (g_outputFolder[0]) {
            SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH, g_outputFolder, nullptr);
        }
    }
    catch (const std::exception& e) {
        SetStatus(std::string("Error: ") + e.what(), ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    catch (...) {
        SetStatus("Error: Unknown batch conversion failure", ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
}

void StartBatchConvertAsync()
{
    bool expected = false;
    if (!g_batchRunning.compare_exchange_strong(expected, true)) {
        SetStatus("Batch conversion is already running", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        return;
    }

    std::thread([]() {
        DoBatchConvert();
        g_batchRunning = false;
    }).detach();
}

void RunCliBatchConvert()
{
    // Copy CLI params to GUI variables for reuse
    strncpy(g_inputFolder, g_cliInputFolder.c_str(), MAX_PATH - 1);
    strncpy(g_outputFolder, g_cliOutputFolder.c_str(), MAX_PATH - 1);
    g_batchMode = g_cliPngToTlg ? 0 : 1;  // 0 = PNG->TLG, 1 = TLG->PNG

    printf("TLG Tool - CLI Batch Mode\n");
    printf("Input: %s\n", g_cliInputFolder.c_str());
    printf("Output: %s\n", g_cliOutputFolder.c_str());
    printf("Mode: %s\n", g_cliPngToTlg ? "PNG -> TLG" : "TLG -> PNG");
    printf("---\n");

    // Call the batch convert function directly
    DoBatchConvert();

    printf("---\n");
    printf("Conversion complete.\n");
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // Process command line arguments first
    ProcessCommandLineArgs();

    // If CLI batch mode, run without GUI
    if (g_cliMode && g_cliBatchMode) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        RunCliBatchConvert();
        CoUninitialize();
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"TLG Tool", nullptr};
    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TLG/PNG Converter", WS_OVERLAPPEDWINDOW, 100, 100, 600, 580, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK);
        return 1;
    }

    if (!CreateDeviceD3D(hwnd))
    {
        MessageBoxW(nullptr, L"Failed to create Direct3D device", L"Error", MB_OK);
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;

    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.WindowPadding = ImVec2(15, 15);
    style.FramePadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScaleAllSizes(1.2f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.95f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.18f, 0.18f, 0.20f, 0.95f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.45f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.45f, 0.75f, 0.40f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.45f, 0.75f, 0.60f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pImmediateContext);

    bool show_demo_window = false;
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImVec2 windowSize = ImGui::GetIO().DisplaySize;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(windowSize);

        ImGui::Begin("TLG/PNG Converter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "TLG / PNG Converter");
        ImGui::Separator();

        ImGui::Spacing();

        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None))
        {
            if (ImGui::BeginTabItem("Single Convert"))
            {
                // PNG to TLG Section
                ImGui::PushID("png_to_tlg");
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.0f), "PNG -> TLG");
                ImGui::Spacing();

                ImGui::Text("Input PNG:");
                ImGui::SameLine(120);
                ImGui::InputText("##input_png", g_inputPng, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse##browse_png", ImVec2(80, 0))) {
                    if (OpenFileDialog(hwnd, g_inputPng, "PNG Files\0*.png\0", "png")) {
                        UpdateDefaultOutput(g_inputPng, g_outputTlg, "tlg");
                    }
                }

                ImGui::Text("Output TLG:");
                ImGui::SameLine(120);
                ImGui::InputText("##output_tlg", g_outputTlg, MAX_PATH);

                if (ImGui::Button("Convert##convert_png", ImVec2(120, 35))) {
                    if (g_inputPng[0] && g_outputTlg[0]) {
                        DoConvertPngToTlg();
                    } else {
                        SetStatus("Please select input and output files", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                    }
                }
                ImGui::PopID();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // TLG to PNG Section
                ImGui::PushID("tlg_to_png");
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "TLG -> PNG");
                ImGui::Spacing();

                ImGui::Text("Input TLG:");
                ImGui::SameLine(120);
                ImGui::InputText("##input_tlg", g_inputTlg, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse##browse_tlg", ImVec2(80, 0))) {
                    if (OpenFileDialog(hwnd, g_inputTlg, "TLG Files\0*.tlg;*.tlg5;*.tlg6\0", "tlg")) {
                        UpdateDefaultOutput(g_inputTlg, g_outputPng, "png");
                    }
                }

                ImGui::Text("Output PNG:");
                ImGui::SameLine(120);
                ImGui::InputText("##output_png", g_outputPng, MAX_PATH);

                if (ImGui::Button("Convert##convert_tlg", ImVec2(120, 35))) {
                    if (g_inputTlg[0] && g_outputPng[0]) {
                        DoConvertTlgToPng();
                    } else {
                        SetStatus("Please select input and output files", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                    }
                }
                ImGui::PopID();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Batch Convert"))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Batch Conversion");
                ImGui::Spacing();

                ImGui::RadioButton("PNG -> TLG", &g_batchMode, 0);
                ImGui::SameLine();
                ImGui::RadioButton("TLG -> PNG", &g_batchMode, 1);

                ImGui::Spacing();

                ImGui::Text("Input Folder:");
                ImGui::SameLine(120);
                ImGui::InputText("##input_folder", g_inputFolder, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse##browse_ifolder", ImVec2(80, 0))) {
                    if (OpenFolderDialog(hwnd, g_inputFolder)) {
                        UpdateDefaultOutputFolder(g_inputFolder, g_outputFolder);
                    }
                }

                ImGui::Text("Output Folder:");
                ImGui::SameLine(120);
                ImGui::InputText("##output_folder", g_outputFolder, MAX_PATH);
                ImGui::SameLine();
                if (ImGui::Button("Browse##browse_ofolder", ImVec2(80, 0))) {
                    OpenFolderDialog(hwnd, g_outputFolder);
                }

                ImGui::Spacing();

                const bool batchRunning = g_batchRunning.load();
                if (batchRunning) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button(batchRunning ? "Converting..." : "Convert All##batch_convert", ImVec2(150, 40))) {
                    StartBatchConvertAsync();
                }
                if (batchRunning) {
                    ImGui::EndDisabled();
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Status bar
        std::string statusText;
        ImVec4 statusColor;
        GetStatus(statusText, statusColor);
        ImGui::TextColored(statusColor, "%s", statusText.c_str());

        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = {0.10f, 0.10f, 0.12f, 1.00f};
        g_pImmediateContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pImmediateContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 1;
    sd.BufferDesc.Height = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pImmediateContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pImmediateContext);
    }
    if (res != S_OK) {
        MessageBoxW(nullptr, L"D3D11 device creation failed.\nMake sure you have DirectX 11 installed.", L"Error", MB_OK);
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer != nullptr)
    {
        hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
        if (FAILED(hr)) {
            MessageBoxW(nullptr, L"Failed to create render target view", L"Error", MB_OK);
        }
    }
    else
    {
        MessageBoxW(nullptr, L"Failed to get back buffer", L"Error", MB_OK);
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pImmediateContext) { g_pImmediateContext->Release(); g_pImmediateContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pSwapChain != nullptr)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
