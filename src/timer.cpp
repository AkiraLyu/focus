#include "timer.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace focus {
bool Settings::valid() const {
    return focus_minutes >= 1 && focus_minutes <= 180 && short_break_minutes >= 1 &&
           short_break_minutes <= 60 && long_break_minutes >= 1 && long_break_minutes <= 120 &&
           rounds >= 2 && rounds <= 12;
}
Timer::Timer(Settings settings) { set_settings(settings); }
void Timer::set_settings(Settings settings) {
    if (!settings.valid()) throw std::invalid_argument("计时周期超出允许范围");
    if (engaged()) throw std::logic_error("请先结束当前计时");
    settings_ = settings;
    rounds_ = 0;
    phase_ = Phase::Focus;
}
Microseconds Timer::duration() const {
    const int minutes = phase_ == Phase::Focus ? settings_.focus_minutes :
                        phase_ == Phase::ShortBreak ? settings_.short_break_minutes : settings_.long_break_minutes;
    return minutes * 60 * second;
}
Microseconds Timer::display_time() const {
    return mode_ == Mode::Stopwatch ? elapsed_ : std::max<Microseconds>(0, duration() - elapsed_);
}
void Timer::start(Microseconds now) {
    if (running_) return;
    running_ = true;
    last_ = now;
}
Tick Timer::advance(Microseconds now) {
    Tick result;
    if (!running_) return result;
    auto delta = std::max<Microseconds>(0, now - last_);
    last_ = now;
    if (mode_ == Mode::Pomodoro) delta = std::min(delta, duration() - elapsed_);
    elapsed_ += delta;
    if (phase_ == Phase::Focus) result.focus_us = delta;
    if (mode_ == Mode::Pomodoro && elapsed_ >= duration()) {
        result.completed = phase_ == Phase::Focus;
        if (result.completed) {
            ++rounds_;
            phase_ = rounds_ % settings_.rounds == 0 ? Phase::LongBreak : Phase::ShortBreak;
        } else phase_ = Phase::Focus;
        running_ = false;
        elapsed_ = 0;
        result.phase_changed = true;
    }
    return result;
}
Tick Timer::pause(Microseconds now) {
    auto result = advance(now);
    running_ = false;
    return result;
}
void Timer::reset() { running_ = false; elapsed_ = 0; phase_ = Phase::Focus; }
void Timer::set_mode(Mode mode) {
    if (engaged()) throw std::logic_error("请先结束当前计时");
    reset();
    mode_ = mode;
}
std::string clock_text(Microseconds time) {
    const auto seconds = std::max<Microseconds>(0, time) / second;
    char text[64];
    if (seconds >= 3600) std::snprintf(text, sizeof(text), "%02lld:%02lld:%02lld",
        static_cast<long long>(seconds / 3600), static_cast<long long>(seconds / 60 % 60), static_cast<long long>(seconds % 60));
    else std::snprintf(text, sizeof(text), "%02lld:%02lld", static_cast<long long>(seconds / 60), static_cast<long long>(seconds % 60));
    return text;
}
std::string duration_text(Microseconds time) {
    auto seconds = time / second;
    if (seconds < 60) return std::to_string(seconds) + " 秒";
    if (seconds < 3600) return std::to_string(seconds / 60) + " 分钟";
    return std::to_string(seconds / 3600) + " 小时 " + std::to_string(seconds / 60 % 60) + " 分钟";
}
}
