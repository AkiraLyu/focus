#pragma once
#include "store.hpp"
#include <optional>

namespace focus {
// Keeps timer transitions and their persisted focus records together.
class FocusTimer {
public:
    explicit FocusTimer(Store& store);
    const Timer& timer() const { return timer_; }
    void start(Id task, Microseconds now);
    Tick advance(Microseconds now, Microseconds wall_end, bool pause = false);
    void flush();
    void finish();
    void reset(ResetScope scope);
    Id active_session() const { return session_ && !session_->completed ? session_->id : 0; }
    void delete_session(Id id);
    void set_mode(Mode mode);
    void set_settings(Settings settings);
private:
    Store& store_;
    Timer timer_;
    std::optional<Session> session_;
    std::vector<Id> cycle_sessions_;
};
}
