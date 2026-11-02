#pragma once
#include "timer.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>
struct sqlite3;

namespace focus {
using Id = std::int64_t;
struct Task { Id id; std::string title; bool done; Microseconds total; };
struct Note { Id id; std::string title; std::string body; std::int64_t updated; };
struct WindowState {
    int width = 1040;
    int height = 820;
    bool maximized = false;
};
struct Session {
    Id id = 0;
    Id task_id = 0;
    Mode mode = Mode::Pomodoro;
    bool completed = false;
    Microseconds total = 0;
    std::map<std::string, Microseconds> days;
    Microseconds started_at = 0;
    Microseconds ended_at = 0;
    void add(Microseconds amount, Microseconds wall_end);
};
struct SessionRecord {
    Id id;
    std::string task;
    Mode mode;
    bool completed;
    Microseconds total;
    Microseconds day_total;
    Microseconds started_at;
    Microseconds ended_at;
};
struct DayTotal {
    std::string date; Microseconds total;
    bool operator==(const DayTotal&) const = default;
};
struct TaskTotal {
    std::string title; Microseconds total;
    bool operator==(const TaskTotal&) const = default;
};
struct Statistics {
    Microseconds today = 0;
    Microseconds week = 0;
    Microseconds all = 0;
    int completed = 0;
    std::vector<DayTotal> days;
    std::vector<TaskTotal> tasks;
    bool operator==(const Statistics&) const = default;
};
class Store {
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Settings settings() const;
    void save_settings(const Settings& settings);
    WindowState window_state() const;
    void save_window_state(const WindowState& state);
    std::vector<Task> tasks() const;
    Id add_task(const std::string& title);
    void update_task(Id id, const std::string& title, bool done);
    std::vector<Note> notes() const;
    Id add_note();
    void save_note(Id id, const std::string& title, const std::string& body);
    void delete_note(Id id);
    Session begin_session(Id task, Mode mode);
    void save_session(const Session& session);
    void delete_sessions(const std::vector<Id>& ids);
    std::vector<SessionRecord> sessions_on(const std::string& day) const;
    Statistics statistics() const;
private:
    struct Totals {
        Microseconds all = 0;
        int completed = 0;
        std::map<Id,Microseconds> tasks;
    };
    sqlite3* db_ = nullptr;
    mutable std::optional<Totals> totals_cache_;
    mutable std::optional<Statistics> statistics_cache_;
    mutable std::string statistics_date_;
    mutable Id data_version_ = 0;
    void exec(const char* sql) const;
    void check_data_version() const;
    const Totals& totals() const;
    void adjust_totals(Id task, Microseconds duration, int completed);
};
std::string local_date(Microseconds wall_us);
std::string default_data_path();
}
