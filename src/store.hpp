#pragma once
#include "timer.hpp"
#include <map>
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
    void add(Microseconds amount, Microseconds wall_end);
};
struct DayTotal { std::string date; Microseconds total; };
struct TaskTotal { std::string title; Microseconds total; };
struct Statistics {
    Microseconds today = 0;
    Microseconds week = 0;
    Microseconds all = 0;
    int completed = 0;
    std::vector<DayTotal> days;
    std::vector<TaskTotal> tasks;
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
    Statistics statistics() const;
private:
    sqlite3* db_ = nullptr;
    void exec(const char* sql) const;
};
std::string local_date(Microseconds wall_us);
std::string default_data_path();
}
