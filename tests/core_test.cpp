#include "store.hpp"
#include "timer.hpp"
#include "focus_timer.hpp"
#include <glib.h>
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
}
int main() {
    gchar* folder=g_dir_make_tmp("focus-test-XXXXXX",nullptr);
    if (!folder) return 1;
    std::filesystem::path path(folder); g_free(folder);
    try {
        timers(); calendar(); persistence((path/"focus.db").string()); resets((path/"resets.db").string());
        std::filesystem::remove_all(path);
        std::puts("Timer, local-day allocation, task, note and persistence checks passed.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAILED: %s\n",error.what());
        std::filesystem::remove_all(path);
        return 1;
    }
}
