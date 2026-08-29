#include "gfx/gpu.h"

#include "core/log.h"

#include <array>

namespace nxp {

namespace {
    constexpr uint32_t kCodePoolSize = 256 * 1024;
    constexpr uint32_t kDataPoolSize = 20 * 1024 * 1024;
    constexpr uint32_t kStaticImagePoolSize = 12 * 1024 * 1024; // the glyph atlas
    constexpr uint32_t kFramebufferPoolSize = 32 * 1024 * 1024;

    constexpr uint32_t kStaticCmdSize = 16 * 1024;
    constexpr uint32_t kFrameCmdSize = 512 * 1024; // per frame slice
}

bool Gpu::init()
{
    m_device = dk::DeviceMaker {}.create();
    if (!m_device) {
        LOG("gpu: dk::DeviceMaker failed");
        return false;
    }

    m_queue = dk::QueueMaker { m_device }.setFlags(DkQueueFlags_Graphics).create();
    if (!m_queue) {
        LOG("gpu: dk::QueueMaker failed");
        return false;
    }

    bool ok = true;
    ok &= m_codePool.create(m_device,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code,
        kCodePoolSize);
    ok &= m_dataPool.create(m_device,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached,
        kDataPoolSize);
    ok &= m_staticImagePool.create(m_device,
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
        kStaticImagePoolSize);
    ok &= m_framebufferPool.create(m_device,
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
        kFramebufferPoolSize);
    if (!ok)
        return false;

    m_staticCmd = dk::CmdBufMaker { m_device }.create();
    MemPool::Slice staticMem = m_dataPool.allocate(kStaticCmdSize, DK_CMDMEM_ALIGNMENT);
    if (!staticMem)
        return false;
    m_staticCmd.addMemory(staticMem.block, staticMem.offset, staticMem.size);

    m_frameCmd = dk::CmdBufMaker { m_device }.create();
    m_frameCmdMem = m_dataPool.allocate(kFrameCmdSize * NumFrames, DK_CMDMEM_ALIGNMENT);
    if (!m_frameCmdMem)
        return false;

    m_ready = true;
    return true;
}

void Gpu::exit()
{
    if (m_queue)
        m_queue.waitIdle();

    destroyFramebuffers();

    m_frameCmd = nullptr;
    m_staticCmd = nullptr;

    m_framebufferPool.destroy();
    m_staticImagePool.destroy();
    m_dataPool.destroy();
    m_codePool.destroy();

    m_queue = nullptr;
    m_device = nullptr;
    m_ready = false;
}

void Gpu::setOperationMode(AppletOperationMode mode)
{
    if (!m_ready)
        return;

    m_mode = mode;
    destroyFramebuffers();

    // Docked output is 1080p, handheld is the panel's native 720p. Rendering
    // 1080p while handheld would only be downscaled by the compositor.
    if (mode == AppletOperationMode_Console) {
        m_width = 1920;
        m_height = 1080;
    } else {
        m_width = 1280;
        m_height = 720;
    }

    createFramebuffers();
    LOG("gpu: framebuffers now %ux%u", m_width, m_height);
}

void Gpu::createFramebuffers()
{
    dk::ImageLayout layout;
    dk::ImageLayoutMaker { m_device }
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_width, m_height)
        .initialize(layout);

    uint64_t size = layout.getSize();
    uint32_t alignment = layout.getAlignment();

    std::array<DkImage const*, NumFrames> images;
    for (unsigned i = 0; i < NumFrames; i++) {
        MemPool::Slice slice = m_framebufferPool.allocate(static_cast<uint32_t>(size), alignment);
        m_framebuffers[i].initialize(layout, slice.block, slice.offset);

        dk::ImageView colorTarget { m_framebuffers[i] };
        m_staticCmd.bindRenderTargets(&colorTarget);
        m_framebufferCmdLists[i] = m_staticCmd.finishList();

        images[i] = &m_framebuffers[i];
    }

    m_swapchain = dk::SwapchainMaker { m_device, nwindowGetDefault(), images }.create();
}

void Gpu::destroyFramebuffers()
{
    if (!m_swapchain)
        return;

    m_queue.waitIdle();

    // Command lists live inside the static command buffer's memory, so both go
    // away together; the framebuffer pool is then reusable from zero.
    m_staticCmd.clear();
    m_swapchain.destroy();
    m_framebufferPool.reset();

    for (unsigned i = 0; i < NumFrames; i++)
        m_framebufferCmdLists[i] = 0;
}

dk::CmdBuf Gpu::beginFrame()
{
    m_acquiredSlot = m_queue.acquireImage(m_swapchain);
    m_queue.submitCommands(m_framebufferCmdLists[m_acquiredSlot]);

    // Recycle this slice of command memory once the GPU is done with the frame
    // that last used it.
    m_frameCmd.clear();
    m_frameFences[m_frameIndex].wait();
    m_frameCmd.addMemory(m_frameCmdMem.block,
        m_frameCmdMem.offset + m_frameIndex * kFrameCmdSize, kFrameCmdSize);

    m_frameCmd.setViewports(0, { { 0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f } });
    m_frameCmd.setScissors(0, { { 0, 0, m_width, m_height } });

    return m_frameCmd;
}

void Gpu::endFrame()
{
    m_frameCmd.signalFence(m_frameFences[m_frameIndex]);
    m_queue.submitCommands(m_frameCmd.finishList());
    m_queue.presentImage(m_swapchain, m_acquiredSlot);

    m_frameIndex = (m_frameIndex + 1) % NumFrames;
    m_acquiredSlot = -1;
}

} // namespace nxp
