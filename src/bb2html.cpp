/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "bb2html.hpp"
#include "stream.hpp"
#include "utils.hpp"
#include "path.hpp"
#include "xml_parse.hpp"
#include "boostbook_chunker.hpp"
#include "html_printer.hpp"
#include "post_process.hpp"
#include "files.hpp"
#include <vector>
#include <cassert>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace quickbook {
    namespace fs = boost::filesystem;
}

namespace quickbook { namespace detail {
    struct html_state;
    struct html_gen;
    struct id_info;
    struct fragment_id_generator;

    typedef boost::unordered_map<string_view, id_info> ids_type;

    typedef void(*node_parser)(html_gen&, xml_element*);
    typedef boost::unordered_map<quickbook::string_view, node_parser> node_parsers_type;
    static node_parsers_type node_parsers;

    void generate_chunked_documentation(chunk*, ids_type const&, html_options const&);
    void generate_chunks(html_state&, chunk*);
    void generate_chunk_navigation(html_gen&, chunk*);
    void generate_inline_chunks(html_gen&, chunk*);
    void generate_chunk_body(html_gen&, chunk*);
    void generate_toc_html(html_gen& gen, chunk*);
    void generate_toc_subtree(html_gen& gen, chunk* page, chunk*, unsigned section_depth);
    void generate_toc_item_html(html_gen&, xml_element*);
    void generate_footnotes_html(html_gen&);
    void number_callouts(html_gen& gen, xml_element* x);
    void number_calloutlist_children(html_gen& gen, unsigned& count, xml_element* x);
    void generate_tree_html(html_gen&, xml_element*);
    void generate_children_html(html_gen&, xml_element*);
    void write_file(html_state&, std::string const& path, std::string const& content);
    std::string get_link_from_path(quickbook::string_view, quickbook::string_view);
    std::string relative_path_from_fs_paths(fs::path const&, fs::path const&);
    std::string relative_path_from_url_paths(quickbook::string_view, quickbook::string_view);

    ids_type get_id_paths(chunk* chunk);
    void get_id_paths_impl(ids_type&, chunk*);
    void get_id_paths_impl2(ids_type&, chunk*, xml_element*);

    void tag(html_gen& gen, quickbook::string_view name, xml_element* x);
    void tag_start_with_id(html_gen& gen, quickbook::string_view name, xml_element* x);
    void open_tag_with_id(html_gen& gen, quickbook::string_view name, xml_element* x);
    void tag_self_close(html_gen& gen, quickbook::string_view name, xml_element* x);
    void graphics_tag(html_gen& gen, quickbook::string_view path, quickbook::string_view fallback);

    struct id_info {
    private:
        chunk* chunk_;
        xml_element* element_;

    public:
        explicit id_info(chunk* c, xml_element* x)
            : chunk_(c), element_(x)
        {
            assert(c);
            assert(!x || x->get_attribute("id"));
        }

        std::string path() const {
            std::string p = chunk_->path_;

            if (element_) {
                p += '#';
                p += *element_->get_attribute("id");
            } else if (chunk_->inline_) {
                p += '#';
                p += chunk_->id_;
            }
            return p;
        }
    };

    struct html_state {
        ids_type const& ids;
        html_options const& options;
        unsigned int error_count;

        explicit html_state(ids_type const& ids_, html_options const& options_)
            : ids(ids_), options(options_), error_count(0) {}
    };

    struct callout_data {
        quickbook::string_view link_id;
        unsigned number;
    };

    struct html_gen {
        html_printer printer;
        html_state& state;
        string_view path;
        bool in_toc;
        boost::unordered_map<string_view, callout_data> callout_numbers;
        std::vector<xml_element*> footnotes;

        explicit html_gen(html_state& state_, string_view p) :
            printer(),
            state(state_),
            path(p),
            in_toc(false) {}

        html_gen(html_gen const& x) :
            printer(),
            state(x.state),
            path(x.path),
            in_toc(false) {}
    };

