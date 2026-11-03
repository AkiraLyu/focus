#include "focus_timer.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <stdexcept>

namespace focus {
FocusTimer::FocusTimer(Store& store) : store_(store), timer_(store.settings()) {}
void FocusTimer::flush() { if (session_) store_.save_session(*session_); }
void FocusTimer::start(Id task, Microseconds now) {
    if (timer_.running()) return;
    flush();
    if (session_ && session_->completed) session_.reset();
    if (timer_.phase() == Phase::Focus && !session_) {
        session_ = store_.begin_session(task,timer_.mode());
        if (timer_.mode() == Mode::Pomodoro) cycle_sessions_.push_back(session_->id);
    }
    timer_.start(now);
}
Tick FocusTimer::advance(Microseconds now, Microseconds wall_end, bool pause) {
    auto event = pause ? timer_.pause(now) : timer_.advance(now);
    if (session_ && (event.focus_us || event.completed)) {
        session_->add(event.focus_us,wall_end-event.finished_ago_us);
        session_->completed = event.completed;
        try { flush(); }
        catch (...) { timer_.pause(now); throw; }
    }
    if (event.phase_changed) session_.reset();
    if (event.cycle_completed) cycle_sessions_.clear();
    return event;
}
void FocusTimer::finish() {
    flush();
    session_.reset();
    if (timer_.phase() == Phase::LongBreak) cycle_sessions_.clear();
    timer_.finish();
}
void FocusTimer::reset(ResetScope scope) {
    if (scope == ResetScope::Cycle && timer_.mode() != Mode::Pomodoro)
        throw std::logic_error(_("Stopwatch mode has no pomodoro cycle."));
    if (scope == ResetScope::Current && session_ && session_->completed) {
        flush();
        session_.reset();
    }
    const auto ids = scope == ResetScope::Cycle ? cycle_sessions_ :
        session_ ? std::vector<Id>{session_->id} : std::vector<Id>{};
    // Only discard in-memory state after the database transaction succeeds.
    store_.delete_sessions(ids);
    if (scope == ResetScope::Cycle) cycle_sessions_.clear();
    else if (session_) std::erase(cycle_sessions_,session_->id);
    session_.reset();
    timer_.reset(scope);
}
void FocusTimer::set_mode(Mode mode) {
    if (mode == timer_.mode()) return;
    auto changed = timer_;
    changed.set_mode(mode);
    flush();
    timer_ = changed;
    session_.reset();
    cycle_sessions_.clear();
}
void FocusTimer::delete_session(Id id) {
    if (id != 0 && id == active_session()) {
        reset(ResetScope::Current);
        return;
    }
    store_.delete_sessions({id});
    std::erase(cycle_sessions_,id);
    if (session_ && session_->id == id) session_.reset();
}
void FocusTimer::set_settings(Settings settings) {
    auto changed = timer_;
    changed.set_settings(settings);
    flush();
    store_.save_settings(settings);
    timer_ = changed;
    session_.reset();
    cycle_sessions_.clear();
}
}
