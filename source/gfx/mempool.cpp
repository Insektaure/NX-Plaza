#include "gfx/mempool.h"

#include "core/log.h"

namespace nxp {

namespace {
    constexpr uint32_t alignUp(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

bool MemPool::create(dk::Device device, uint32_t flags, uint32_t size)
{
    destroy();

    m_size = alignUp(size, DK_MEMBLOCK_ALIGNMENT);
    m_block = dk::MemBlockMaker { device, m_size }.setFlags(flags).create();
    if (!m_block) {
        LOG("mempool: failed to allocate %u bytes (flags 0x%x)", m_size, flags);
        m_size = 0;
        return false;
    }

    m_cpuAddr = static_cast<uint8_t*>(m_block.getCpuAddr());
    m_gpuAddr = m_block.getGpuAddr();
    m_used = 0;
    return true;
}

void MemPool::destroy()
{
    if (m_block)
        m_block.destroy();
    m_cpuAddr = nullptr;
    m_gpuAddr = DK_GPU_ADDR_INVALID;
    m_size = 0;
    m_used = 0;
}

MemPool::Slice MemPool::allocate(uint32_t size, uint32_t alignment)
{
    Slice slice;
    if (!m_block || size == 0)
        return slice;

    if (alignment < 4)
        alignment = 4;

    uint32_t start = alignUp(m_used, alignment);
    if (start + size > m_size) {
        LOG("mempool: out of memory (%u used, %u requested, %u total)",
            m_used, size, m_size);
        return slice;
    }

    m_used = start + size;

    slice.block = m_block;
    slice.offset = start;
    slice.size = size;
    slice.cpuAddr = m_cpuAddr ? m_cpuAddr + start : nullptr;
    slice.gpuAddr = m_gpuAddr != DK_GPU_ADDR_INVALID ? m_gpuAddr + start : DK_GPU_ADDR_INVALID;
    return slice;
}

} // namespace nxp
