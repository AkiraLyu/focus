#include "markdown.hpp"
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <memory>
#include <vector>

namespace focus {
namespace {
class Renderer {
public:
    explicit Renderer(GtkTextBuffer* buffer) : buffer_(buffer) {
        gtk_text_buffer_set_text(buffer_,"",0);
        if (!gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer_),"strong")) {
            gtk_text_buffer_create_tag(buffer_,"strong","weight",PANGO_WEIGHT_BOLD,nullptr);
            gtk_text_buffer_create_tag(buffer_,"emphasis","style",PANGO_STYLE_ITALIC,nullptr);
            gtk_text_buffer_create_tag(buffer_,"code","family","monospace","scale",0.93,nullptr);
            gtk_text_buffer_create_tag(buffer_,"quote","left-margin",22,"style",PANGO_STYLE_ITALIC,nullptr);
            gtk_text_buffer_create_tag(buffer_,"link","underline",PANGO_UNDERLINE_SINGLE,nullptr);
            gtk_text_buffer_create_tag(buffer_,"strike","strikethrough",true,nullptr);
            gtk_text_buffer_create_tag(buffer_,"paragraph","pixels-below-lines",10,nullptr);
            gtk_text_buffer_create_tag(buffer_,"list-item","pixels-below-lines",3,nullptr);
            for (int level=1; level<=6; ++level) {
                const auto name="h"+std::to_string(level);
                gtk_text_buffer_create_tag(buffer_,name.c_str(),"weight",PANGO_WEIGHT_BOLD,
                    "scale",level==1?1.8:level==2?1.45:1.15,"pixels-above-lines",10,"pixels-below-lines",6,nullptr);
            }
        }
    }
    void node(cmark_node* node, int depth=0) {
        const auto type=cmark_node_get_type(node);
        const int start=gtk_text_buffer_get_char_count(buffer_);
        switch(type) {
        case CMARK_NODE_TEXT: append(cmark_node_get_literal(node)); break;
        case CMARK_NODE_CODE: append(cmark_node_get_literal(node)); tag("code",start); break;
        case CMARK_NODE_CODE_BLOCK: append(cmark_node_get_literal(node)); tag("code",start); append("\n"); break;
        case CMARK_NODE_HTML_BLOCK: case CMARK_NODE_HTML_INLINE:
            append(cmark_node_get_literal(node)); if (type==CMARK_NODE_HTML_BLOCK) append("\n"); break;
        case CMARK_NODE_SOFTBREAK: append(" "); break;
        case CMARK_NODE_LINEBREAK: append("\n"); break;
        case CMARK_NODE_THEMATIC_BREAK: append("────────────────────\n\n"); break;
        case CMARK_NODE_HEADING:
            children(node,depth); append("\n"); tag(("h"+std::to_string(cmark_node_get_heading_level(node))).c_str(),start); break;
        case CMARK_NODE_PARAGRAPH:
            children(node,depth); append("\n");
            tag(cmark_node_get_type(cmark_node_parent(node))==CMARK_NODE_ITEM?"list-item":"paragraph",start); break;
        case CMARK_NODE_STRONG: children(node,depth); tag("strong",start); break;
        case CMARK_NODE_EMPH: children(node,depth); tag("emphasis",start); break;
        case CMARK_NODE_BLOCK_QUOTE: children(node,depth); tag("quote",start); break;
        case CMARK_NODE_LIST: {
            int number=cmark_node_get_list_start(node);
            for (auto* item=cmark_node_first_child(node); item; item=cmark_node_next(item)) {
                append(std::string(static_cast<size_t>(depth)*3,' '));
                const auto* kind=cmark_node_get_type_string(item);
                if (std::string(kind)=="tasklist") append(cmark_gfm_extensions_get_tasklist_item_checked(item)?"☑  ":"☐  ");
                else append(cmark_node_get_list_type(node)==CMARK_ORDERED_LIST?std::to_string(number++)+".  ":"•  ");
                children(item,depth+1);
            }
            append("\n"); break;
        }
        case CMARK_NODE_LINK:
            children(node,depth); tag("link",start); append(" ("+std::string(cmark_node_get_url(node))+")"); break;
        case CMARK_NODE_IMAGE:
            append("[图片："); children(node,depth); append("] ("+std::string(cmark_node_get_url(node))+")"); break;
        default: {
            const std::string kind=cmark_node_get_type_string(node);
            if (kind=="table") {
                children(node,depth); tag("code",start); append("\n");
            } else if (kind=="table_header" || kind=="table_row") {
                children(node,depth); append("\n");
            } else if (kind=="table_cell") {
                children(node,depth); if (cmark_node_next(node)) append("  │  ");
            } else {
                children(node,depth);
                if (kind=="strikethrough") tag("strike",start);
            }
        }
        }
    }
private:
    void children(cmark_node* node, int depth) {
        for (auto* child=cmark_node_first_child(node); child; child=cmark_node_next(child)) this->node(child,depth);
    }
    void append(const std::string& text) {
        const auto start=gtk_text_buffer_get_char_count(buffer_);
        GtkTextIter end; gtk_text_buffer_get_end_iter(buffer_,&end);
        gtk_text_buffer_insert(buffer_,&end,text.data(),static_cast<int>(text.size()));
        GtkTextIter begin; gtk_text_buffer_get_iter_at_offset(buffer_,&begin,start);
        gtk_text_buffer_remove_all_tags(buffer_,&begin,&end);
    }
    void tag(const char* name,int start) {
        GtkTextIter begin,end;
        gtk_text_buffer_get_iter_at_offset(buffer_,&begin,start); gtk_text_buffer_get_end_iter(buffer_,&end);
        gtk_text_buffer_apply_tag_by_name(buffer_,name,&begin,&end);
    }
    GtkTextBuffer* buffer_;
};
}
void render_markdown(GtkTextBuffer* buffer, const std::string& markdown) {
    cmark_gfm_core_extensions_ensure_registered();
    auto parser=std::unique_ptr<cmark_parser,decltype(&cmark_parser_free)>(cmark_parser_new(CMARK_OPT_DEFAULT),cmark_parser_free);
    for (const char* name : {"strikethrough","table","tasklist","autolink"})
        cmark_parser_attach_syntax_extension(parser.get(),cmark_find_syntax_extension(name));
    cmark_parser_feed(parser.get(),markdown.data(),markdown.size());
    auto document=std::unique_ptr<cmark_node,decltype(&cmark_node_free)>(cmark_parser_finish(parser.get()),cmark_node_free);
    Renderer(buffer).node(document.get());
}
}
