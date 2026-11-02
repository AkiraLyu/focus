#pragma once
#include "application.hpp"

namespace focus {
class NotesPage {
public:
    explicit NotesPage(Application& app);
    ~NotesPage();
    GtkWidget* widget() const { return root_; }
    void flush();
private:
    void reload();
    void select(Id id);
    void changed();
    void preview();
    void delete_current();
    Application& app_;
    GtkWidget* root_;
    GtkWidget* list_;
    GtkWidget* search_;
    GtkWidget* title_;
    GtkWidget* editor_;
    GtkWidget* preview_;
    GtkWidget* content_;
    GtkWidget* empty_;
    GtkWidget* editor_stack_;
    GtkWidget* status_;
    GtkTextBuffer* buffer_;
    Id current_ = 0;
    bool loading_ = false;
    bool dirty_ = false;
    guint save_id_ = 0;
};
}
