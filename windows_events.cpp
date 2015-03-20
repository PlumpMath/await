// windows_events.cpp : Defines the entry point for the application.
//
// In VS 2015 x64 Native prompt:
// CL.exe /Zi /nologo /W3 /GS- /Od /D _DEBUG /D WIN32 /D _UNICODE /D UNICODE /Gm /EHsc /MDd /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TP /await windows_events.cpp
//

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")

#include <new>
#include <utility>
#include <memory>
#include <type_traits>
#include <tuple>
#include <list>

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

#include "unwinder.h"

#include "windows_user.h"
namespace wu = windows_user;

#include "async_generator.h"
#include "async_operators.h"
namespace ao = async::operators;
#include "async_subjects.h"
namespace asub = async::subjects;
#include "async_windows_user.h"
namespace awu = async::windows_user;

struct RootWindow : public awu::async_messages, public awu::enable_send_call<RootWindow, WM_USER+1>
{
    // window class
    using window_class = wu::window_class<RootWindow>;
    static LPCWSTR class_name() {return L"Scratch";}
    static void change_class(WNDCLASSEX& wcex) {}

    // createstruct parameter type
    using param_type = std::wstring;

    // public methods

    // static methods use a window message per call

    static LRESULT set_title(HWND w, const std::wstring& t) {
        return send_call(w, [&](RootWindow& r){
            r.set_title(t);
            return 0;
        });
    }
    static std::wstring get_title(HWND w) {
        std::wstring t;
        send_call(w, [&](RootWindow& r){
            t = r.get_title();
            return 0;
        });
        return t;
    }

    // instance methods are accessed using static send_call(hwnd, [](RootWindow& r){. . .});
    // send_call uses one window message, the lambda can call many instance methods.

    void set_title(const std::wstring& t) {
        title = t;
    }
    const std::wstring& get_title() {
        return title;
    }

    // lifetime

    // called during WM_NCDESTROY
    ~RootWindow() {
        PostQuitMessage(0);
    }

    // called during WM_NCCREATE
    RootWindow(HWND w, LPCREATESTRUCT, param_type* title)
        : window(w)
        , title(title ? *title : L"RootWindow")
        , position{40, 10} {
        // listen for the following messages
        OnPaint();
        OnPrintClient();
        OnKeyDown();
        OnMovesWhileLButtonDown();
    }

private:
    // implementation

    HWND window;
    std::wstring title;
    POINTS position;

    void PaintContent(PAINTSTRUCT& ps) {
        RECT rect;
        GetClientRect (window, &rect) ;
        SetTextColor(ps.hdc, 0x00000000);
        SetBkMode(ps.hdc,TRANSPARENT);
        rect.left=position.x;
        rect.top=position.y;
        DrawText( ps.hdc, title.c_str(), -1, &rect, DT_SINGLELINE | DT_NOCLIP  ) ;
    }

    auto OnKeyDown() -> std::future<void> {
        for __await (auto& m : messages<WM_KEYDOWN>()) {
            m.handled(); // skip DefWindowProc

            MessageBox(window, L"KeyDown", L"RootWindow", MB_OK);
            // NOTE: MessageBox pumps messages, but this subscription only
            // receives messages if it is suspended by 'for await', so any
            // WM_KEYDOWN arriving while the message box is up is not delivered.
            // the other subscriptions will receive messages.
        }
    }

    auto OnMovesWhileLButtonDown() -> std::future<void> {

        auto moves_while_lbitton_down = messages<WM_LBUTTONDOWN>() |
            ao::flat_map(
                [this](auto& m) -> async::async_generator<Message> {
                    m.handled(); // skip DefWindowProc

                    return messages<WM_MOUSEMOVE>() |
                        ao::take_until(messages<WM_LBUTTONUP>());
                });

        for __await (auto& m : moves_while_lbitton_down) {
            m.handled(); // skip DefWindowProc

            position = MAKEPOINTS(m.lParam);
            InvalidateRect(window, nullptr, true);
        }
    }

    auto OnPaint() -> std::future<void> {
        for __await (auto& m : messages<WM_PAINT>()) {
            m.handled(); // skip DefWindowProc

            PAINTSTRUCT ps;
            BeginPaint(window, &ps);
            PaintContent(ps);
            EndPaint(window, &ps);
        }
    }

    auto OnPrintClient() -> std::future<void> {
        for __await (auto& m : messages<WM_PRINTCLIENT, HDC>()) {
            m.handled(); // skip DefWindowProc

            PAINTSTRUCT ps;
            ps.hdc = m.wParam;
            GetClientRect(window, &ps.rcPaint);
            PaintContent(ps);
        }
    }
};

int PASCAL
wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int nShowCmd)
{
    HRESULT hr = S_OK;

    hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        return FALSE;
    }
    ON_UNWIND_AUTO([&]{CoUninitialize();});

    InitCommonControls();

    RootWindow::window_class::Register();

    LONG winerror = ERROR_SUCCESS;

    std::wstring title{L"Scratch App - RootWindow"};

    // normal create window call, just takes the class name and optional create parameters
    HWND window = CreateWindow(
        RootWindow::window_class::Name(), title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL,
        hinst,
        &title);
    if (!window) {winerror = GetLastError();}

    if (!!winerror || !window)
    {
        return winerror;
    }

    ShowWindow(window, nShowCmd);

    // interact with window safely on the UI thread from another thread
    auto settitle = std::async([window](){

        // by static method
        RootWindow::set_title(window, L"SET_TITLE! Scratch App - RootWindow");

        // or multiple instance methods
        RootWindow::send_call(window, [](RootWindow& r){
            r.set_title(L"SEND_CALL! " + r.get_title());
            return 0;
        });
    });

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    settitle.get();

    return 0;
}