    int boostbook_to_html(quickbook::string_view source, html_options const& options)
    {
        xml_tree tree;
        try {
            tree = xml_parse(source);
        } catch (quickbook::detail::xml_parse_error e) {
            string_view source_view(source);
            file_position p = relative_position(source_view.begin(), e.pos);
            string_view::iterator line_start = e.pos - (p.column < 40 ? p.column - 1 : 39);
            string_view::iterator line_end = std::find(e.pos, source_view.end(), '\n');
            if (line_end - e.pos > 80) {
                line_end = e.pos + 80;
            }
            std::string indent;
            for (auto i = e.pos - line_start; i; --i) {
                indent += ' ';
            }
            ::quickbook::detail::outerr()
                << "converting boostbook at line "
                << p.line
                << " char "
                << p.column
                << ": "
                << e.message
                << "\n"
                << string_view(line_start, line_end - line_start)
                << "\n"
                << indent
                << "^"
                << "\n\n";

            return 1;
        }

        chunk_tree chunked = chunk_document(tree);
        // Overwrite paths depending on whether output is chunked or not.
        // Really want to do something better, e.g. incorporate many section chunks into their parent.
        chunked.root()->path_ = path_to_generic(options.home_path.filename());
        if (options.chunked_output) {
            inline_sections(chunked.root(), 0);

            // Create the root directory if necessary for chunked documentation.
            fs::path parent = options.home_path.parent_path();
            if (!parent.empty() && !fs::exists(parent)) {
                fs::create_directory(parent);
            }
        } else {
            inline_all(chunked.root());
        }
        ids_type ids = get_id_paths(chunked.root());
        html_state state(ids, options);
        if (chunked.root()) { generate_chunks(state, chunked.root()); }
        return state.error_count;
    }

    void generate_chunks(html_state& state, chunk* x) {
        html_gen gen(state, x->path_);
        gen.printer.html += "<!DOCTYPE html>\n";
        open_tag(gen.printer, "html");
        open_tag(gen.printer, "head");
        if (!state.options.css_path.empty()) {
            tag_start(gen.printer, "link");
            tag_attribute(gen.printer, "rel", "stylesheet");
            tag_attribute(gen.printer, "type", "text/css");
            tag_attribute(gen.printer, "href",
                relative_path_from_fs_paths(state.options.css_path,
                    state.options.home_path.parent_path() / x->path_));
            tag_end_self_close(gen.printer);
        }
        close_tag(gen.printer, "head");
        open_tag(gen.printer, "body");
        generate_chunk_navigation(gen, x);
        generate_chunk_body(gen, x);
        chunk* it = x->children();
        for (; it && it->inline_; it = it->next())
        {
            generate_inline_chunks(gen, it);
        }
        generate_footnotes_html(gen);
        close_tag(gen.printer, "body");
        close_tag(gen.printer, "html");
        write_file(state, x->path_, gen.printer.html);
        for (; it; it = it->next())
        {
            assert(!it->inline_);
            generate_chunks(state, it);
        }
    }

    void generate_chunk_navigation(html_gen& gen, chunk* x) {
        chunk* next = 0;
        for (chunk* it = x->children(); it; it = it->next()) {
            if (!it->inline_) {
                next = it;
                break;
            }
        }
        if (!next) { next = x->next(); }

        chunk* prev = x->prev();
        if (prev) {
            while(prev->children()) {
                for(prev = prev->children(); prev->next(); prev = prev->next()) {}
            }
        } else {
            prev = x->parent();
        }

        if (next || prev || x->parent()) {
            tag_start(gen.printer, "div");
            tag_attribute(gen.printer, "class", "spirit-nav");
            tag_end(gen.printer);
            if (prev) {
                tag_start(gen.printer, "a");
                tag_attribute(gen.printer, "href", get_link_from_path(prev->path_, x->path_));
                tag_attribute(gen.printer, "accesskey", "p");
                tag_end(gen.printer);
                graphics_tag(gen, "/prev.png", "prev");
                close_tag(gen.printer, "a");
                gen.printer.html += " ";
            }
            if (x->parent()) {
                tag_start(gen.printer, "a");
                tag_attribute(gen.printer, "href", get_link_from_path(x->parent()->path_, x->path_));
                tag_attribute(gen.printer, "accesskey", "u");
                tag_end(gen.printer);
                graphics_tag(gen, "/up.png", "up");
                close_tag(gen.printer, "a");
                gen.printer.html += " ";

                tag_start(gen.printer, "a");
                tag_attribute(gen.printer, "href", get_link_from_path("index.html", x->path_));
                tag_attribute(gen.printer, "accesskey", "h");
                tag_end(gen.printer);
                graphics_tag(gen, "/home.png", "home");
                close_tag(gen.printer, "a");
                if (next) { gen.printer.html += " "; }
            }
            if (next) {
                tag_start(gen.printer, "a");
                tag_attribute(gen.printer, "href", get_link_from_path(next->path_, x->path_));
                tag_attribute(gen.printer, "accesskey", "n");
                tag_end(gen.printer);
                graphics_tag(gen, "/next.png", "next");
                close_tag(gen.printer, "a");
            }
            close_tag(gen.printer, "div");
        }
    }

