#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
struct TrialResult
{
    double delaySeconds = 0.0;
    double reactionMs = 0.0;
    bool falseStart = false;
};

enum class Phase
{
    BeginTrial,
    WaitingForStimulus,
    WaitingForResponse,
    Finished
};

enum class SessionOutcome
{
    Completed,
    Aborted,
    QuitRequested
};

enum class ArgParseResult
{
    Ok,
    ExitRequested,
    Error
};

struct App
{
    HWND hwnd = nullptr;
    UINT width = 0;
    UINT height = 0;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;

    LARGE_INTEGER qpcFreq{};

    int trialCount = 10;
    double minDelaySeconds = 2.0;
    double maxDelaySeconds = 5.0;
    bool runOnceNoPrompt = false;
    std::string jsonOutputPath;
    std::string csvOutputPath;

    int trialIndex = 0;
    Phase phase = Phase::BeginTrial;
    bool hasInput = false;
    bool inputWasFalseStart = false;
    bool escapePressed = false;
    bool quitRequested = false;

    LONGLONG trialStartQpc = 0;
    LONGLONG stimulusQpc = 0;
    LONGLONG inputQpc = 0;
    double scheduledDelaySeconds = 0.0;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> delayDist{2.0, 5.0};
    std::vector<TrialResult> results;
};

double ComputeAverageReactionMs(const App& app);

LONGLONG QpcNow()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

double QpcDeltaToMilliseconds(LONGLONG delta, LONGLONG freq)
{
    return static_cast<double>(delta) * 1000.0 / static_cast<double>(freq);
}

double QpcDeltaToSeconds(LONGLONG delta, LONGLONG freq)
{
    return static_cast<double>(delta) / static_cast<double>(freq);
}

void PrintLastErrorAndExit(const char* message)
{
    std::fprintf(stderr, "%s (GetLastError=%lu)\n", message, GetLastError());
    std::exit(1);
}

void PrintHResultAndExit(const char* message, HRESULT hr)
{
    std::fprintf(stderr, "%s (HRESULT=0x%08lx)\n", message, static_cast<unsigned long>(hr));
    std::exit(1);
}

void PresentSolidColor(App& app, float gray)
{
    const float color[4] = {gray, gray, gray, 1.0f};
    app.context->OMSetRenderTargets(1, app.rtv.GetAddressOf(), nullptr);
    app.context->ClearRenderTargetView(app.rtv.Get(), color);
    app.swapChain->Present(1, 0);
}

void RecordRawInputPress(App& app)
{
    if (app.phase == Phase::WaitingForResponse && !app.hasInput)
    {
        app.inputQpc = QpcNow();
        app.hasInput = true;
        app.inputWasFalseStart = false;
    }
    else if (app.phase == Phase::WaitingForStimulus && !app.hasInput)
    {
        app.inputQpc = QpcNow();
        app.hasInput = true;
        app.inputWasFalseStart = true;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_INPUT:
    {
        if (!app)
        {
            break;
        }

        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
        {
            break;
        }

        if (raw.header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& kb = raw.data.keyboard;
            const bool isBreak = (kb.Flags & RI_KEY_BREAK) != 0;
            if (!isBreak)
            {
                if (kb.VKey == VK_ESCAPE)
                {
                    app->escapePressed = true;
                    return 0;
                }
                RecordRawInputPress(*app);
            }
        }
        else if (raw.header.dwType == RIM_TYPEMOUSE)
        {
            const USHORT f = raw.data.mouse.usButtonFlags;
            if ((f & RI_MOUSE_LEFT_BUTTON_DOWN) ||
                (f & RI_MOUSE_RIGHT_BUTTON_DOWN) ||
                (f & RI_MOUSE_MIDDLE_BUTTON_DOWN) ||
                (f & RI_MOUSE_BUTTON_4_DOWN) ||
                (f & RI_MOUSE_BUTTON_5_DOWN))
            {
                RecordRawInputPress(*app);
            }
        }
        return 0;
    }

    case WM_CLOSE:
        if (app)
        {
            app->quitRequested = true;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (app)
        {
            app->quitRequested = true;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CreateConsole()
{
    if (!AllocConsole())
    {
        PrintLastErrorAndExit("AllocConsole failed");
    }

    FILE* stream = nullptr;
    if (freopen_s(&stream, "CONIN$", "r", stdin) != 0)
    {
        PrintLastErrorAndExit("freopen_s stdin failed");
    }
    if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0)
    {
        PrintLastErrorAndExit("freopen_s stdout failed");
    }
    if (freopen_s(&stream, "CONOUT$", "w", stderr) != 0)
    {
        PrintLastErrorAndExit("freopen_s stderr failed");
    }
}

void RegisterRawInput(HWND hwnd)
{
    RAWINPUTDEVICE devices[2]{};

    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = 0;
    devices[0].hwndTarget = hwnd;

    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = 0;
    devices[1].hwndTarget = hwnd;

    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)))
    {
        PrintLastErrorAndExit("RegisterRawInputDevices failed");
    }
}

