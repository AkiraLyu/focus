#include "store.hpp"
#include "timer.hpp"
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
    check(timer.phase()==Phase::ShortBreak && !timer.running(),"First round must wait at short break");
    check(timer.advance(300*second).focus_us==0,"Next phase must wait for explicit start");
    timer.start(300*second);
    auto rest=timer.advance(360*second);
    check(rest.focus_us==0 && !rest.completed && timer.phase()==Phase::Focus,"Breaks must not contribute focus time");
    timer.start(360*second); timer.advance(420*second);
    check(timer.phase()==Phase::LongBreak && timer.completed_rounds()==2,"Configured round count must trigger a long break");
    timer.reset();
    timer.set_mode(Mode::Stopwatch);
    timer.start(0);
    check(timer.pause(3'601*second).focus_us==3'601*second,"Stopwatch must run beyond a countdown duration");
    check(clock_text(timer.display_time())=="01:00:01","Long stopwatch time must include hours");
    timer.start(4'000*second);
    check(timer.pause(4'002*second).focus_us==2*second,"Resume must exclude paused interval");
    timer.reset();
    timer.set_settings({50,10,30,3});
    check(timer.duration()==50*60*second,"Settings must update the next duration");
    rejects([&]{timer.set_settings({0,5,15,4});});
    Timer fractional;
    fractional.set_mode(Mode::Stopwatch);
    fractional.start(0);
    Microseconds total=0;
    for (int i=1;i<=10;++i) { total+=fractional.pause(i*100'000).focus_us; fractional.start(i*100'000); }
    check(total==second,"Repeated pauses must preserve sub-second focus time");
}
void calendar() {
    g_setenv("TZ","Asia/Tokyo",true); tzset();
    Session session;
    session.add(20*second,local_time(2026,11,3,0,0,10));
    check(session.days["2026-11-02"]==10*second && session.days["2026-11-03"]==10*second,"Midnight sessions must split across local days");
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
}
int main() {
    gchar* folder=g_dir_make_tmp("focus-test-XXXXXX",nullptr);
    if (!folder) return 1;
    std::filesystem::path path(folder); g_free(folder);
    try {
        timers(); calendar(); persistence((path/"focus.db").string());
        std::filesystem::remove_all(path);
        std::puts("Timer, local-day allocation, task, note and persistence checks passed.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAILED: %s\n",error.what());
        std::filesystem::remove_all(path);
        return 1;
    }
}
