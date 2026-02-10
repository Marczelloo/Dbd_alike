#include "engine/core/EventBus.hpp"

namespace engine::core
{
void EventBus::Subscribe(const std::string& eventName, Handler handler)
{
    m_handlers[eventName].push_back(std::move(handler));
}

void EventBus::Publish(Event event)
{
    m_queue.push(std::move(event));
}

void EventBus::DispatchQueued()
{
    while (!m_queue.empty())
    {
        Event event = std::move(m_queue.front());
        m_queue.pop();

        const auto it = m_handlers.find(event.name);
        if (it == m_handlers.end())
        {
            continue;
        }

        for (const Handler& handler : it->second)
        {
            handler(event);
        }
    }
}
} // namespace engine::core
