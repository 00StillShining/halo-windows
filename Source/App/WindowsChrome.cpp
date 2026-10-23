// Small Win32 touches for the main window, kept out of the JUCE translation
// units: the dark title bar (a per-window DWM attribute, not a system setting)
// and the bare-Alt filter (docs/redesign/plan.md sections 3.1 and 2.5).
#include "App/WindowsChrome.h"

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace halo::windowsChrome
{
    void useDarkTitleBar (void* nativeHandle, bool dark)
    {
        auto hwnd = static_cast<HWND> (nativeHandle);
        if (hwnd == nullptr)
            return;
        using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
        static const auto setAttribute = []
        {
            const auto lib = LoadLibraryW (L"dwmapi.dll");
            return lib != nullptr ? reinterpret_cast<SetAttribute> (reinterpret_cast<void*> (GetProcAddress (lib, "DwmSetWindowAttribute"))) : nullptr;
        }();
        if (setAttribute == nullptr)
            return;
        const BOOL value = dark ? TRUE : FALSE;
        constexpr DWORD immersiveDarkMode = 20;       // DWMWA_USE_IMMERSIVE_DARK_MODE
        if (FAILED (setAttribute (hwnd, immersiveDarkMode, &value, sizeof (value))))
            setAttribute (hwnd, 19, &value, sizeof (value)); // pre-20H1 builds used 19
    }

    namespace
    {
        WNDPROC previousProc = nullptr;
        bool altAlone = false;

        LRESULT CALLBACK filterProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            // A bare Alt tap would enter the system-menu loop and swallow the
            // next keystroke (Alt is the encoders' "fine" modifier). Alt+F4,
            // Alt+Space and Alt with a mouse wheel still work.
            if (msg == WM_SYSKEYDOWN && wParam == VK_MENU)
                altAlone = true;
            else if (msg == WM_SYSKEYDOWN || msg == WM_KEYDOWN || msg == WM_MOUSEWHEEL || msg == WM_LBUTTONDOWN)
                altAlone = false;
            if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU && lParam == 0 && altAlone)
                return 0;
            if (msg == WM_SYSKEYUP && wParam == VK_MENU && altAlone)
            {
                altAlone = false;
                return 0;
            }
            return CallWindowProcW (previousProc, hwnd, msg, wParam, lParam);
        }
    }

    void installBareAltFilter (void* nativeHandle)
    {
        auto hwnd = static_cast<HWND> (nativeHandle);
        if (hwnd == nullptr || previousProc != nullptr)
            return;
        previousProc = reinterpret_cast<WNDPROC> (SetWindowLongPtrW (hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR> (&filterProc)));
    }

    bool animationsEnabled()
    {
        BOOL on = TRUE;
        if (! SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &on, 0))
            return true;
        return on != FALSE;
    }

    void removeBareAltFilter (void* nativeHandle)
    {
        auto hwnd = static_cast<HWND> (nativeHandle);
        if (hwnd == nullptr || previousProc == nullptr)
            return;
        SetWindowLongPtrW (hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR> (previousProc));
        previousProc = nullptr;
    }
}
#else
namespace halo::windowsChrome
{
    void useDarkTitleBar (void*, bool) {}
    void installBareAltFilter (void*) {}
    void removeBareAltFilter (void*) {}
    bool animationsEnabled() { return true; }
}
#endif