    void generate_inline_chunks(html_gen& gen, chunk* x) {
        tag_start(gen.printer, "div");
        tag_attribute(gen.printer, "id", x->id_);
        tag_end(gen.printer);
        generate_chunk_body(gen, x);
        for (chunk* it = x->children(); it; it = it->next())
        {
            assert(it->inline_);
            generate_inline_chunks(gen, it);
        }
        close_tag(gen.printer, "div");
    }

    void generate_chunk_body(html_gen& gen, chunk* x) {
        gen.callout_numbers.clear();

        number_callouts(gen, x->title_.root());
        number_callouts(gen, x->info_.root());
        number_callouts(gen, x->contents_.root());

        generate_tree_html(gen, x->title_.root());
        generate_tree_html(gen, x->info_.root());
        generate_toc_html(gen, x);
        generate_tree_html(gen, x->contents_.root());
    }

    void generate_toc_html(html_gen& gen, chunk* x) {
        if (x->children() && x->contents_.root()->name_ != "section") {
            tag_start(gen.printer, "div");
            tag_attribute(gen.printer, "class", "toc");
            tag_end(gen.printer);
            open_tag(gen.printer, "p");
            open_tag(gen.printer, "b");
            gen.printer.html += "Table of contents";
            close_tag(gen.printer, "b");
            close_tag(gen.printer, "p");
            generate_toc_subtree(gen, x, x, 1);
            close_tag(gen.printer, "div");
        }
    }

    void generate_toc_subtree(html_gen& gen, chunk* page, chunk* x, unsigned section_depth) {
        if (x != page && section_depth == 0) {
            bool has_non_section_child = false;
            for (chunk* it = x->children(); it; it = it->next()) {
                if (it->contents_.root()->name_ != "section") {
                    has_non_section_child = true;
                }
            }
            if (!has_non_section_child) {
                return;
            }
        }

        gen.printer.html += "<ul>";
        for (chunk* it = x->children(); it; it = it->next())
        {
            auto link = gen.state.ids.find(it->id_);
            gen.printer.html += "<li>";
            if (link != gen.state.ids.end()) {
                gen.printer.html += "<a href=\"";
                gen.printer.html += encode_string(get_link_from_path(link->second.path(), page->path_));
                gen.printer.html += "\">";
                generate_toc_item_html(gen, it->title_.root());
                gen.printer.html += "</a>";
            } else {
                generate_toc_item_html(gen, it->title_.root());
            }
            if (it->children()) {
                generate_toc_subtree(gen, page, it,
                    it->contents_.root()->name_ == "section" && section_depth > 0 ?
                        section_depth - 1 : section_depth);
            }
            gen.printer.html += "</li>";
        }
        gen.printer.html += "</ul>";
    }

    void generate_toc_item_html(html_gen& gen, xml_element* x) {
        if (x) {
            bool old = gen.in_toc;
            gen.in_toc = true;
            generate_children_html(gen, x);
            gen.in_toc = old;
        } else {
            gen.printer.html += "<i>Untitled</i>";
        }
    }

    void generate_footnotes_html(html_gen& gen) {
        if (!gen.footnotes.empty()) {
            tag_start(gen.printer, "div");
            tag_attribute(gen.printer, "class", "footnotes");
            tag_end(gen.printer);
            gen.printer.html += "<br/>";
            gen.printer.html += "<hr/>";
            for (std::vector<xml_element*>::iterator it = gen.footnotes.begin(); it != gen.footnotes.end(); ++it) {
                std::string footnote_label = *(*it)->get_attribute("(((footnote-label)))");
                tag_start(gen.printer, "div");
                tag_attribute(gen.printer, "id", "footnote-" + footnote_label);
                tag_attribute(gen.printer, "class", "footnote");
                tag_end(gen.printer);

                generate_children_html(gen, *it);
                close_tag(gen.printer, "div");
            }
            close_tag(gen.printer, "div");
        }
    }

