#pragma once

// Custom splash screen. Shows <game>/.iw6sp-mod/assets/splash/splash.bmp as a
// borderless top-most window at launch (covering the vanilla splash), then
// closes itself. Does nothing if the bitmap is absent.
namespace Splash
{
    void Show();
}
