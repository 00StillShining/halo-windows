#pragma once

namespace halo::windowsChrome
{
    // Sets DWMWA_USE_IMMERSIVE_DARK_MODE on this window only.
    void useDarkTitleBar (void* nativeHandle, bool dark);
    // Stops a bare Alt tap entering the system-menu loop (Alt = fine).
    void installBareAltFilter (void* nativeHandle);
    void removeBareAltFilter (void* nativeHandle);
    // Settings > Accessibility > Visual effects > Animation effects (read only).
    bool animationsEnabled();
}