    void number_callouts(html_gen& gen, xml_element* x) {
        if (!x) { return; }

        if (x->type_ == xml_element::element_node) {
            if (x->name_ == "calloutlist") {
                unsigned count = 0;
                number_calloutlist_children(gen, count, x);
            } else if (x->name_ == "co") {
                // TODO: Set id if missing?
                std::string* linkends = x->get_attribute("linkends");
                std::string* id = x->get_attribute("id");
                if (id && linkends) {
                    gen.callout_numbers[*linkends].link_id = *id;
                }
            }
        }
        for (xml_element* it = x->children(); it; it=it->next()) {
            number_callouts(gen, it);
        }
    }

    void number_calloutlist_children(html_gen& gen, unsigned& count, xml_element* x) {
        for (xml_element* it=x->children(); it; it=it->next()) {
            if (it->type_ == xml_element::element_node && it->name_ == "callout") {
                std::string* id = it->get_attribute("id");
                if (id) {
                    gen.callout_numbers[*id].number = ++count;
                }
            }
            number_calloutlist_children(gen, count, it);
        }
    }

    void generate_tree_html(html_gen& gen, xml_element* x) {
        if (!x) { return; }
        switch (x->type_) {
        case xml_element::element_text: {
            gen.printer.html += x->contents_;
            break;
        }
        case xml_element::element_html: {
            gen.printer.html += x->contents_;
            break;
        }
        case xml_element::element_node: {
            node_parsers_type::iterator parser = node_parsers.find(x->name_);
            if (parser != node_parsers.end()) {
                parser->second(gen, x);
            }
            else {
                quickbook::detail::out() << "Unsupported tag: " << x->name_ << std::endl;
                generate_children_html(gen, x);
            }
            break;
        }
        default:
            assert(false);
        }
    }

    void generate_children_html(html_gen& gen, xml_element* x) {
        for (xml_element* it = x->children(); it; it = it->next()) {
            generate_tree_html(gen, it);
        }
    }

    void write_file(html_state& state, std::string const& generic_path, std::string const& content) {
        fs::path path = state.options.home_path.parent_path() / generic_to_path(generic_path);
        std::string html = content;

        if (state.options.pretty_print)
        {
            try
            {
                html = post_process(html, -1, -1, true);
            }
            catch (quickbook::post_process_failure&)
            {
                ::quickbook::detail::outerr(path)
                    << "Post Processing Failed."
                    << std::endl;
                ++state.error_count;
            }
        }

        fs::path parent = path.parent_path();
        if (state.options.chunked_output && !parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }

        fs::ofstream fileout(path);

        if (fileout.fail()) {
            ::quickbook::detail::outerr(path)
                << "Error opening output file"
                << std::endl;
            ++state.error_count;
            return;
        }

        fileout << html;

        if (fileout.fail()) {
            ::quickbook::detail::outerr(path)
                << "Error writing to output file"
                << std::endl;
            ++state.error_count;
            return;
        }
    }

    std::string get_link_from_path(quickbook::string_view link, quickbook::string_view path) {
        if (boost::starts_with(link, "boost:")) {
            // TODO: Parameterize the boost location, so that it can use relative paths.
            string_iterator it = link.begin() + strlen("boost:");
            if (*it == '/') { ++it; }
            std::string result = "http://www.boost.org/doc/libs/release/";
            result.append(it, link.end());
            return result;
        }

        return relative_path_from_url_paths(link, path);
    }

    // Note: assume that base is a file, not a directory.
    std::string relative_path_from_fs_paths(fs::path const& p, fs::path const& base) {
        return path_to_generic(path_difference(base.parent_path(), p));
    }

