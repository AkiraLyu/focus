#include "store.hpp"
#include "i18n.hpp"
#include <glib.h>
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace focus {
namespace {
constexpr const char* session_schema = " (id INTEGER PRIMARY KEY AUTOINCREMENT, task_id INTEGER REFERENCES tasks(id), mode INTEGER NOT NULL, total INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0, started_at INTEGER, ended_at INTEGER);";
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) fail();
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int index, Id value) { if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) fail(); }
    void bind(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) fail();
    }
    bool next() {
        const int result = sqlite3_step(stmt_);
        if (result == SQLITE_ROW) return true;
        if (result != SQLITE_DONE) fail();
        return false;
    }
    Id number(int column) const { return sqlite3_column_int64(stmt_, column); }
    std::string text(int column) const {
        auto* value = sqlite3_column_text(stmt_, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
private:
    [[noreturn]] void fail() const { throw std::runtime_error(sqlite3_errmsg(db_)); }
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};
using Date = std::unique_ptr<GDateTime, decltype(&g_date_time_unref)>;
Date datetime(Microseconds us) { return Date(g_date_time_new_from_unix_local(us / second), g_date_time_unref); }
std::string date_string(GDateTime* date) {
    gchar* value = g_date_time_format(date, "%Y-%m-%d");
    std::string result(value);
    g_free(value);
    return result;
}
void validate_title(const std::string& title) {
    if (title.empty() || title.find_first_not_of(" \t\n\r") == std::string::npos)
        throw std::invalid_argument(_("Enter a title."));
}
}
std::string local_date(Microseconds wall_us) { return date_string(datetime(wall_us).get()); }
std::string default_data_path() {
    auto folder = std::filesystem::path(g_get_user_data_dir()) / "focus";
    std::filesystem::create_directories(folder);
    return (folder / "focus.db").string();
}
void Session::add(Microseconds amount, Microseconds wall_end) {
    if (amount <= 0) return;
    if (total == 0) started_at = wall_end - amount;
    ended_at = wall_end;
    total += amount;
    auto cursor = wall_end - amount;
    while (cursor < wall_end) {
        auto date = datetime(cursor);
        Date midnight(g_date_time_new_local(g_date_time_get_year(date.get()), g_date_time_get_month(date.get()),
            g_date_time_get_day_of_month(date.get()), 0, 0, 0), g_date_time_unref);
        Date next(g_date_time_add_days(midnight.get(), 1), g_date_time_unref);
        auto end = std::min<Microseconds>(wall_end, g_date_time_to_unix(next.get()) * second);
        days[date_string(date.get())] += end - cursor;
        cursor = end;
    }
}
Store::Store(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(error);
    }
    try {
        sqlite3_busy_timeout(db_, 3000);
        exec("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
             "CREATE TABLE IF NOT EXISTS settings (id INTEGER PRIMARY KEY CHECK(id=1), focus INTEGER NOT NULL, short_break INTEGER NOT NULL, long_break INTEGER NOT NULL, rounds INTEGER NOT NULL);"
             "INSERT OR IGNORE INTO settings VALUES(1,25,5,15,4);"
             "CREATE TABLE IF NOT EXISTS window_state (id INTEGER PRIMARY KEY CHECK(id=1), width INTEGER NOT NULL CHECK(width>0), height INTEGER NOT NULL CHECK(height>0), maximized INTEGER NOT NULL CHECK(maximized IN (0,1)));"
             "CREATE TABLE IF NOT EXISTS tasks (id INTEGER PRIMARY KEY, title TEXT NOT NULL, done INTEGER NOT NULL DEFAULT 0);"
             "CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, title TEXT NOT NULL, body TEXT NOT NULL, updated INTEGER NOT NULL);");
        exec((std::string("CREATE TABLE IF NOT EXISTS sessions")+session_schema).c_str());
        exec("CREATE TABLE IF NOT EXISTS activity (session_id INTEGER NOT NULL REFERENCES sessions(id), day TEXT NOT NULL, duration INTEGER NOT NULL, PRIMARY KEY(session_id,day));"
             "CREATE INDEX IF NOT EXISTS activity_day ON activity(day);");
        bool has_timestamps = false;
        {
            Statement columns(db_, "PRAGMA table_info(sessions)");
            while (columns.next()) if (columns.text(1) == "started_at") has_timestamps = true;
        }
        if (!has_timestamps) {
            // Rebuild once to preserve old records and give deletable records stable IDs.
            // Unknown historical timestamps stay NULL; durations cannot reconstruct pauses.
            exec("PRAGMA foreign_keys=OFF; BEGIN IMMEDIATE;");
            try {
                exec((std::string("CREATE TABLE sessions_new")+session_schema).c_str());
                exec("INSERT INTO sessions_new(id,task_id,mode,total,completed) SELECT id,task_id,mode,total,completed FROM sessions;"
                     "DROP TABLE sessions; ALTER TABLE sessions_new RENAME TO sessions; COMMIT;");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
            exec("PRAGMA foreign_keys=ON;");
        }
        exec("CREATE INDEX IF NOT EXISTS sessions_task_totals ON sessions(task_id,total,completed);");
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Store::~Store() { sqlite3_close(db_); }
void Store::exec(const char* sql) const {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
}
void Store::check_data_version() const {
    Statement query(db_,"PRAGMA data_version");
    query.next();
    if (data_version_==query.number(0)) return;
    data_version_=query.number(0);
    totals_cache_.reset();
    statistics_cache_.reset();
}
const Store::Totals& Store::totals() const {
    if (!totals_cache_) {
        Totals result;
        Statement query(db_,"SELECT task_id,SUM(total),SUM(completed) FROM sessions GROUP BY task_id");
        while (query.next()) {
            result.all+=query.number(1);
            result.completed+=static_cast<int>(query.number(2));
            if (query.number(1)) result.tasks[query.number(0)]=query.number(1);
        }
        totals_cache_=std::move(result);
    }
    return *totals_cache_;
}
void Store::adjust_totals(Id task, Microseconds duration, int completed) {
    if (!totals_cache_) return;
    try {
        if (duration) {
            auto& value=totals_cache_->tasks[task];
            value+=duration;
            if (!value) totals_cache_->tasks.erase(task);
        }
        totals_cache_->all+=duration;
        totals_cache_->completed+=completed;
    } catch (...) { totals_cache_.reset(); throw; }
}
Settings Store::settings() const {
    Statement query(db_, "SELECT focus,short_break,long_break,rounds FROM settings WHERE id=1");
    query.next();
    Settings settings{static_cast<int>(query.number(0)), static_cast<int>(query.number(1)), static_cast<int>(query.number(2)), static_cast<int>(query.number(3))};
    if (!settings.valid()) throw std::runtime_error(_("The saved timer settings are invalid."));
    return settings;
}
void Store::save_settings(const Settings& settings) {
    if (!settings.valid()) throw std::invalid_argument(_("Timer durations are outside the allowed range."));
    Statement query(db_, "UPDATE settings SET focus=?,short_break=?,long_break=?,rounds=? WHERE id=1");
    query.bind(1, settings.focus_minutes); query.bind(2, settings.short_break_minutes);
    query.bind(3, settings.long_break_minutes); query.bind(4, settings.rounds); query.next();
}
WindowState Store::window_state() const {
    Statement query(db_, "SELECT width,height,maximized FROM window_state WHERE id=1");
    if (!query.next()) return {};
    return {static_cast<int>(query.number(0)),static_cast<int>(query.number(1)),query.number(2)!=0};
}
void Store::save_window_state(const WindowState& state) {
    if (state.width<=0 || state.height<=0) throw std::invalid_argument(_("Window dimensions must be greater than zero."));
    Statement query(db_, "INSERT INTO window_state VALUES(1,?,?,?) ON CONFLICT(id) DO UPDATE SET width=excluded.width,height=excluded.height,maximized=excluded.maximized");
    query.bind(1,state.width); query.bind(2,state.height); query.bind(3,state.maximized); query.next();
}
std::vector<Task> Store::tasks() const {
    check_data_version();
    const auto& durations=totals().tasks;
    Statement query(db_, "SELECT id,title,done FROM tasks ORDER BY done,id DESC");
    std::vector<Task> result;
    while (query.next()) {
        const auto found=durations.find(query.number(0));
        result.push_back({query.number(0),query.text(1),query.number(2)!=0,found==durations.end()?0:found->second});
    }
    return result;
}
Id Store::add_task(const std::string& title) {
    validate_title(title);
    Statement query(db_, "INSERT INTO tasks(title) VALUES(?)"); query.bind(1,title); query.next();
    statistics_cache_.reset();
    return sqlite3_last_insert_rowid(db_);
}
void Store::update_task(Id id, const std::string& title, bool done) {
    validate_title(title);
    Statement query(db_, "UPDATE tasks SET title=?,done=? WHERE id=?");
    query.bind(1,title); query.bind(2,done); query.bind(3,id); query.next();
    statistics_cache_.reset();
}
std::vector<Note> Store::notes() const {
    Statement query(db_, "SELECT id,title,body,updated FROM notes ORDER BY updated DESC,id DESC");
    std::vector<Note> result;
    while (query.next()) result.push_back({query.number(0),query.text(1),query.text(2),query.number(3)});
    return result;
}
Id Store::add_note() {
    Statement query(db_, "INSERT INTO notes(title,body,updated) VALUES(?,'',?)");
    query.bind(1,std::string(_("Untitled Note")));
    query.bind(2,g_get_real_time()); query.next(); return sqlite3_last_insert_rowid(db_);
}
void Store::save_note(Id id, const std::string& title, const std::string& body) {
    Statement query(db_, "UPDATE notes SET title=?,body=?,updated=? WHERE id=?");
    query.bind(1,title.empty()?_("Untitled Note"):title); query.bind(2,body);
    query.bind(3,g_get_real_time()); query.bind(4,id); query.next();
}
void Store::delete_note(Id id) {
    Statement query(db_, "DELETE FROM notes WHERE id=?"); query.bind(1,id); query.next();
}
Session Store::begin_session(Id task, Mode mode) {
    Statement query(db_, "INSERT INTO sessions(task_id,mode) VALUES(NULLIF(?,0),?)");
    query.bind(1,task); query.bind(2,static_cast<Id>(mode)); query.next();
    return {sqlite3_last_insert_rowid(db_),task,mode,false,0,{}};
}
void Store::save_session(const Session& session) {
    exec("BEGIN IMMEDIATE");
    Id task=0;
    Microseconds duration_delta=0;
    int completed_delta=0;
    try {
        check_data_version();
        if (totals_cache_) {
            Statement previous(db_,"SELECT task_id,total,completed FROM sessions WHERE id=?");
            previous.bind(1,session.id);
            if (!previous.next()) throw std::runtime_error(_("This focus record has been deleted."));
            task=previous.number(0);
            duration_delta=session.total-previous.number(1);
            completed_delta=static_cast<int>(session.completed)-static_cast<int>(previous.number(2));
        }
        Statement update(db_, "UPDATE sessions SET total=?,completed=?,started_at=NULLIF(?,0),ended_at=NULLIF(?,0) WHERE id=?");
        update.bind(1,session.total); update.bind(2,session.completed);
        update.bind(3,session.started_at); update.bind(4,session.ended_at); update.bind(5,session.id); update.next();
        for (const auto& [day,amount] : session.days) {
            Statement query(db_, "INSERT INTO activity VALUES(?,?,?) ON CONFLICT(session_id,day) DO UPDATE SET duration=excluded.duration");
            query.bind(1,session.id); query.bind(2,day); query.bind(3,amount); query.next();
        }
        exec("COMMIT");
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    statistics_cache_.reset();
    adjust_totals(task,duration_delta,completed_delta);
}
void Store::delete_sessions(const std::vector<Id>& ids) {
    if (ids.empty()) return;
    exec("BEGIN IMMEDIATE");
    std::vector<std::tuple<Id,Microseconds,int>> removed;
    try {
        check_data_version();
        for (const auto id : ids) {
            if (totals_cache_) {
                Statement previous(db_,"SELECT task_id,total,completed FROM sessions WHERE id=?");
                previous.bind(1,id);
                if (previous.next()) removed.emplace_back(previous.number(0),previous.number(1),static_cast<int>(previous.number(2)));
            }
            Statement activity(db_, "DELETE FROM activity WHERE session_id=?");
            activity.bind(1,id); activity.next();
            Statement session(db_, "DELETE FROM sessions WHERE id=?");
            session.bind(1,id); session.next();
        }
        exec("COMMIT");
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    statistics_cache_.reset();
    for (const auto& [task,duration,completed] : removed) adjust_totals(task,-duration,-completed);
}
std::vector<SessionRecord> Store::sessions_on(const std::string& day) const {
    Statement query(db_, "SELECT s.id,COALESCE(t.title,?),s.mode,s.completed,s.total,a.duration,s.started_at,s.ended_at "
                         "FROM activity a JOIN sessions s ON s.id=a.session_id LEFT JOIN tasks t ON t.id=s.task_id "
                         "WHERE a.day=? AND a.duration>0 ORDER BY s.started_at DESC,s.id DESC");
    query.bind(1,std::string(_("Free Focus"))); query.bind(2,day);
    std::vector<SessionRecord> result;
    while (query.next()) result.push_back({query.number(0),query.text(1),static_cast<Mode>(query.number(2)),
        query.number(3)!=0,query.number(4),query.number(5),query.number(6),query.number(7)});
    return result;
}
Statistics Store::statistics() const {
    check_data_version();
    auto today = datetime(g_get_real_time());
    const auto current_date=date_string(today.get());
    if (statistics_cache_ && statistics_date_==current_date) return *statistics_cache_;
    Statistics result;
    for (int offset = -6; offset <= 0; ++offset) {
        Date day(g_date_time_add_days(today.get(), offset), g_date_time_unref);
        auto date = date_string(day.get());
        Statement query(db_, "SELECT COALESCE(SUM(duration),0) FROM activity WHERE day=?");
        query.bind(1,date); query.next();
        result.days.push_back({date,query.number(0)});
        result.week += query.number(0);
        if (offset == 0) result.today = query.number(0);
    }
    const auto& summary=totals();
    result.all=summary.all;
    result.completed=summary.completed;
    Statement tasks(db_,"SELECT id,title FROM tasks ORDER BY id");
    while (tasks.next()) {
        const auto found=summary.tasks.find(tasks.number(0));
        if (found!=summary.tasks.end() && found->second>0) result.tasks.push_back({tasks.text(1),found->second});
    }
    if (const auto free=summary.tasks.find(0); free!=summary.tasks.end() && free->second>0)
        result.tasks.push_back({_("Free Focus"),free->second});
    std::stable_sort(result.tasks.begin(),result.tasks.end(),[](const TaskTotal& left,const TaskTotal& right){return left.total>right.total;});
    statistics_cache_=result;
    statistics_date_=current_date;
    return result;
}
}