HWND CreateWindowForFullscreen(HINSTANCE instance, UINT width, UINT height)
{
    const wchar_t* className = L"PurpleReactionWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.style = CS_OWNDC;

    if (!RegisterClassExW(&wc))
    {
        PrintLastErrorAndExit("RegisterClassExW failed");
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        className,
        L"PurpleReaction",
        WS_POPUP,
        0,
        0,
        static_cast<int>(width),
        static_cast<int>(height),
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd)
    {
        PrintLastErrorAndExit("CreateWindowExW failed");
    }

    return hwnd;
}

void InitD3D11(App& app, UINT refreshHz)
{
    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = app.width;
    swapDesc.BufferDesc.Height = app.height;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = refreshHz;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.OutputWindow = app.hwnd;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    constexpr UINT levelCount = static_cast<UINT>(sizeof(levels) / sizeof(levels[0]));

    D3D_FEATURE_LEVEL chosen{};
    auto tryCreate = [&](UINT createFlags, const D3D_FEATURE_LEVEL* requestedLevels, UINT requestedCount) -> HRESULT
    {
        return D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            requestedLevels,
            requestedCount,
            D3D11_SDK_VERSION,
            &swapDesc,
            app.swapChain.GetAddressOf(),
            app.device.GetAddressOf(),
            &chosen,
            app.context.GetAddressOf());
    };

    HRESULT hr = tryCreate(flags, levels, levelCount);
    if (hr == E_INVALIDARG)
    {
        hr = tryCreate(flags, levels + 1, levelCount - 1);
    }
#if defined(_DEBUG)
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = tryCreate(flags, levels, levelCount);
        if (hr == E_INVALIDARG)
        {
            hr = tryCreate(flags, levels + 1, levelCount - 1);
        }
    }
#endif
    if (FAILED(hr))
    {
        PrintHResultAndExit("D3D11CreateDeviceAndSwapChain failed", hr);
    }

    ComPtr<IDXGIFactory> factory;
    hr = app.swapChain->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr))
    {
        PrintHResultAndExit("IDXGISwapChain::GetParent failed", hr);
    }
    factory->MakeWindowAssociation(app.hwnd, DXGI_MWA_NO_ALT_ENTER);

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = app.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr))
    {
        PrintHResultAndExit("IDXGISwapChain::GetBuffer failed", hr);
    }
    hr = app.device->CreateRenderTargetView(backBuffer.Get(), nullptr, app.rtv.GetAddressOf());
    if (FAILED(hr))
    {
        PrintHResultAndExit("ID3D11Device::CreateRenderTargetView failed", hr);
    }

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(app.width);
    vp.Height = static_cast<float>(app.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    app.context->RSSetViewports(1, &vp);

    ComPtr<IDXGIDevice1> dxgiDevice;
    hr = app.device.As(&dxgiDevice);
    if (SUCCEEDED(hr))
    {
        dxgiDevice->SetMaximumFrameLatency(1);
    }
}

void SetRealtimePriority(bool enabled)
{
    if (enabled)
    {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    }
    else
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    }
}

void EnterFullscreen(App& app)
{
    ShowWindow(app.hwnd, SW_SHOW);
    SetForegroundWindow(app.hwnd);
    SetFocus(app.hwnd);
    ShowCursor(FALSE);

    const HRESULT hr = app.swapChain->SetFullscreenState(TRUE, nullptr);
    if (FAILED(hr))
    {
        PrintHResultAndExit("SetFullscreenState(TRUE) failed", hr);
    }
}