    std::string relative_path_from_url_paths(quickbook::string_view path, quickbook::string_view base) {
        string_iterator path_it = path.begin();
        string_iterator base_it = base.begin();
        string_iterator path_diff_start = path_it;
        string_iterator base_diff_start = base_it;

        for(;path_it != path.end() && base_it != base.end() && *path_it == *base_it;
            ++path_it, ++base_it)
        {
            if (*path_it == '/') {
                path_diff_start = path_it + 1;
                base_diff_start = base_it + 1;
            } else if (*path_it == '#') {
                return std::string(path_it, path.end());
            }
        }

        if (base_it == base.end() && path_it != path.end() && *path_it == '#') {
            return std::string(path_it, path.end());
        }

        if (path_it == path.end() && (base_it == base.end() || *base_it == '#')) {
            return std::string("#");
        }

        auto up_count = std::count(base_diff_start,
            std::find(base_it, base.end(), '#'), '/');

        std::string result;
        for (int i = 0; i < up_count; ++i) { result += "../"; }
        result.append(path_diff_start, path.end());
        return result;
    }

    // get_id_paths

    ids_type get_id_paths(chunk* chunk) {
        ids_type ids;
        if (chunk) { get_id_paths_impl(ids, chunk); }
        return ids;
    }

    void get_id_paths_impl(ids_type& ids, chunk* c) {
        std::string p = c->path_;
        if (c->inline_) {
            p += '#';
            p += c->id_;
        }
        ids.emplace(c->id_, id_info(c, 0));

        get_id_paths_impl2(ids, c, c->title_.root());
        get_id_paths_impl2(ids, c, c->info_.root());
        get_id_paths_impl2(ids, c, c->contents_.root());
        for(chunk* i = c->children(); i; i = i->next())
        {
            get_id_paths_impl(ids, i);
        }
    }

    void get_id_paths_impl2(ids_type& ids, chunk* c, xml_element* node) {
        if (!node) { return; }
        std::string* id = node->get_attribute("id");
        if (id) {
            ids.emplace(*id, id_info(c, node));
        }
        for (xml_element* i = node->children(); i; i = i->next()) {
            get_id_paths_impl2(ids, c, i);
        }
    }

    void tag(html_gen& gen, quickbook::string_view name, xml_element* x) {
        open_tag_with_id(gen, name, x);
        generate_children_html(gen, x);
        close_tag(gen.printer, name);
    }

    void open_tag_with_id(html_gen& gen, quickbook::string_view name, xml_element* x) {
        tag_start_with_id(gen, name, x);
        tag_end(gen.printer);
    }

    void tag_self_close(html_gen& gen, quickbook::string_view name, xml_element* x) {
        tag_start_with_id(gen, name, x);
        tag_end_self_close(gen.printer);
    }

    void graphics_tag(html_gen& gen, quickbook::string_view path, quickbook::string_view fallback) {
        if (!gen.state.options.graphics_path.empty()) {
            std::string url = relative_path_from_fs_paths(
                gen.state.options.graphics_path / path.to_s(),
                gen.state.options.home_path.parent_path() / gen.path.to_s());
            tag_start(gen.printer, "img");
            tag_attribute(gen.printer, "src", url);
            tag_attribute(gen.printer, "alt", fallback);
            tag_end(gen.printer);
        } else {
            gen.printer.html.append(fallback.begin(), fallback.end());
        }
    }

    void tag_start_with_id(html_gen& gen, quickbook::string_view name, xml_element* x) {
        tag_start(gen.printer, name);
        if (!gen.in_toc) {
            std::string* id = x->get_attribute("id");
            if (id) {
                tag_attribute(gen.printer, "id", *id);
            }
        }
    }

    // Handle boostbook nodes

#define NODE_RULE(tag_name, gen, x) \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen&, xml_element*); \
    static struct BOOST_PP_CAT(register_parser_type_, tag_name) { \
        BOOST_PP_CAT(register_parser_type_, tag_name)() { \
            node_parsers.emplace(BOOST_PP_STRINGIZE(tag_name), \
                &BOOST_PP_CAT(parser_, tag_name)); \
        } \
    } BOOST_PP_CAT(register_parser_, tag_name); \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen& gen, xml_element* x)

#define NODE_MAP(tag_name, html_name) \
    NODE_RULE(tag_name, gen, x) { \
        tag(gen, BOOST_PP_STRINGIZE(html_name), x); \
    }

#define NODE_MAP_CLASS(tag_name, html_name, class_name) \
    NODE_RULE(tag_name, gen, x) { \
        tag_start_with_id(gen, BOOST_PP_STRINGIZE(html_name), x); \
        tag_attribute(gen.printer, "class", BOOST_PP_STRINGIZE(class_name)); \
        tag_end(gen.printer); \
        generate_children_html(gen, x); \
        close_tag(gen.printer, BOOST_PP_STRINGIZE(html_name)); \
    }

