#pragma once

#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core
{
struct Event
{
    std::string name;
    std::vector<std::string> args;
};

class EventBus
{
public:
    using Handler = std::function<void(const Event&)>;

    void Subscribe(const std::string& eventName, Handler handler);
    void Publish(Event event);
    void DispatchQueued();

private:
    std::unordered_map<std::string, std::vector<Handler>> m_handlers;
    std::queue<Event> m_queue;
};
} // namespace engine::core
