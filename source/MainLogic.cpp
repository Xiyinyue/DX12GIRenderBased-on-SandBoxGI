//#include "DXRSExampleRTScene.h"
#include "DXRSExampleGIScene.h"
#include "Common.h"

#include <Dbt.h>

using namespace DirectX;

HDEVNOTIFY gNewAudio = nullptr;
LPCWSTR gAppName = L"HighLevelGIRender";
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 渲染类的实例
std::unique_ptr<DXRSExampleGIScene> gSample;

//优先使用独立显卡
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// 程序的入口
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    // 我需要这两个参数的声明符
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    
    
    //检查是否支持SIMD
    if (!XMVerifyCPUSupport())
        return 1;

    //初始化为多线程并发模式
    Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialize))
        return 1;

    //gSample = std::make_unique<DXRSExampleRTScene>();
    gSample = std::make_unique<DXRSExampleGIScene>();
    //创建实例调用它对应的构造

    // 注册类并且创建
    {
        // 注册类
        WNDCLASSEXW wcex;
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInstance;
        wcex.hIcon = (HICON)LoadImage(NULL, L"DXRS.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
        wcex.hIconSm = (HICON)LoadImage(NULL, L"DXRS.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = L"HighLevelGIRender";
        if (!RegisterClassExW(&wcex))
            return 1;

        // 创建窗口
        int w = 1920, h = 1080;

        RECT rc = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };

        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwnd = CreateWindowExW(0, L"HighLevelGIRender", gAppName, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
            nullptr);

        if (!hwnd)
            return 1;

        ShowWindow(hwnd, nCmdShow);

        //传递一个指针，以便于我们可以在消息处理函数的时候使用实例（对象）里的内容
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gSample.get()));

        GetClientRect(hwnd, &rc);

        gSample->Init(hwnd, rc.right - rc.left, rc.bottom - rc.top);
    }

    // 建立消息处理机制，有消息处理消息，没有消息进行gsample的Run,也就是运行
    MSG msg = {};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            gSample->Run();
        }
    }

    gSample.reset();
    //释放com库的资源
    CoUninitialize();

    return (int)msg.wParam;
}

// Windows 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;

    static bool s_in_sizemove = false;
    static bool s_in_suspend = false;
    static bool s_minimized = false;
    static bool s_fullscreen = false;
    // Set s_fullscreen to true if defaulting to fullscreen.

    //auto sample = reinterpret_cast<DXRSExampleRTScene*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    auto sample = reinterpret_cast<DXRSExampleGIScene*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
        if (!gNewAudio)
        {
            // Ask for notification of new audio devices
            DEV_BROADCAST_DEVICEINTERFACE filter = {};
            filter.dbcc_size = sizeof(filter);
            filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            filter.dbcc_classguid = KSCATEGORY_AUDIO;

            gNewAudio = RegisterDeviceNotification(hWnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        }
        break;

    case WM_CLOSE:
        if (gNewAudio)
        {
            UnregisterDeviceNotification(gNewAudio);
            gNewAudio = nullptr;
        }
        DestroyWindow(hWnd);
        break;

    case WM_PAINT:
        if (s_in_sizemove && sample)
        {
            sample->Run();
        }
        else
        {
            hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_SIZE:
        if (!s_in_sizemove && sample)
        {
            sample->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
        }
        break;

    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        break;
        //没有拖拽的时候就开始进行重新设置
    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (sample)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);

            sample->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
        }
        break;

    case WM_GETMINMAXINFO:
    {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 320;
        info->ptMinTrackSize.y = 200;
    }
    break;

    case WM_ACTIVATEAPP:
        //if (sample)
        //{
        //    Keyboard::ProcessMessage(message, wParam, lParam);
        //    Mouse::ProcessMessage(message, wParam, lParam);
        //
        //    if (wParam)
        //    {
        //        sample->OnActivated();
        //    }
        //    else
        //    {
        //        sample->OnDeactivated();
        //    }
        //}
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_INPUT:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEHOVER:
        Mouse::ProcessMessage(message, wParam, lParam);
        break;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        Keyboard::ProcessMessage(message, wParam, lParam);
        break;
        //设置全屏
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
        {
            // Implements the classic ALT+ENTER fullscreen toggle
            if (s_fullscreen)
            {
                SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, 0);

                int width = 800;
                int height = 600;

                ShowWindow(hWnd, SW_SHOWNORMAL);

                SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            else
            {
                SetWindowLongPtr(hWnd, GWL_STYLE, 0);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);

                SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

                ShowWindow(hWnd, SW_SHOWMAXIMIZED);
            }

            s_fullscreen = !s_fullscreen;
        }
        Keyboard::ProcessMessage(message, wParam, lParam);
        break;

    case WM_MENUCHAR:
        // A menu is active and the user presses a key that does not correspond
        // to any mnemonic or accelerator key. Ignore so we don't produce an error beep.
        return MAKELRESULT(0, MNC_CLOSE);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Exit helper
void ExitSample()
{
    PostQuitMessage(0);
}