    // TODO: For some reason 'hr' generates an empty paragraph?
    NODE_MAP(para, p)
    NODE_MAP(simpara, div)
    NODE_MAP(orderedlist, ol)
    NODE_MAP(itemizedlist, ul)
    NODE_MAP(listitem, li)
    NODE_MAP(blockquote, blockquote)
    NODE_MAP(quote, q)
    NODE_MAP(code, code)
    NODE_MAP(macronname, code)
    NODE_MAP(classname, code)
    NODE_MAP_CLASS(programlisting, pre, programlisting)
    NODE_MAP(literal, tt)
    NODE_MAP(subscript, sub)
    NODE_MAP(superscript, sup)
    NODE_MAP(section, div)
    NODE_MAP(anchor, span)

    NODE_MAP(title, h3)

    NODE_MAP_CLASS(sidebar, div, sidebar) // TODO: sidebar role="blurb"
    NODE_MAP_CLASS(warning, div, warning)
    NODE_MAP_CLASS(caution, div, caution)
    NODE_MAP_CLASS(important, div, important)
    NODE_MAP_CLASS(note, div, note)
    NODE_MAP_CLASS(tip, div, tip)
    NODE_MAP_CLASS(replaceable, em, replaceable)

    NODE_RULE(sbr, gen, x) {
        if (!x->children()) {
            tag_self_close(gen, "br", x);
        } else {
            tag(gen, "br", x);
        }
    }

    NODE_RULE(bridgehead, gen, x) {
        std::string* renderas = x->get_attribute("renderas");
        char header[3] = "h3";
        if (renderas && renderas->size() == 5 && boost::starts_with(*renderas, "sect")) {
            char l = (*renderas)[4];
            if (l >= '1' && l <= '6') { header[1] = l; }
        }
        return tag(gen, header, x);
    }

    NODE_RULE(ulink, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("url");

        tag_start_with_id(gen, "a", x);
        if (value) { tag_attribute(gen.printer, "href", get_link_from_path(*value, gen.path)); }
        tag_end(gen.printer);
        generate_children_html(gen, x);
        close_tag(gen.printer, "a");
    }

    NODE_RULE(link, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("linkend");

        auto it = gen.state.ids.end();
        if (value) { it = gen.state.ids.find(*value); }

        tag_start_with_id(gen, "a", x);
        if (it != gen.state.ids.end()) {
            tag_attribute(gen.printer, "href", relative_path_from_url_paths(it->second.path(), gen.path));
        }
        tag_end(gen.printer);
        generate_children_html(gen, x);
        close_tag(gen.printer, "a");
    }

    NODE_RULE(phrase, gen, x) {
        std::string* value = x->get_attribute("role");

        tag_start_with_id(gen, "span", x);
        if (value) { tag_attribute(gen.printer, "class", *value); }
        tag_end(gen.printer);
        generate_children_html(gen, x);
        close_tag(gen.printer, "span");
    }

    NODE_RULE(emphasis, gen, x) {
        std::string* value = x->get_attribute("role");
        quickbook::string_view tag_name = "em";
        quickbook::string_view class_name = "";
        // TODO: case insensitive?
        if (value) {
            if (*value == "bold" || *value == "strong") {
                tag_name = "strong";
            } else {
                tag_name = "span";
                class_name = *value;
            }
        }
        tag_start_with_id(gen, tag_name, x);
        if (!class_name.empty()) { tag_attribute(gen.printer, "class", class_name); }
        tag_end(gen.printer);
        generate_children_html(gen, x);
        close_tag(gen.printer, tag_name);
    }

