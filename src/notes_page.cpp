#include "notes_page.hpp"
#include "markdown.hpp"
#include "ui.hpp"
#include <algorithm>

namespace focus {
NotesPage::NotesPage(Application& app) : app_(app) {
    root_ = ui::box(GTK_ORIENTATION_VERTICAL);
    auto* split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_resize_start_child(GTK_PANED(split),false);
    gtk_paned_set_shrink_start_child(GTK_PANED(split),false);
    gtk_paned_set_shrink_end_child(GTK_PANED(split),false);
    gtk_paned_set_position(GTK_PANED(split),270);
    gtk_widget_set_vexpand(split,true);
    auto* sidebar = ui::box(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_size_request(sidebar,230,-1);
    gtk_widget_add_css_class(sidebar,"note-sidebar");
    auto* controls = ui::box(GTK_ORIENTATION_VERTICAL,10); ui::margins(controls,16);
    auto* create = ui::button("新建笔记", "suggested-action");
    gtk_box_append(GTK_BOX(controls),create);
    search_ = gtk_search_entry_new();
    g_object_set(search_,"placeholder-text","搜索笔记",nullptr);
    gtk_box_append(GTK_BOX(controls),search_);
    gtk_box_append(GTK_BOX(sidebar),controls);
    list_ = gtk_list_box_new();
    gtk_widget_add_css_class(list_,"navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_),GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list_),true);
    auto* no_results = ui::label("暂无笔记", "dim-label"); ui::margins(no_results,20);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list_),no_results);
    gtk_box_append(GTK_BOX(sidebar),ui::scrolled(list_));
    gtk_paned_set_start_child(GTK_PANED(split),sidebar);
    auto* right = gtk_stack_new();
    gtk_widget_set_hexpand(right,true);
    content_ = ui::box(GTK_ORIENTATION_VERTICAL);
    auto* title_bar = ui::box(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_widget_add_css_class(title_bar,"notes-toolbar");
    title_ = gtk_entry_new();
    gtk_widget_add_css_class(title_,"flat"); gtk_widget_add_css_class(title_,"note-title");
    gtk_entry_set_placeholder_text(GTK_ENTRY(title_),"笔记标题");
    gtk_entry_set_max_length(GTK_ENTRY(title_),160);
    gtk_widget_set_hexpand(title_,true);
    gtk_box_append(GTK_BOX(title_bar),title_);
    auto* remove = ui::icon_button("user-trash-symbolic","删除笔记");
    gtk_widget_add_css_class(remove,"flat");
    gtk_box_append(GTK_BOX(title_bar),remove);
    gtk_box_append(GTK_BOX(content_),title_bar);
    editor_stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(editor_stack_,true);
    editor_ = gtk_text_view_new();
    gtk_widget_add_css_class(editor_,"note-editor");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor_),GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(editor_),12);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(editor_),12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(editor_),12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(editor_),24);
    buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_));
    gtk_text_buffer_set_enable_undo(buffer_,true);
    preview_ = gtk_text_view_new();
    gtk_widget_add_css_class(preview_,"markdown-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(preview_),false);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview_),false);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(preview_),GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(preview_),5);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(preview_),12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(preview_),12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(preview_),24);
    gtk_stack_add_titled(GTK_STACK(editor_stack_),ui::scrolled(editor_),"edit","编辑");
    gtk_stack_add_titled(GTK_STACK(editor_stack_),ui::scrolled(preview_),"preview","预览");
    auto* bar = ui::box(GTK_ORIENTATION_HORIZONTAL,12); ui::margins(bar,12);
    auto* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher),GTK_STACK(editor_stack_));
    gtk_box_append(GTK_BOX(bar),switcher);
    status_ = ui::label("", "dim-label");
    gtk_widget_set_visible(status_,false);
    gtk_widget_set_hexpand(status_,true); gtk_label_set_xalign(GTK_LABEL(status_),1);
    gtk_box_append(GTK_BOX(bar),status_);
    gtk_box_append(GTK_BOX(content_),bar);
    gtk_box_append(GTK_BOX(content_),editor_stack_);
    empty_ = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty_),"document-edit-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(empty_),"暂无笔记");
    gtk_stack_add_named(GTK_STACK(right),empty_,"empty");
    gtk_stack_add_named(GTK_STACK(right),content_,"content");
    gtk_paned_set_end_child(GTK_PANED(split),right);
    gtk_box_append(GTK_BOX(root_),split);
    ui::clicked(create,[this] {
        flush();
        auto id=app_.store().add_note();
        gtk_editable_set_text(GTK_EDITABLE(search_),"");
        reload(); select(id);
        gtk_stack_set_visible_child_name(GTK_STACK(editor_stack_),"edit");
        gtk_widget_grab_focus(title_);
        gtk_editable_select_region(GTK_EDITABLE(title_),0,-1);
    });
    ui::clicked(remove,[this]{delete_current();});
    ui::on(G_OBJECT(search_),"search-changed",root_,[this]{reload();});
    ui::on(G_OBJECT(title_),"changed",root_,[this]{changed();});
    ui::on(G_OBJECT(buffer_),"changed",root_,[this]{changed();});
    ui::notify(G_OBJECT(editor_stack_),"visible-child-name",root_,[this] {
        if (std::string(gtk_stack_get_visible_child_name(GTK_STACK(editor_stack_)))=="preview") { flush(); preview(); }
    });
    g_signal_connect(list_,"row-activated",G_CALLBACK(+[](GtkListBox*,GtkListBoxRow* row,gpointer data) {
        auto& self=*static_cast<NotesPage*>(data);
        auto* id=static_cast<Id*>(g_object_get_data(G_OBJECT(row),"note-id"));
        if (id) ui::guard(self.root_,[&]{self.select(*id);});
    }),this);
    reload();
    auto notes=app_.store().notes();
    if (!notes.empty()) select(notes.front().id);
}
NotesPage::~NotesPage() { if (save_id_) g_source_remove(save_id_); }
void NotesPage::reload() {
    auto notes=app_.store().notes();
    auto* folded=g_utf8_casefold(gtk_editable_get_text(GTK_EDITABLE(search_)),-1);
    std::string search(folded); g_free(folded);
    ui::clear_list(list_);
    for (const auto& note : notes) {
        auto* haystack=g_utf8_casefold((note.title+"\n"+note.body).c_str(),-1);
        const bool matches=std::string(haystack).find(search)!=std::string::npos; g_free(haystack);
        if (!matches) continue;
        auto* row=gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(row),"note-id",new Id(note.id),+[](gpointer id){delete static_cast<Id*>(id);});
        auto* content=ui::box(GTK_ORIENTATION_VERTICAL,6);
        gtk_widget_add_css_class(content,"note-row");
        auto* title=ui::label(note.title,"heading");
        gtk_label_set_wrap(GTK_LABEL(title),false); gtk_label_set_ellipsize(GTK_LABEL(title),PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(content),title);
        auto summary=note.body.empty()?std::string("空白笔记"):note.body;
        std::replace(summary.begin(),summary.end(),'\n',' ');
        auto* body=ui::label(summary,"dim-label");
        gtk_label_set_wrap(GTK_LABEL(body),false); gtk_label_set_ellipsize(GTK_LABEL(body),PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(body),24);
        gtk_box_append(GTK_BOX(content),body);
        auto* date=g_date_time_new_from_unix_local(note.updated/second);
        auto* formatted=g_date_time_format(date,"%m 月 %d 日 · %H:%M");
        auto* timestamp=ui::label(formatted,"caption"); gtk_widget_add_css_class(timestamp,"dim-label");
        gtk_box_append(GTK_BOX(content),timestamp);
        g_free(formatted); g_date_time_unref(date);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),content);
        gtk_list_box_append(GTK_LIST_BOX(list_),row);
        if (note.id==current_) gtk_list_box_select_row(GTK_LIST_BOX(list_),GTK_LIST_BOX_ROW(row));
    }
}
void NotesPage::select(Id id) {
    if (id==current_) return;
    flush();
    auto notes=app_.store().notes();
    auto found=std::find_if(notes.begin(),notes.end(),[id](const Note& note){return note.id==id;});
    if (found==notes.end()) return;
    loading_=true;
    current_=id;
    gtk_editable_set_text(GTK_EDITABLE(title_),found->title.c_str());
    gtk_text_buffer_begin_irreversible_action(buffer_);
    gtk_text_buffer_set_text(buffer_,found->body.c_str(),static_cast<int>(found->body.size()));
    gtk_text_buffer_end_irreversible_action(buffer_);
    GtkTextIter begin; gtk_text_buffer_get_start_iter(buffer_,&begin); gtk_text_buffer_place_cursor(buffer_,&begin);
    gtk_widget_set_visible(status_,false);
    loading_=false;
    gtk_stack_set_visible_child_name(GTK_STACK(gtk_widget_get_parent(content_)),"content");
    preview(); reload();
}
void NotesPage::changed() {
    if (loading_ || !current_) return;
    dirty_=true;
    gtk_label_set_text(GTK_LABEL(status_),"正在保存…");
    gtk_widget_set_visible(status_,true);
    if (save_id_) g_source_remove(save_id_);
    save_id_=g_timeout_add(600,+[](gpointer data)->gboolean {
        auto& self=*static_cast<NotesPage*>(data);
        self.save_id_=0;
        try { self.flush(); }
        catch (const std::exception& e) {
            gtk_label_set_text(GTK_LABEL(self.status_),"保存失败，内容仍在编辑器中");
            ui::error(self.root_,e.what());
        }
        return G_SOURCE_REMOVE;
    },this);
}
void NotesPage::flush() {
    if (save_id_) {g_source_remove(save_id_); save_id_=0;}
    if (!dirty_ || !current_) return;
    app_.store().save_note(current_,ui::trimmed(gtk_editable_get_text(GTK_EDITABLE(title_))),ui::text(buffer_));
    dirty_=false;
    gtk_widget_set_visible(status_,false);
    reload();
}
void NotesPage::preview() { render_markdown(gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview_)),ui::text(buffer_)); }
void NotesPage::delete_current() {
    if (!current_) return;
    flush();
    auto* dialog=adw_alert_dialog_new("删除这条笔记？","笔记将从本机永久删除。");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),"cancel","取消","delete","删除",nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),"delete",ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog),"cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog),"cancel");
    const Id id=current_;
    ui::response(dialog,[this,id](const std::string& response) {
        if (response!="delete") return;
        app_.store().delete_note(id); current_=0;
        auto notes=app_.store().notes();
        if (!notes.empty()) select(notes.front().id);
        else gtk_stack_set_visible_child_name(GTK_STACK(gtk_widget_get_parent(content_)),"empty");
        reload(); app_.toast("笔记已删除");
    });
    adw_dialog_present(dialog,app_.window());
}
}
