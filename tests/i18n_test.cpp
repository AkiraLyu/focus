#include "i18n.hpp"
#include "store.hpp"
#include <iostream>
#include <stdexcept>

using namespace focus;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) {
    if (argc!=2) return 1;
    i18n::initialize();
    const bool chinese=std::string(argv[1])=="zh_CN";
    try {
        check(std::string(_("Quick Notes"))==(chinese?"速记":"Quick Notes"),"Notes navigation was not translated");
        check(std::string(_("Show Window"))==(chinese?"显示窗口":"Show Window"),"Tray text was not translated");
        check(std::string(_("Focus Complete"))==(chinese?"专注完成":"Focus Complete"),"Notification text was not translated");
        check(duration_text(second)==(chinese?"1 秒":"1 second"),"Singular duration is incorrect");
        check(duration_text(2*second)==(chinese?"2 秒":"2 seconds"),"Plural duration is incorrect");
        check(duration_text(60*second)==(chinese?"1 分钟":"1 minute"),"Singular minute is incorrect");
        check(duration_text(120*second)==(chinese?"2 分钟":"2 minutes"),"Plural minutes are incorrect");
        check(duration_text(3660*second)==(chinese?"1 小时 1 分钟":"1 h 1 min"),"Long duration is incorrect");
        check(i18n::format(P_("%d pomodoro completed","%d pomodoros completed",1),1)==
            (chinese?"已完成 1 个番茄":"1 pomodoro completed"),"Singular completion text is incorrect");
        check(i18n::format(P_("%d pomodoro completed","%d pomodoros completed",2),2)==
            (chinese?"已完成 2 个番茄":"2 pomodoros completed"),"Plural completion text is incorrect");
        Store store(":memory:");
        auto session=store.begin_session(0,Mode::Stopwatch);
        const auto wall=g_get_real_time();
        session.add(second,wall); store.save_session(session);
        const auto free=chinese?"自由专注":"Free Focus";
        check(store.statistics().tasks.front().title==free && store.sessions_on(local_date(wall)).front().task==free,
              "Free focus must use the selected language in statistics and details");
        const auto note=store.add_note();
        check(store.notes().front().title==(chinese?"未命名笔记":"Untitled Note"),"New note default title was not localized");
        store.save_note(note,"中文标题 / English", "# 原文 & source");
        store.add_task("原始任务 / Original task");
        check(store.notes().front().title=="中文标题 / English" && store.notes().front().body=="# 原文 & source" &&
            store.tasks().front().title=="原始任务 / Original task","Localization must preserve user content");
        std::cout<<"Translations, plurals and user-content preservation passed for "<<argv[1]<<".\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