void LeaveFullscreen(App& app)
{
    if (app.swapChain)
    {
        app.swapChain->SetFullscreenState(FALSE, nullptr);
    }
    ShowCursor(TRUE);
    ShowWindow(app.hwnd, SW_HIDE);
}

void PumpMessages(App& app)
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            app.quitRequested = true;
        }
    }
}

void PrintResults(const App& app)
{
    std::printf("\n=== Results ===\n");
    size_t validCount = 0;
    size_t falseStartCount = 0;
    for (size_t i = 0; i < app.results.size(); ++i)
    {
        if (app.results[i].falseStart)
        {
            std::printf("Trial %zu: delay=%.3f s, FALSE START\n",
                i + 1,
                app.results[i].delaySeconds);
            ++falseStartCount;
        }
        else
        {
            std::printf("Trial %zu: delay=%.3f s, reaction=%.3f ms\n",
                i + 1,
                app.results[i].delaySeconds,
                app.results[i].reactionMs);
            ++validCount;
        }
    }
    if (validCount > 0)
    {
        std::printf("Average reaction (valid only): %.3f ms\n", ComputeAverageReactionMs(app));
    }
    std::printf("Valid trials: %zu, false starts: %zu\n", validCount, falseStartCount);
    std::printf("================\n");
}

double ComputeAverageReactionMs(const App& app)
{
    double total = 0.0;
    size_t validCount = 0;
    for (const TrialResult& trial : app.results)
    {
        if (!trial.falseStart)
        {
            total += trial.reactionMs;
            ++validCount;
        }
    }
    if (validCount == 0)
    {
        return 0.0;
    }
    return total / static_cast<double>(validCount);
}

std::string BuildDefaultCsvPath()
{
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    char fileName[128]{};
    std::snprintf(
        fileName,
        sizeof(fileName),
        "PurpleReaction_%04u%02u%02u_%02u%02u%02u.csv",
        static_cast<unsigned>(localTime.wYear),
        static_cast<unsigned>(localTime.wMonth),
        static_cast<unsigned>(localTime.wDay),
        static_cast<unsigned>(localTime.wHour),
        static_cast<unsigned>(localTime.wMinute),
        static_cast<unsigned>(localTime.wSecond));

    return std::string(fileName);
}

