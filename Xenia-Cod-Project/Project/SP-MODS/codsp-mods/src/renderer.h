#pragma once

// Sets up the D3D11 Present/ResizeBuffers hooks and the ImGui overlay.
// Called once from the injected DLL's worker thread.
namespace Renderer
{
    void Setup();
    void Shutdown();
}
