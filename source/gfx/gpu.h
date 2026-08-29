#pragma once

#include "gfx/mempool.h"

#include <switch.h>

#include <deko3d.hpp>

namespace nxp {

// Owns the deko3d device, queue, swapchain and per-frame command memory.
//
// deko3d gives us no default framebuffer and no implicit state, so this class
// is the whole graphics bring-up: two framebuffers, a static command list per
// framebuffer that binds it as the render target, and a double-buffered ring
// of command memory that the renderer records into every frame.
class Gpu {
public:
    static constexpr unsigned NumFrames = 2;

    bool init();
    void exit();

    // Docking or undocking changes the output resolution, which means the
    // framebuffers and the swapchain have to be thrown away and rebuilt.
    void setOperationMode(AppletOperationMode mode);

    // Acquires the next framebuffer, binds it, and opens the frame's command
    // buffer. Returns the command buffer to record into.
    dk::CmdBuf beginFrame();

    // Submits the frame and presents it.
    void endFrame();

    dk::Device device() { return m_device; }
    dk::Queue queue() { return m_queue; }

    MemPool& dataPool() { return m_dataPool; }
    MemPool& codePool() { return m_codePool; }
    MemPool& persistentImagePool() { return m_staticImagePool; }

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    bool handheld() const { return m_mode == AppletOperationMode_Handheld; }

    // Index of the frame resources in use, 0..NumFrames-1. Anything the
    // renderer double buffers (vertices, glyph staging) is indexed by this and
    // is safe to overwrite: beginFrame() already waited on its fence.
    unsigned frameIndex() const { return m_frameIndex; }

private:
    void createFramebuffers();
    void destroyFramebuffers();

    dk::UniqueDevice m_device;
    dk::UniqueQueue m_queue;

    MemPool m_codePool;         // shader code
    MemPool m_dataPool;         // uniforms, vertices, descriptors, staging
    MemPool m_staticImagePool;  // glyph atlas
    MemPool m_framebufferPool;  // framebuffers only, reset on dock/undock

    dk::UniqueCmdBuf m_staticCmd; // records the framebuffer-binding lists
    dk::UniqueCmdBuf m_frameCmd;  // re-recorded every frame

    MemPool::Slice m_frameCmdMem;
    dk::Fence m_frameFences[NumFrames] = {};

    dk::Image m_framebuffers[NumFrames];
    DkCmdList m_framebufferCmdLists[NumFrames] = {};
    dk::UniqueSwapchain m_swapchain;

    AppletOperationMode m_mode = AppletOperationMode_Handheld;
    uint32_t m_width = 1280;
    uint32_t m_height = 720;

    unsigned m_frameIndex = 0;
    int m_acquiredSlot = -1;
    bool m_ready = false;
};

} // namespace nxp