bool ExportResultsCsv(const App& app, const std::string& path)
{
    if (app.results.empty())
    {
        std::printf("No results to export.\n");
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
    {
        std::printf("Failed to open CSV path: %s\n", path.c_str());
        return false;
    }

    out << std::fixed << std::setprecision(6);
    out << "trial,random_delay_seconds,reaction_ms,false_start\n";
    for (size_t i = 0; i < app.results.size(); ++i)
    {
        out << (i + 1) << ","
            << app.results[i].delaySeconds << ",";
        if (app.results[i].falseStart)
        {
            out << ",1\n";
        }
        else
        {
            out << app.results[i].reactionMs << ",0\n";
        }
    }
    out << "average,," << ComputeAverageReactionMs(app) << ",\n";

    if (!out.good())
    {
        std::printf("Failed while writing CSV: %s\n", path.c_str());
        return false;
    }

    std::printf("CSV exported: %s\n", path.c_str());
    return true;
}

bool ExportResultsJson(const App& app, const std::string& path)
{
    if (app.results.empty())
    {
        std::printf("No results to export.\n");
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
    {
        std::printf("Failed to open JSON path: %s\n", path.c_str());
        return false;
    }

    size_t validCount = 0;
    size_t falseStartCount = 0;
    for (const TrialResult& trial : app.results)
    {
        if (trial.falseStart)
        {
            ++falseStartCount;
        }
        else
        {
            ++validCount;
        }
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"trial_count\": " << app.results.size() << ",\n";
    out << "  \"valid_count\": " << validCount << ",\n";
    out << "  \"false_start_count\": " << falseStartCount << ",\n";
    out << "  \"average_reaction_ms\": ";
    if (validCount > 0)
    {
        out << ComputeAverageReactionMs(app);
    }
    else
    {
        out << "null";
    }
    out << ",\n";
    out << "  \"trials\": [\n";
    for (size_t i = 0; i < app.results.size(); ++i)
    {
        const TrialResult& trial = app.results[i];
        out << "    {\"trial\": " << (i + 1)
            << ", \"random_delay_seconds\": " << trial.delaySeconds
            << ", \"reaction_ms\": ";
        if (trial.falseStart)
        {
            out << "null";
        }
        else
        {
            out << trial.reactionMs;
        }
        out << ", \"false_start\": " << (trial.falseStart ? "true" : "false") << "}";
        if (i + 1 < app.results.size())
        {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    if (!out.good())
    {
        std::printf("Failed while writing JSON: %s\n", path.c_str());
        return false;
    }

    std::printf("JSON exported: %s\n", path.c_str());
    return true;
}

std::string WideToUtf8(const wchar_t* value)
{
    if (!value || *value == L'\0')
    {
        return {};
    }

    const int requiredChars = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (requiredChars <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(requiredChars), '\0');
    const int convertedChars = WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        result.data(),
        requiredChars,
        nullptr,
        nullptr);
    if (convertedChars <= 1)
    {
        return {};
    }
    result.resize(static_cast<size_t>(convertedChars) - 1);
    return result;
}

bool TryParseDoubleW(const wchar_t* value, double& out)
{
    if (!value || *value == L'\0')
    {
        return false;
    }

    wchar_t* endPtr = nullptr;
    const double parsed = wcstod(value, &endPtr);
    if (endPtr == value || *endPtr != L'\0')
    {
        return false;
    }

    out = parsed;
    return true;
}

bool TryParseIntW(const wchar_t* value, int& out)
{
    if (!value || *value == L'\0')
    {
        return false;
    }

    wchar_t* endPtr = nullptr;
    const long parsed = wcstol(value, &endPtr, 10);
    if (endPtr == value || *endPtr != L'\0')
    {
        return false;
    }
    if (parsed < 1 || parsed > 1000000)
    {
        return false;
    }

    out = static_cast<int>(parsed);
    return true;
}

bool TryParseIntNarrow(const std::string& value, int& out)
{
    if (value.empty())
    {
        return false;
    }

    char* endPtr = nullptr;
    const long parsed = std::strtol(value.c_str(), &endPtr, 10);
    if (endPtr == value.c_str() || *endPtr != '\0')
    {
        return false;
    }
    if (parsed < 1 || parsed > 1000000)
    {
        return false;
    }

    out = static_cast<int>(parsed);
    return true;
}

bool TryParseDoubleNarrow(const std::string& value, double& out)
{
    if (value.empty())
    {
        return false;
    }

    char* endPtr = nullptr;
    const double parsed = std::strtod(value.c_str(), &endPtr);
    if (endPtr == value.c_str() || *endPtr != '\0')
    {
        return false;
    }

    out = parsed;
    return true;
}

void PrintUsage()
{
    std::printf("Usage:\n");
    std::printf("  PurpleReaction.exe [--min-delay seconds] [--max-delay seconds] [--trials count]\n");
    std::printf("                     [--run-once] [--json-out path] [--csv-out path]\n");
    std::printf("Defaults: --min-delay 2.0 --max-delay 5.0 --trials 10\n");
}

ArgParseResult ParseArgs(App& app)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        std::printf("Failed to parse command line.\n");
        return ArgParseResult::Error;
    }

    bool ok = true;
    bool helpRequested = false;
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* arg = argv[i];

        if (wcscmp(arg, L"--min-delay") == 0)
        {
            if (i + 1 >= argc || !TryParseDoubleW(argv[++i], app.minDelaySeconds))
            {
                ok = false;
                break;
            }
        }
        else if (wcscmp(arg, L"--max-delay") == 0)
        {
            if (i + 1 >= argc || !TryParseDoubleW(argv[++i], app.maxDelaySeconds))
            {
                ok = false;
                break;
            }
        }
        else if (wcscmp(arg, L"--trials") == 0)
        {
            if (i + 1 >= argc || !TryParseIntW(argv[++i], app.trialCount))
            {
                ok = false;
                break;
            }
        }
        else if (wcscmp(arg, L"--run-once") == 0)
        {
            app.runOnceNoPrompt = true;
        }
        else if (wcscmp(arg, L"--json-out") == 0)
        {
            if (i + 1 >= argc)
            {
                ok = false;
                break;
            }
            app.jsonOutputPath = WideToUtf8(argv[++i]);
            if (app.jsonOutputPath.empty())
            {
                ok = false;
                break;
            }
        }
        else if (wcscmp(arg, L"--csv-out") == 0)
        {
            if (i + 1 >= argc)
            {
                ok = false;
                break;
            }
            app.csvOutputPath = WideToUtf8(argv[++i]);
            if (app.csvOutputPath.empty())
            {
                ok = false;
                break;
            }
        }
        else if (wcscmp(arg, L"--help") == 0 || wcscmp(arg, L"-h") == 0)
        {
            helpRequested = true;
            break;
        }
        else
        {
            ok = false;
            break;
        }
    }

    LocalFree(argv);

    if (helpRequested)
    {
        return ArgParseResult::ExitRequested;
    }
    if (!ok || app.minDelaySeconds <= 0.0 || app.maxDelaySeconds <= 0.0 || app.minDelaySeconds >= app.maxDelaySeconds)
    {
        return ArgParseResult::Error;
    }

    return ArgParseResult::Ok;
}

std::string ReadLine(const char* prompt)
{
    std::printf("%s", prompt);
    std::fflush(stdout);

    std::string line;
    if (!std::getline(std::cin, line))
    {
        return {};
    }
    return line;
}

int PromptChoice(const char* prompt, int minValue, int maxValue)
{
    for (;;)
    {
        const std::string line = ReadLine(prompt);
        int value = 0;
        if (TryParseIntNarrow(line, value) && value >= minValue && value <= maxValue)
        {
            return value;
        }
        std::printf("Invalid selection. Enter %d-%d.\n", minValue, maxValue);
    }
}

void PromptCsvExport(const App& app)
{
    if (app.results.empty())
    {
        return;
    }

    for (;;)
    {
        std::printf("\n=== CSV Export ===\n");
        std::printf("1. Export to default filename\n");
        std::printf("2. Export to custom path\n");
        std::printf("3. Skip\n");

        const int choice = PromptChoice("Select option: ", 1, 3);
        if (choice == 3)
        {
            return;
        }

        if (choice == 1)
        {
            const std::string path = BuildDefaultCsvPath();
            if (ExportResultsCsv(app, path))
            {
                return;
            }
            continue;
        }

        const std::string path = ReadLine("Enter CSV output path: ");
        if (path.empty())
        {
            std::printf("Path cannot be empty.\n");
            continue;
        }
        if (ExportResultsCsv(app, path))
        {
            return;
        }
    }
}

void ShowAboutPage()
{
    std::printf("\n=== About PurpleReaction ===\n");
    std::printf("Purpose: measure human reaction time with low-latency timing.\n");
    std::printf("Timing: QueryPerformanceCounter for stimulus and input timestamps.\n");
    std::printf("Input: Raw Input API for keyboard/mouse press events.\n");
    std::printf("Display: DirectX 11 exclusive fullscreen, VSync present.\n");
    std::printf("Stimulus: black screen -> white screen only (no animations).\n");
    std::printf("============================\n");
    (void)ReadLine("Press Enter to return to menu...");
}

void ShowSettingsPage(App& app)
{
    for (;;)
    {
        std::printf("\n=== Settings ===\n");
        std::printf("1. Min random delay (seconds): %.3f\n", app.minDelaySeconds);
        std::printf("2. Max random delay (seconds): %.3f\n", app.maxDelaySeconds);
        std::printf("3. Trial count: %d\n", app.trialCount);
        std::printf("4. Back\n");

        const int choice = PromptChoice("Select option: ", 1, 4);
        if (choice == 4)
        {
            break;
        }

        if (choice == 1)
        {
            const std::string line = ReadLine("New min delay (seconds): ");
            double value = 0.0;
            if (!TryParseDoubleNarrow(line, value) || value <= 0.0 || value >= app.maxDelaySeconds)
            {
                std::printf("Invalid value. Must be > 0 and < current max delay.\n");
                continue;
            }
            app.minDelaySeconds = value;
        }
        else if (choice == 2)
        {
            const std::string line = ReadLine("New max delay (seconds): ");
            double value = 0.0;
            if (!TryParseDoubleNarrow(line, value) || value <= app.minDelaySeconds)
            {
                std::printf("Invalid value. Must be > current min delay.\n");
                continue;
            }
            app.maxDelaySeconds = value;
        }
        else if (choice == 3)
        {
            const std::string line = ReadLine("New trial count: ");
            int value = 0;
            if (!TryParseIntNarrow(line, value))
            {
                std::printf("Invalid value. Must be a positive integer.\n");
                continue;
            }
            app.trialCount = value;
        }
    }
}

void ResetSessionState(App& app)
{
    app.results.clear();
    app.trialIndex = 0;
    app.phase = Phase::BeginTrial;
    app.hasInput = false;
    app.inputWasFalseStart = false;
    app.escapePressed = false;
    app.trialStartQpc = 0;
    app.stimulusQpc = 0;
    app.inputQpc = 0;
    app.scheduledDelaySeconds = 0.0;
    app.delayDist = std::uniform_real_distribution<double>(app.minDelaySeconds, app.maxDelaySeconds);
}

SessionOutcome RunTestSession(App& app, bool promptForStart)
{
    ResetSessionState(app);

    std::printf("\n=== Test Run ===\n");
    std::printf("Wait for white screen, then press any key or mouse button as fast as possible.\n");
    std::printf("Press Esc during a run to abort back to menu.\n");
    if (promptForStart)
    {
        std::printf("Fullscreen starts after you press Enter.\n");
        (void)ReadLine("Press Enter to begin...");
    }

    EnterFullscreen(app);
    SetRealtimePriority(true);

    SessionOutcome outcome = SessionOutcome::Completed;
    bool sessionActive = true;
    while (sessionActive)
    {
        PumpMessages(app);
        if (app.quitRequested)
        {
            outcome = SessionOutcome::QuitRequested;
            break;
        }
        if (app.escapePressed)
        {
            outcome = SessionOutcome::Aborted;
            break;
        }

        const LONGLONG now = QpcNow();

        switch (app.phase)
        {
        case Phase::BeginTrial:
            app.scheduledDelaySeconds = app.delayDist(app.rng);
            app.trialStartQpc = now;
            app.stimulusQpc = 0;
            app.inputQpc = 0;
            app.hasInput = false;
            app.inputWasFalseStart = false;

            PresentSolidColor(app, 0.0f);

            std::printf("Trial %d/%d: waiting %.3f s\n",
                app.trialIndex + 1,
                app.trialCount,
                app.scheduledDelaySeconds);

            app.phase = Phase::WaitingForStimulus;
            break;

        case Phase::WaitingForStimulus:
        {
            if (app.hasInput && app.inputWasFalseStart)
            {
                app.results.push_back(TrialResult{
                    app.scheduledDelaySeconds,
                    0.0,
                    true
                    });

                std::printf("  False start: input before stimulus.\n");

                ++app.trialIndex;
                app.phase = (app.trialIndex >= app.trialCount) ? Phase::Finished : Phase::BeginTrial;
                break;
            }

            const double elapsed = QpcDeltaToSeconds(now - app.trialStartQpc, app.qpcFreq.QuadPart);
            if (elapsed >= app.scheduledDelaySeconds)
            {
                const LONGLONG t0 = QpcNow();
                PresentSolidColor(app, 1.0f);
                const LONGLONG t1 = QpcNow();

                // Present blocks with VSync; midpoint around this call is used as the displayed stimulus timestamp.
                app.stimulusQpc = (t0 + t1) / 2;
                app.phase = Phase::WaitingForResponse;
            }
            else
            {
                const double remaining = app.scheduledDelaySeconds - elapsed;
                if (remaining > 0.003)
                {
                    Sleep(1);
                }
                else
                {
                    SwitchToThread();
                }
            }
            break;
        }

        case Phase::WaitingForResponse:
            if (app.hasInput && !app.inputWasFalseStart)
            {
                const double reactionMs = QpcDeltaToMilliseconds(
                    app.inputQpc - app.stimulusQpc,
                    app.qpcFreq.QuadPart);

                app.results.push_back(TrialResult{
                    app.scheduledDelaySeconds,
                    reactionMs,
                    false
                    });

                std::printf("  Reaction: %.3f ms\n", reactionMs);

                ++app.trialIndex;
                app.phase = (app.trialIndex >= app.trialCount) ? Phase::Finished : Phase::BeginTrial;
            }
            else
            {
                SwitchToThread();
            }
            break;

        case Phase::Finished:
            sessionActive = false;
            outcome = SessionOutcome::Completed;
            break;
        }
    }

    SetRealtimePriority(false);
    LeaveFullscreen(app);

    if (outcome == SessionOutcome::Completed)
    {
        PrintResults(app);
    }
    else if (outcome == SessionOutcome::Aborted)
    {
        std::printf("\nRun aborted.\n");
    }

    return outcome;
}

int PromptPostRunChoice()
{
    std::printf("\n=== Next Action ===\n");
    std::printf("1. Redo test\n");
    std::printf("2. Back to main menu\n");
    std::printf("3. Quit\n");
    return PromptChoice("Select option: ", 1, 3);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    App app{};

    const ArgParseResult argResult = ParseArgs(app);
    if (argResult == ArgParseResult::ExitRequested)
    {
        if (!app.runOnceNoPrompt)
        {
            CreateConsole();
            PrintUsage();
        }
        return 0;
    }
    if (argResult == ArgParseResult::Error)
    {
        if (!app.runOnceNoPrompt)
        {
            CreateConsole();
            PrintUsage();
        }
        return 1;
    }
    if (!app.runOnceNoPrompt)
    {
        CreateConsole();
    }

    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    {
        PrintLastErrorAndExit("EnumDisplaySettingsW failed");
    }
    app.width = dm.dmPelsWidth;
    app.height = dm.dmPelsHeight;

    QueryPerformanceFrequency(&app.qpcFreq);
    if (app.qpcFreq.QuadPart <= 0)
    {
        PrintLastErrorAndExit("QueryPerformanceFrequency failed");
    }

    app.hwnd = CreateWindowForFullscreen(instance, app.width, app.height);
    SetWindowLongPtrW(app.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    RegisterRawInput(app.hwnd);
    InitD3D11(app, dm.dmDisplayFrequency > 0 ? dm.dmDisplayFrequency : 60);
    ShowWindow(app.hwnd, SW_HIDE);

    int exitCode = 0;
    if (app.runOnceNoPrompt)
    {
        const SessionOutcome outcome = RunTestSession(app, false);
        if (outcome == SessionOutcome::Completed)
        {
            if (!app.csvOutputPath.empty() && !ExportResultsCsv(app, app.csvOutputPath))
            {
                exitCode = 2;
            }
            if (!app.jsonOutputPath.empty() && !ExportResultsJson(app, app.jsonOutputPath))
            {
                exitCode = 2;
            }
        }
        else if (outcome == SessionOutcome::Aborted)
        {
            exitCode = 3;
        }
        else
        {
            exitCode = 4;
        }
    }
    else
    {
        std::printf("PurpleReaction ready.\n");

        while (!app.quitRequested)
        {
            PumpMessages(app);
            if (app.quitRequested)
            {
                break;
            }

            std::printf("\n=== PurpleReaction ===\n");
            std::printf("Current settings: delay %.3f-%.3f s, trials %d\n",
                app.minDelaySeconds,
                app.maxDelaySeconds,
                app.trialCount);
            std::printf("1. Start test\n");
            std::printf("2. Settings\n");
            std::printf("3. About\n");
            std::printf("4. Quit\n");

            const int menuChoice = PromptChoice("Select option: ", 1, 4);

            if (menuChoice == 1)
            {
                bool keepRunningTests = true;
                while (keepRunningTests && !app.quitRequested)
                {
                    const SessionOutcome outcome = RunTestSession(app, true);
                    if (outcome == SessionOutcome::QuitRequested)
                    {
                        app.quitRequested = true;
                        break;
                    }
                    if (outcome == SessionOutcome::Completed)
                    {
                        PromptCsvExport(app);
                    }

                    const int next = PromptPostRunChoice();
                    if (next == 1)
                    {
                        keepRunningTests = true;
                    }
                    else if (next == 2)
                    {
                        keepRunningTests = false;
                    }
                    else
                    {
                        app.quitRequested = true;
                        keepRunningTests = false;
                    }
                }
            }
            else if (menuChoice == 2)
            {
                ShowSettingsPage(app);
            }
            else if (menuChoice == 3)
            {
                ShowAboutPage();
            }
            else
            {
                app.quitRequested = true;
            }
        }
    }

    if (app.swapChain)
    {
        app.swapChain->SetFullscreenState(FALSE, nullptr);
    }
    ShowCursor(TRUE);
    if (app.hwnd)
    {
        DestroyWindow(app.hwnd);
    }

    return exitCode;
}
