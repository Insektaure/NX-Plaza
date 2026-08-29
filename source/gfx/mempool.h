#pragma once

#include <deko3d.hpp>

#include <cstdint>

namespace nxp {

// A bump allocator over one deko3d memory block.
//
// The UI has three completely different allocation lifetimes and none of them
// need a general purpose heap:
//   * code and the glyph atlas live for the whole process,
//   * framebuffers live until the console is docked or undocked,
//   * per-frame data lives in a ring that is recycled by fences.
// A pool per lifetime, reset wholesale, is both simpler and less fragmenting
// than a real allocator.
class MemPool {
public:
    struct Slice {
        dk::MemBlock block;
        uint32_t offset = 0;
        uint32_t size = 0;
        void* cpuAddr = nullptr;      // null for GPU-only pools
        DkGpuAddr gpuAddr = DK_GPU_ADDR_INVALID;

        explicit operator bool() const { return size != 0; }
    };

    MemPool() = default;
    MemPool(const MemPool&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    ~MemPool() { destroy(); }

    // `size` is rounded up to the memory block granularity.
    bool create(dk::Device device, uint32_t flags, uint32_t size);
    void destroy();

    // Hands the whole block back for reuse. The caller is responsible for
    // having made the GPU idle first.
    void reset() { m_used = 0; }

    Slice allocate(uint32_t size, uint32_t alignment = 4);

    uint32_t used() const { return m_used; }
    uint32_t capacity() const { return m_size; }

private:
    dk::UniqueMemBlock m_block;
    uint8_t* m_cpuAddr = nullptr;
    DkGpuAddr m_gpuAddr = DK_GPU_ADDR_INVALID;
    uint32_t m_size = 0;
    uint32_t m_used = 0;
};

} // namespace nxp
