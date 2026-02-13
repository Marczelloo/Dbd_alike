#include "engine/render/RenderThread.hpp"

#include <iostream>

namespace engine::render
{

bool RenderThread::Initialize()
{
    if (m_initialized)
    {
        return true;
    }

    m_shutdown = false;
    m_framesSubmitted = 0;
    m_framesDropped = 0;

    while (!m_frameQueue.empty())
    {
        m_frameQueue.pop();
    }

    m_initialized = true;
    std::cout << "[RenderThread] Initialized (command buffer mode)\n";
    return true;
}

void RenderThread::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_shutdown = true;
    m_submitCondition.notify_all();
    m_completeCondition.notify_all();

    while (!m_frameQueue.empty())
    {
        m_frameQueue.pop();
    }

    m_initialized = false;
    std::cout << "[RenderThread] Shutdown complete\n";
}

void RenderThread::BeginFrame()
{
}

void RenderThread::SubmitFrameData(const RenderFrameData& data)
{
    if (!m_initialized || !m_enabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (m_frameQueue.size() >= kMaxPendingFrames)
    {
        m_frameQueue.pop();
        ++m_framesDropped;
    }

    m_frameQueue.push(data);
    ++m_framesSubmitted;
    m_submitCondition.notify_one();
}

void RenderThread::EndFrame()
{
    m_completeCondition.notify_all();
}

void RenderThread::WaitForSubmit()
{
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completeCondition.wait(lock, [this]() {
        return m_frameQueue.empty() || m_shutdown;
    });
}

std::size_t RenderThread::PendingFrames() const
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_frameQueue.size();
}

RenderThread::Stats RenderThread::GetStats() const
{
    Stats stats;
    stats.framesSubmitted = m_framesSubmitted.load();
    stats.framesDropped = m_framesDropped.load();
    stats.pendingFrames = PendingFrames();
    return stats;
}

}
