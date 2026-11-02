#pragma once
struct ca_context;

namespace focus {
class EventSound {
public:
    EventSound() = default;
    ~EventSound();
    EventSound(const EventSound&) = delete;
    EventSound& operator=(const EventSound&) = delete;
    void play_completion(const char* description);
private:
    ca_context* context_ = nullptr;
};
}
