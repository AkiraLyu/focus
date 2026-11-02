#pragma once
#include <cstdint>
#include <string>

namespace focus {
using Microseconds = std::int64_t;
constexpr Microseconds second = 1'000'000;
enum class Mode { Pomodoro, Stopwatch };
enum class Phase { Focus, ShortBreak, LongBreak };
struct Settings {
    int focus_minutes = 25;
    int short_break_minutes = 5;
    int long_break_minutes = 15;
    int rounds = 4;
    bool valid() const;
};
struct Tick {
    Microseconds focus_us = 0;
    Microseconds finished_ago_us = 0;
    bool completed = false;
    bool phase_changed = false;
};
class Timer {
public:
    explicit Timer(Settings settings = {});
    void start(Microseconds now);
    Tick advance(Microseconds now);
    Tick pause(Microseconds now);
    void reset();
    void set_mode(Mode mode);
    void set_settings(Settings settings);
    bool running() const { return running_; }
    bool engaged() const { return running_ || elapsed_ > 0; }
    Mode mode() const { return mode_; }
    Phase phase() const { return phase_; }
    Microseconds elapsed() const { return elapsed_; }
    Microseconds duration() const;
    Microseconds display_time() const;
    int completed_rounds() const { return rounds_; }
    const Settings& settings() const { return settings_; }
private:
    Settings settings_;
    Mode mode_ = Mode::Pomodoro;
    Phase phase_ = Phase::Focus;
    bool running_ = false;
    Microseconds elapsed_ = 0;
    Microseconds last_ = 0;
    int rounds_ = 0;
};
std::string clock_text(Microseconds time);
std::string duration_text(Microseconds time);
Microseconds timer_now();
}
