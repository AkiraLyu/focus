#include "store.hpp"
#include "timer.hpp"
#include "focus_timer.hpp"
#include <glib.h>
#include <sqlite3.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace focus;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected=false;
    try { action(); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Invalid operation was accepted");
}
Microseconds local_time(int year,int month,int day,int hour,int minute,int seconds) {
    auto* date=g_date_time_new_local(year,month,day,hour,minute,seconds);
    auto value=g_date_time_to_unix(date)*second;
    g_date_time_unref(date);
    return value;
}
void timers() {
    Timer timer({1,1,2,2});
    timer.start(10*second);
    check(timer.advance(20*second).focus_us==10*second,"Active time must count");
    check(timer.pause(25*second).focus_us==5*second,"Pause must capture partial time");
    check(timer.advance(100*second).focus_us==0,"Paused time must not count");
    rejects([&]{timer.set_mode(Mode::Stopwatch);});
    rejects([&]{timer.set_settings({2,1,1,2});});
    timer.start(100*second);
    auto complete=timer.advance(200*second);
    check(complete.focus_us==45*second && complete.completed,"Countdown must cap delayed callbacks at its deadline");
    check(complete.finished_ago_us==55*second,"Delayed completion must preserve the actual end time");
    check(timer.phase()==Phase::ShortBreak && !timer.running(),"First round must wait at short break");
    check(timer.advance(300*second).focus_us==0,"Next phase must wait for explicit start");
    timer.start(300*second);
    auto rest=timer.advance(360*second);
    check(rest.focus_us==0 && !rest.completed && timer.phase()==Phase::Focus,"Breaks must not contribute focus time");
    timer.start(360*second); timer.advance(420*second);
    check(timer.phase()==Phase::LongBreak && timer.completed_rounds()==2,"Configured round count must trigger a long break");
    timer.finish();
    timer.set_mode(Mode::Stopwatch);
    timer.start(0);
    check(timer.pause(3'601*second).focus_us==3'601*second,"Stopwatch must run beyond a countdown duration");
    check(clock_text(timer.display_time())=="01:00:01","Long stopwatch time must include hours");
    timer.start(4'000*second);
    check(timer.pause(4'002*second).focus_us==2*second,"Resume must exclude paused interval");
    timer.finish();
    timer.set_settings({50,10,30,3});
    check(timer.duration()==50*60*second,"Settings must update the next duration");
    rejects([&]{timer.set_settings({0,5,15,4});});
    Timer fractional;
    fractional.set_mode(Mode::Stopwatch);
    fractional.start(0);
    Microseconds total=0;
    for (int i=1;i<=10;++i) { total+=fractional.pause(i*100'000).focus_us; fractional.start(i*100'000); }
    check(total==second,"Repeated pauses must preserve sub-second focus time");
    check(timer_now()>0,"The suspend-aware timer clock must be available");
}
void calendar() {
    g_setenv("TZ","Asia/Tokyo",true); tzset();
    Session session;
    session.add(20*second,local_time(2026,11,3,0,0,10));
    check(session.days["2026-11-02"]==10*second && session.days["2026-11-03"]==10*second,"Midnight sessions must split across local days");
    Timer delayed({1,1,2,2});
    delayed.start(0);
    const auto completed=delayed.advance(5*60*second);
    Session delayed_session;
    delayed_session.add(completed.focus_us,local_time(2026,11,3,0,3,30)-completed.finished_ago_us);
    check(delayed_session.days.size()==1 && delayed_session.days["2026-11-02"]==60*second,
          "A countdown completed before midnight must remain on that day after a delayed wakeup");
    g_setenv("TZ","America/New_York",true); tzset();
    Session spring;
    const auto start=local_time(2026,3,8,0,0,0), end=local_time(2026,3,9,0,0,0);
    spring.add(end-start,end);
    check(spring.days["2026-03-08"]==23*3600*second,"Spring DST day must contain 23 hours");
    Session fall;
    const auto fall_start=local_time(2026,11,1,0,0,0),fall_end=local_time(2026,11,2,0,0,0);
    fall.add(fall_end-fall_start,fall_end);
    check(fall.days["2026-11-01"]==25*3600*second,"Autumn DST day must contain 25 hours");
    g_setenv("TZ","Asia/Tokyo",true); tzset();
}
void persistence(const std::string& path) {
    Id task,note;
    {
        Store store(path);
        const auto initial_window=store.window_state();
        check(initial_window.width>0 && initial_window.height>0 && !initial_window.maximized,"A new profile must have a normal window size");
        store.save_window_state({900,680,false});
        store.save_window_state({900,680,true});
        rejects([&]{store.save_window_state({0,680,false});});
        store.save_settings({50,10,30,3});
        task=store.add_task("学习 C++ & GTK <4>");
        note=store.add_note();
        store.save_note(note,"想法 ' 一", "# 中文\n\n**重点**\n- [ ] 待办\n<script>原文</script>");
        auto session=store.begin_session(task,Mode::Pomodoro);
        session.add(25*60*second,g_get_real_time());
        session.completed=true;
        store.save_session(session);
        store.save_session(session);
        auto stopwatch=store.begin_session(task,Mode::Stopwatch);
        stopwatch.add(10*60*second,g_get_real_time());
        store.save_session(stopwatch);
        auto stats=store.statistics();
        check(stats.all==35*60*second && stats.completed==1,"Checkpoint retries must not double count focus or completed rounds");
        check(stats.week==stats.all && stats.days.size()==7,"Recent focus must appear in the seven-day summary");
        check(stats.tasks.size()==1 && stats.tasks.front().total==stats.all,"Both modes must contribute to task totals");
        store.update_task(task,"重命名后的任务",true);
        check(store.tasks().front().done && store.tasks().front().total==stats.all,"Completing tasks must preserve their statistics");
        rejects([&]{store.add_task("  ");});
    }
    {
        Store reopened(path);
        const auto window=reopened.window_state();
        check(window.width==900 && window.height==680 && window.maximized,"Normal size and maximized state must survive restart");
        reopened.save_window_state({960,720,false});
        check(reopened.settings().focus_minutes==50,"Settings must survive restart");
        check(reopened.tasks().front().id==task && reopened.tasks().front().title=="重命名后的任务","Tasks must survive restart");
        check(reopened.notes().front().body.find("**重点**")!=std::string::npos,"Markdown source must survive restart");
        reopened.save_note(note,"更新", "new content");
        check(reopened.notes().front().body=="new content","Existing notes must be editable");
        reopened.delete_note(note);
        check(reopened.notes().empty(),"Deleted notes must be removed");
        check(reopened.statistics().all==35*60*second,"Statistics must survive restart");
    }
    {
        Store reopened(path);
        const auto window=reopened.window_state();
        check(window.width==960 && window.height==720 && !window.maximized,"Restored and resized window state must replace the previous state");
    }
}
void resets(const std::string& path) {
    Microseconds expected;
    {
        Store store(path);
        store.save_settings({1,1,2,2});
        const auto first=store.add_task("第一项任务"), second_task=store.add_task("第二项任务");
        auto history=store.begin_session(first,Mode::Stopwatch);
        history.add(90*second,g_get_real_time()); store.save_session(history);
        FocusTimer focus(store);
        Microseconds now=0;
        const auto wall=g_get_real_time()-3600*second;
        const auto run=[&](Id task,int seconds,bool pause=false) {
            focus.start(task,now);
            now+=seconds*second;
            return focus.advance(now,wall+now,pause);
        };
        const auto totals=[&](int seconds,int completed) {
            const auto stats=store.statistics();
            check(stats.all==seconds*second && stats.week==stats.all && stats.completed==completed,
                  "Reset must keep duration, daily totals and completed counts consistent");
            Microseconds tasks=0;
            for (const auto& task:store.tasks()) tasks+=task.total;
            check(tasks==stats.all,"Reset must update task totals");
        };
        run(first,15,true); totals(105,0);
        focus.reset(ResetScope::Current); totals(90,0);
        check(!focus.timer().engaged() && focus.timer().display_time()==60*second,"Current reset must return focus to its full duration and wait");
        run(first,60); totals(150,1);
        run(first,10);
        focus.reset(ResetScope::Current);
        check(focus.timer().phase()==Phase::ShortBreak && focus.timer().completed_rounds()==1 && focus.timer().display_time()==60*second,
              "Resetting a short break must preserve the phase and completed focus rounds");
        totals(150,1);
        run(first,60);
        run(second_task,20);
        focus.reset(ResetScope::Current); totals(150,1);
        check(focus.timer().completed_rounds()==1,"Current reset must preserve cycle progress");
        focus.reset(ResetScope::Cycle); totals(90,0);
        check(focus.timer().phase()==Phase::Focus && focus.timer().completed_rounds()==0 && !focus.timer().engaged(),
              "Cycle reset must discard earlier rounds and return to the first focus phase");
        run(first,60); run(first,60); run(second_task,60);
        run(first,15);
        focus.reset(ResetScope::Current);
        check(focus.timer().phase()==Phase::LongBreak && focus.timer().display_time()==120*second && focus.timer().completed_rounds()==2,
              "Resetting a long break must preserve the current cycle");
        totals(210,2);
        focus.reset(ResetScope::Cycle); totals(90,0);
        run(first,60); run(first,60); run(second_task,60);
        check(run(first,120).cycle_completed,"Finishing a long break must complete the cycle");
        check(focus.timer().completed_rounds()==0,"A new cycle must start with no completed rounds");
        run(first,5);
        focus.reset(ResetScope::Cycle); totals(210,2);
        focus.advance(now+second,wall+now+second); focus.flush(); totals(210,2);
        run(first,7,true); focus.finish(); totals(217,2);
        focus.reset(ResetScope::Cycle); totals(210,2);
        run(first,9,true); focus.finish();
        focus.set_mode(Mode::Stopwatch);
        run(second_task,20);
        focus.reset(ResetScope::Current); totals(219,2);
        check(focus.timer().display_time()==0 && !focus.timer().engaged(),"Stopwatch reset must clear elapsed time and wait");
        rejects([&]{focus.reset(ResetScope::Cycle);});
        focus.set_mode(Mode::Pomodoro);
        focus.reset(ResetScope::Cycle); totals(219,2);
        run(first,60);
        focus.set_settings({2,1,2,2});
        focus.reset(ResetScope::Cycle); totals(279,3);
        expected=store.statistics().all;
    }
    Store reopened(path);
    check(reopened.statistics().all==expected && reopened.statistics().completed==3,"Discarded records must remain absent after reopening the database");
}
void history(const std::string& path) {
    const auto start=local_time(2026,11,2,23,59,50);
    Id task, crossing_id;
    {
        Store store(path);
        task=store.add_task("跨天任务");
        auto crossing=store.begin_session(task,Mode::Pomodoro);
        crossing_id=crossing.id;
        crossing.add(20*second,start+20*second);
        // A pause extends the wall-clock interval but contributes no focus duration.
        crossing.add(10*second,start+120*second);
        crossing.completed=true;
        store.save_session(crossing);
        store.save_session(crossing);
        auto later=store.begin_session(0,Mode::Stopwatch);
        later.add(5*second,start+200*second);
        store.save_session(later);
        const auto previous=store.sessions_on("2026-11-02");
        const auto next=store.sessions_on("2026-11-03");
        check(previous.size()==1 && next.size()==2,"Daily history must include every session that contributed to the selected day");
        const auto& record=previous.front();
        check(record.id==crossing.id && record.task=="跨天任务" && record.completed && record.mode==Mode::Pomodoro,
              "History must retain task, mode and completion information");
        check(record.total==30*second && record.day_total==10*second && next.back().day_total==20*second,
              "History must distinguish whole-session duration from the selected day's portion");
        check(record.started_at==start && record.ended_at==start+120*second,
              "Session timestamps must span pauses without inflating focus duration");
        check(next.front().id==later.id && next.front().task=="自由专注" && next.front().mode==Mode::Stopwatch,
              "History must be newest first and include sessions without a task");
        check(store.sessions_on("2026-11-04").empty(),"An empty day must have no records");
    }
    {
        Store store(path);
        check(store.sessions_on("2026-11-02").front().started_at==start,"Session timestamps must survive reopening");
        FocusTimer focus(store);
        focus.delete_session(crossing_id);
        check(store.sessions_on("2026-11-02").empty() && store.sessions_on("2026-11-03").size()==1,
              "Deleting a session must remove its contributions from every day");
        check(store.statistics().all==5*second && store.statistics().completed==0 && store.tasks().front().total==0,
              "Deletion must update total duration, completed count and task totals together");
        focus.start(task,0);
        focus.advance(3*second,start+300*second,true);
        const auto active=focus.active_session();
        focus.delete_session(active);
        focus.advance(4*second,start+301*second); focus.flush();
        check(!focus.timer().engaged() && store.statistics().all==5*second,
              "Deleting a paused current session must reset the timer and never restore the record");
        focus.start(task,5*second);
        focus.advance(6*second,start+302*second);
        check(focus.active_session()!=active,"Deleted record IDs must never identify newly created sessions");
        focus.delete_session(active);
        check(focus.timer().running(),"A stale deletion request must leave a newer timer untouched");
        focus.delete_session(focus.active_session());
        focus.flush();
        check(!focus.timer().engaged() && store.statistics().all==5*second,"Deleting a running session must discard its accumulated time");
        focus.start(task,10*second);
        const auto delayed=focus.advance(10*second+30*60*second,start+30*60*second);
        check(delayed.completed,"Test focus must complete");
        const auto records=store.sessions_on("2026-11-03");
        const auto found=std::find_if(records.begin(),records.end(),[](const SessionRecord& record){return record.completed;});
        check(found!=records.end(),"Completed focus must appear in the history");
        const auto completed=*found;
        check(completed.started_at==start && completed.ended_at==start+25*60*second,
              "Late completion callbacks must retain the actual start and finish times");
        focus.delete_session(completed.id);
        check(focus.timer().phase()==Phase::ShortBreak,"Deleting past focus must preserve the current break");
        auto kept=store.begin_session(task,Mode::Stopwatch);
        kept.add(7*second,start+2000*second); store.save_session(kept);
        focus.reset(ResetScope::Cycle);
        check(store.statistics().all==12*second,"Cycle reset must not delete unrelated records created after a history deletion");
    }
    Store reopened(path);
    check(reopened.statistics().all==12*second && reopened.sessions_on("2026-11-02").empty(),
          "Deleted history and corrected totals must survive restarting");
}
void history_upgrade(const std::string& path) {
    sqlite3* db=nullptr;
    check(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"Could not create historical profile");
    const auto result=sqlite3_exec(db,
        "CREATE TABLE tasks(id INTEGER PRIMARY KEY,title TEXT NOT NULL,done INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO tasks VALUES(1,'历史任务',0);"
        "CREATE TABLE sessions(id INTEGER PRIMARY KEY,task_id INTEGER REFERENCES tasks(id),mode INTEGER NOT NULL,total INTEGER NOT NULL DEFAULT 0,completed INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO sessions VALUES(1,1,0,60000000,1);"
        "CREATE TABLE activity(session_id INTEGER NOT NULL REFERENCES sessions(id),day TEXT NOT NULL,duration INTEGER NOT NULL,PRIMARY KEY(session_id,day));"
        "INSERT INTO activity VALUES(1,'2026-11-02',60000000);",nullptr,nullptr,nullptr);
    sqlite3_close(db);
    check(result==SQLITE_OK,"Could not populate historical profile");
    {
        Store store(path);
        const auto records=store.sessions_on("2026-11-02");
        check(records.size()==1 && records.front().total==60*second && records.front().task=="历史任务",
              "Upgrading must preserve existing records and task relationships");
        check(records.front().started_at==0 && records.front().ended_at==0,
              "Historical timestamps must remain unknown instead of being fabricated");
        check(store.statistics().all==60*second && store.statistics().completed==1,"Upgrading must preserve existing statistics");
        store.delete_sessions({records.front().id});
        auto fresh=store.begin_session(1,Mode::Stopwatch);
        check(fresh.id!=records.front().id,"Upgraded profiles must also use stable record IDs");
        fresh.add(3*second,local_time(2026,11,3,12,0,0)); store.save_session(fresh);
    }
    Store reopened(path);
    check(reopened.statistics().all==3*second && reopened.sessions_on("2026-11-03").front().ended_at!=0,
          "Upgraded profiles must support saving, deleting and reopening records");
}
void cached_statistics(const std::string& path) {
    Store store(path);
    check(store.statistics().all==0 && store.tasks().empty(),"A fresh profile must have empty statistics");
    const auto task=store.add_task("缓存任务");
    const auto wall=g_get_real_time();
    auto session=store.begin_session(task,Mode::Pomodoro);
    session.add(15*second,wall);
    store.save_session(session);
    check(store.statistics().all==15*second && store.tasks().front().total==15*second,
          "Cached totals must include newly saved focus");
    store.save_session(session);
    check(store.statistics().all==15*second,"Saving a checkpoint twice must not inflate cached totals");
    session.add(10*second,wall+10*second); session.completed=true;
    store.save_session(session);
    check(store.statistics().all==25*second && store.statistics().completed==1,
          "Updating an existing session must update cached duration and completed count");
    store.update_task(task,"已改名",true);
    check(store.statistics().tasks.front().title=="已改名" && store.tasks().front().done,
          "Task metadata must update without losing cached focus duration");
    auto free=store.begin_session(0,Mode::Stopwatch);
    free.add(5*second,wall); store.save_session(free);
    check(store.statistics().all==30*second && store.statistics().tasks.size()==2,
          "Free focus must be included alongside named tasks");
    store.delete_sessions({session.id,session.id,99999});
    check(store.statistics().all==5*second && store.statistics().completed==0 && store.tasks().front().total==0,
          "Batch deletion must subtract each record exactly once from cached totals");

    Store writer(path);
    auto external=writer.begin_session(task,Mode::Stopwatch);
    external.add(8*second,wall); writer.save_session(external);
    check(store.statistics().all==13*second && store.tasks().front().total==8*second,
          "Statistics must observe committed changes from another database connection");
    writer.update_task(task,"外部改名",false);
    check(store.statistics().tasks.front().title=="外部改名" && !store.tasks().front().done,
          "External task changes must invalidate cached labels");
    writer.delete_sessions({external.id});
    check(store.statistics().all==5*second && store.tasks().front().total==0,
          "External deletion must invalidate previously cached totals");

    const auto database_command=[&](const std::string& sql) {
        sqlite3* db=nullptr;
        check(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"Could not open failure fixture connection");
        const auto result=sqlite3_exec(db,sql.c_str(),nullptr,nullptr,nullptr);
        sqlite3_close(db);
        check(result==SQLITE_OK,"Could not configure database failure fixture");
    };
    database_command("CREATE TRIGGER reject_activity BEFORE INSERT ON activity BEGIN SELECT RAISE(ABORT,'test write failure'); END;");
    const auto before=store.statistics();
    free.add(4*second,wall+4*second);
    rejects([&]{store.save_session(free);});
    check(store.statistics()==before && writer.statistics()==before,
          "A failed save must preserve both cached and persisted statistics");
    database_command("DROP TRIGGER reject_activity;");
    store.statistics();
    store.save_session(free);
    check(store.statistics().all==9*second,"Retrying a failed save must apply its duration exactly once");

    auto extra=store.begin_session(task,Mode::Stopwatch);
    extra.add(2*second,wall); store.save_session(extra);
    database_command("CREATE TRIGGER reject_deletion BEFORE DELETE ON sessions WHEN OLD.id="+std::to_string(extra.id)+
                     " BEGIN SELECT RAISE(ABORT,'test delete failure'); END;");
    const auto before_delete=store.statistics();
    rejects([&]{store.delete_sessions({free.id,extra.id});});
    check(store.statistics()==before_delete && writer.statistics()==before_delete && store.tasks().front().total==2*second,
          "A partially failed batch deletion must preserve all cached and persisted statistics");
    database_command("DROP TRIGGER reject_deletion;");
    store.statistics();
    store.delete_sessions({free.id,extra.id});
    check(store.statistics().all==0 && store.statistics().tasks.empty(),"Deleting the last records must clear cached task distribution");

    g_setenv("TZ","Etc/GMT-14",true); tzset();
    const auto first_date=local_date(g_get_real_time());
    check(store.statistics().days.back().date==first_date,"Statistics must follow the current local date");
    g_setenv("TZ","Etc/GMT+12",true); tzset();
    const auto next_date=local_date(g_get_real_time());
    check(first_date!=next_date && store.statistics().days.back().date==next_date,
          "The seven-day window must update when the local date changes without a database write");
    g_setenv("TZ","Asia/Tokyo",true); tzset();
}
}
int main() {
    gchar* folder=g_dir_make_tmp("focus-test-XXXXXX",nullptr);
    if (!folder) return 1;
    std::filesystem::path path(folder); g_free(folder);
    try {
        timers(); calendar(); persistence((path/"focus.db").string()); resets((path/"resets.db").string());
        history((path/"history.db").string()); history_upgrade((path/"upgrade.db").string());
        cached_statistics((path/"cache.db").string());
        std::filesystem::remove_all(path);
        std::puts("Timer, local-day allocation, task, note and persistence checks passed.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAILED: %s\n",error.what());
        std::filesystem::remove_all(path);
        return 1;
    }
}