    NODE_RULE(inlinemediaobject, gen, x) {
        std::string* image;

        // Get image link
        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "imageobject") {
                for(xml_element* j = i->children(); j; j = j->next()) {
                    if (j->type_ == xml_element::element_node && j->name_ == "imagedata") {
                        image = j->get_attribute("fileref");
                        if (image) { break; }
                    }
                }
            }
        }

        std::string alt;
        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "textobject") {
                for(xml_element* j = i->children(); j; j = j->next()) {
                    if (j->type_ == xml_element::element_node && j->name_ == "phrase") {
                        std::string* role = j->get_attribute("role");
                        if (role && *role == "alt") {
                            html_gen gen2(gen);
                            generate_tree_html(gen2, j);
                            alt = gen2.printer.html;
                        }
                    }
                }
            }
        }
        // TODO: This was in the original php code, not sure why.
        if (alt.empty()) { alt = "[]"; }
        if (image) {
            tag_start(gen.printer, "span");
            tag_attribute(gen.printer, "class", "inlinemediaobject");
            tag_end(gen.printer);
            tag_start_with_id(gen, "img", x);
            tag_attribute(gen.printer, "src", get_link_from_path(*image, gen.path));
            tag_attribute(gen.printer, "alt", alt);
            tag_end_self_close(gen.printer);
            close_tag(gen.printer, "span");
        }
    }

    NODE_RULE(variablelist, gen, x) {
        typedef std::vector<std::pair<xml_element*, xml_element*> > items_type;
        items_type items;
        for (xml_element* i = x->children(); i; i = i->next()) {
            if (i && i->type_ == xml_element::element_node) {
                if (i->name_ == "title") {
                    // TODO: What to do with titles?
                    continue;
                } else if (i->name_ == "varlistentry") {
                    // TODO: What if i has an id?
                    xml_element* term = 0;
                    xml_element* listitem = 0;
                    for (xml_element* j = i->children(); j; j = j->next()) {
                        if (j && j->type_ == xml_element::element_node) {
                            if (j->name_ == "term") {
                                term = j;
                            } else if (j->name_ == "listitem") {
                                listitem = j;
                            }
                        }
                    }
                    if (term && listitem) {
                        items.push_back(std::make_pair(term, listitem));
                    }
                }
            }
        }

        if (!items.empty()) {
            open_tag_with_id(gen, "dl", x);
            for(items_type::iterator i = items.begin(); i != items.end(); ++i) {
                tag(gen, "dt", i->first);
                tag(gen, "dd", i->second);
            }
            close_tag(gen.printer, "dl");
        }
    }

    void write_table_rows(html_gen& gen, xml_element* x, char const* td_tag) {
        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "row") {
                open_tag_with_id(gen, "tr", i);
                for(xml_element* j = i->children(); j; j = j->next()) {
                    if (j->type_ == xml_element::element_node && j->name_ == "entry") {
                        open_tag_with_id(gen, td_tag, j);
                        generate_children_html(gen, j);
                        close_tag(gen.printer, td_tag);
                    }
                }
                close_tag(gen.printer, "tr");
            }
        }
    }

    void write_table(html_gen& gen, xml_element* x) {
        xml_element* title = 0;
        xml_element* tgroup = 0;
        xml_element* thead = 0;
        xml_element* tbody = 0;

        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "title") {
                title = i;
            }
            if (i->type_ == xml_element::element_node && i->name_ == "tgroup") {
                tgroup = i;
            }
        }

        if (!tgroup) { return; }

        for(xml_element* i = tgroup->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "thead") {
                thead = i;
            }
            if (i->type_ == xml_element::element_node && i->name_ == "tbody") {
                tbody = i;
            }
        }

        tag_start_with_id(gen, "div", x);
        tag_attribute(gen.printer, "class", x->name_);
        tag_end(gen.printer);
        open_tag(gen.printer, "table");
        if (title) { tag(gen, "caption", title); }
        if (thead) {
            open_tag(gen.printer, "thead");
            write_table_rows(gen, thead, "th");
            close_tag(gen.printer, "thead");
        }
        if (tbody) {
            open_tag(gen.printer, "tbody");
            write_table_rows(gen, tbody, "td");
            close_tag(gen.printer, "tbody");
        }
        close_tag(gen.printer, "table");
        close_tag(gen.printer, "div");
    }

    NODE_RULE(table, gen, x) { write_table(gen, x); }
    NODE_RULE(informaltable, gen, x) { write_table(gen, x); }

    NODE_MAP(calloutlist, div)

    NODE_RULE(callout, gen, x) {
        std::string* id = x->get_attribute("id");
        boost::unordered_map<string_view, callout_data>::const_iterator data = gen.callout_numbers.end();
        auto link = gen.state.ids.end();
        if (id) {
            data = gen.callout_numbers.find(*id);
        }
        if (data != gen.callout_numbers.end() && !data->second.link_id.empty()) {
            link = gen.state.ids.find(data->second.link_id);
        }

        open_tag_with_id(gen, "div", x);
        if (link != gen.state.ids.end()) {
            tag_start(gen.printer, "a");
            tag_attribute(gen.printer, "href", relative_path_from_url_paths(link->second.path(), gen.path));
            tag_end(gen.printer);
        }
        graphics_tag(gen, "/callouts/" +
                boost::lexical_cast<std::string>(data->second.number)
                + ".png",
                "(" + boost::lexical_cast<std::string>(data->second.number) + ")");
        if (link != gen.state.ids.end()) {
            close_tag(gen.printer, "a");
        }
        gen.printer.html += " ";
        generate_children_html(gen, x);
        close_tag(gen.printer, "div");
    }

    NODE_RULE(co, gen, x) {
        std::string* linkends = x->get_attribute("linkends");
        boost::unordered_map<string_view, callout_data>::const_iterator data = gen.callout_numbers.end();
        auto link = gen.state.ids.end();
        if (linkends) {
            data = gen.callout_numbers.find(*linkends);
            link = gen.state.ids.find(*linkends);
        }

        if (link != gen.state.ids.end()) {
            tag_start(gen.printer, "a");
            tag_attribute(gen.printer, "href", relative_path_from_url_paths(link->second.path(), gen.path));
            tag_end(gen.printer);
        }
        if (data != gen.callout_numbers.end()) {
            graphics_tag(gen, "/callouts/" +
                    boost::lexical_cast<std::string>(data->second.number)
                    + ".png",
                    "(" + boost::lexical_cast<std::string>(data->second.number) + ")");
        } else {
            gen.printer.html += "(0)";
        }
        if (link != gen.state.ids.end()) {
            close_tag(gen.printer, "a");
        }
    }

    NODE_RULE(footnote, gen, x) {
        // TODO: Better id generation....
        static int footnote_number = 0;
        ++footnote_number;
        std::string footnote_label = boost::lexical_cast<std::string>(footnote_number);

        tag_start_with_id(gen, "a", x);
        tag_attribute(gen.printer, "href", "#footnote-" + footnote_label);
        tag_end(gen.printer);
        tag_start(gen.printer, "sup");
        tag_attribute(gen.printer, "class", "footnote");
        tag_end(gen.printer);
        gen.printer.html += "[" + footnote_label + "]";
        close_tag(gen.printer, "sup");
        close_tag(gen.printer, "a");

        // Generate HTML to add to footnote.
        html_printer printer;
        tag_start(printer, "a");
        // TODO: Might not have an id.
        tag_attribute(printer, "href", "#" + *x->get_attribute("id"));
        tag_end(printer);
        tag_start(printer, "sup");
        tag_end(printer);
        printer.html += "[" + footnote_label + "]";
        close_tag(printer, "sup");
        close_tag(printer, "a");
        printer.html += ' ';
        xml_tree_builder builder;
        builder.add_element(xml_element::html_node(printer.html));

        // Find position to insert.
        auto pos = x->children();
        for (;pos && pos->type_ == xml_element::element_text; pos = pos->next() ) {
            if (pos->contents_.find_first_not_of("\t\n ") != std::string::npos) { break; }
        }
        if (!pos) {
            x->add_first_child(builder.release());
        } else switch (pos->type_) {
        case xml_element::element_node:
            // TODO: Check type of node? Recurse?
            pos->add_first_child(builder.release());
            break;
        default:
            pos->add_before(builder.release());
            break;
        }

        x->attributes_.push_back(std::make_pair("(((footnote-label)))", footnote_label));
        gen.footnotes.push_back(x);
    }
}}
