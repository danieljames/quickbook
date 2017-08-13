/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "bb2html.hpp"
#include <cassert>
#include <vector>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include "boostbook_chunker.hpp"
#include "path.hpp"
#include "stream.hpp"
#include "utils.hpp"
#include "xml_parse.hpp"

namespace quickbook
{
    namespace fs = boost::filesystem;
}

namespace quickbook
{
    namespace detail
    {
        struct chunk_writer;
        struct html_gen;

        typedef boost::unordered_map<string_view, std::string> id_paths_type;

        typedef void (*node_parser)(html_gen&, xml_element*);
        typedef boost::unordered_map<quickbook::string_view, node_parser>
            node_parsers_type;
        static node_parsers_type node_parsers;

        void generate_chunked_documentation(
            chunk* root,
            id_paths_type const&,
            fs::path const& root_path,
            html_options const&);
        void generate_chunks(chunk_writer&, chunk*);
        void generate_inline_chunks(html_gen& gen, chunk* root);
        void generate_chunk_html(html_gen&, chunk*);
        void generate_toc_html(html_gen& gen, chunk* root);
        void generate_toc_subtree(
            html_gen& gen, chunk* page, chunk* chunk_root);
        void generate_toc_item_html(html_gen&, xml_element*);
        void generate_footnotes_html(html_gen&);
        void number_callouts(html_gen& gen, xml_element* x);
        void number_callouts2(html_gen& gen, unsigned& count, xml_element* x);
        void generate_tree_html(html_gen&, xml_element*);
        void generate_children_html(html_gen&, xml_element*);
        void write_file(fs::path const& path, std::string const& content);
        std::string relative_path_from(
            quickbook::string_view, quickbook::string_view);

        id_paths_type get_id_paths(chunk* chunk);
        void get_id_paths_impl(id_paths_type&, chunk*);
        void get_id_paths_impl2(id_paths_type&, string_view, xml_element*);

        void tag(html_gen& gen, quickbook::string_view name, xml_element* x);
        void tag_attribute(
            html_gen& gen,
            quickbook::string_view name,
            quickbook::string_view value);
        void tag_start(html_gen& gen, quickbook::string_view name);
        void tag_start_with_id(
            html_gen& gen, quickbook::string_view name, xml_element* x);
        void tag_end(html_gen& gen);
        void tag_end_self_close(html_gen& gen);
        void tag_attribute(
            html_gen& gen,
            quickbook::string_view name,
            quickbook::string_view value);
        void open_tag(html_gen& gen, quickbook::string_view name);
        void open_tag_with_id(
            html_gen& gen, quickbook::string_view name, xml_element* x);
        void close_tag(html_gen& gen, quickbook::string_view name);
        void tag_self_close(
            html_gen& gen, quickbook::string_view name, xml_element* x);
        void graphics_tag(
            html_gen& gen,
            quickbook::string_view path,
            quickbook::string_view fallback);

        struct chunk_writer
        {
            fs::path const& root_path;
            id_paths_type const& id_paths;
            fs::path const& css_path;
            fs::path const& graphics_path;

            explicit chunk_writer(
                fs::path const& r,
                id_paths_type const& ip,
                html_options const& options)
                : root_path(r)
                , id_paths(ip)
                , css_path(options.css_path)
                , graphics_path(options.graphics_path)
            {
            }

            void write_file(
                std::string const& generic_path, std::string const& content)
            {
                fs::path path = root_path / generic_to_path(generic_path);
                fs::create_directories(path.parent_path());
                quickbook::detail::write_file(path, content);
            }
        };

        struct callout_data
        {
            quickbook::string_view link_id;
            unsigned number;
        };

        struct html_gen
        {
            id_paths_type const& id_paths;
            std::string graphics_path;
            string_view path;
            std::string html;
            bool in_toc;
            boost::unordered_map<string_view, callout_data> callout_numbers;
            std::vector<xml_element*> footnotes;

            html_gen(html_gen const& x)
                : id_paths(x.id_paths)
                , graphics_path(x.graphics_path)
                , path(x.path)
                , in_toc(false)
            {
            }
            explicit html_gen(
                id_paths_type const& ip,
                std::string const& graphics_path,
                string_view p)
                : id_paths(ip), graphics_path(graphics_path), path(p)
            {
            }
        };

        int boostbook_to_html(
            quickbook::string_view source, html_options const& options)
        {
            fs::path root_dir;
            std::string root_filename;
            if (options.chunked_output) {
                root_dir = options.output_path;
                root_filename = "index.html";
            }
            else {
                root_dir = options.output_path.parent_path();
                root_filename = path_to_generic(options.output_path.filename());
            }

            xml_tree tree = xml_parse(source);
            chunk_tree chunked = chunk_document(tree);
            // Overwrite paths depending on whether output is chunked or not.
            // Really want to do something better, e.g. incorporate many section
            // chunks into their parent.
            chunked.root()->path_ = root_filename;
            if (options.chunked_output) {
                inline_sections(chunked.root(), 0);
            }
            else {
                inline_chunks(chunked.root());
            }
            id_paths_type id_paths = get_id_paths(chunked.root());
            generate_chunked_documentation(
                chunked.root(), id_paths, root_dir, options);
            return 0;
        }

        void generate_chunked_documentation(
            chunk* chunked,
            id_paths_type const& id_paths,
            fs::path const& path,
            html_options const& options)
        {
            fs::create_directory(path);
            chunk_writer writer(path, id_paths, options);
            if (chunked) {
                generate_chunks(writer, chunked);
            }
        }

        void generate_chunks(chunk_writer& writer, chunk* chunk_root)
        {
            chunk* next = 0;
            for (chunk* it = chunk_root->children(); it; it = it->next()) {
                if (!it->inline_) {
                    next = it;
                    break;
                }
            }
            if (!next) {
                next = chunk_root->next();
            }

            chunk* prev = chunk_root->prev();
            if (prev) {
                while (prev->children()) {
                    for (prev = prev->children(); prev->next();
                         prev = prev->next()) {
                    }
                }
            }
            else {
                prev = chunk_root->parent();
            }

            html_gen gen(
                writer.id_paths,
                path_to_generic(path_difference(
                    (writer.root_path / chunk_root->path_).parent_path(),
                    writer.graphics_path)),
                chunk_root->path_);
            if (!writer.css_path.empty()) {
                tag_start(gen, "link");
                tag_attribute(gen, "rel", "stylesheet");
                tag_attribute(gen, "type", "text/css");
                tag_attribute(
                    gen, "href",
                    path_to_generic(path_difference(
                        (writer.root_path / chunk_root->path_).parent_path(),
                        writer.css_path)));
                tag_end_self_close(gen);
            }
            tag_start(gen, "div");
            tag_attribute(gen, "class", "spirit-nav");
            tag_end(gen);
            if (prev) {
                tag_start(gen, "a");
                tag_attribute(
                    gen, "href",
                    relative_path_from(prev->path_, chunk_root->path_));
                tag_attribute(gen, "accesskey", "p");
                tag_end(gen);
                graphics_tag(gen, "/prev.png", "prev");
                close_tag(gen, "a");
                gen.html += " ";
            }
            if (chunk_root->parent()) {
                tag_start(gen, "a");
                tag_attribute(
                    gen, "href",
                    relative_path_from(
                        chunk_root->parent()->path_, chunk_root->path_));
                tag_attribute(gen, "accesskey", "u");
                tag_end(gen);
                graphics_tag(gen, "/up.png", "up");
                close_tag(gen, "a");
                gen.html += " ";

                tag_start(gen, "a");
                tag_attribute(
                    gen, "href",
                    relative_path_from("index.html", chunk_root->path_));
                tag_attribute(gen, "accesskey", "h");
                tag_end(gen);
                graphics_tag(gen, "/home.png", "home");
                close_tag(gen, "a");
                if (next) {
                    gen.html += " ";
                }
            }
            if (next) {
                tag_start(gen, "a");
                tag_attribute(
                    gen, "href",
                    relative_path_from(next->path_, chunk_root->path_));
                tag_attribute(gen, "accesskey", "n");
                tag_end(gen);
                graphics_tag(gen, "/next.png", "next");
                close_tag(gen, "a");
            }
            close_tag(gen, "div");
            generate_chunk_html(gen, chunk_root);
            chunk* it = chunk_root->children();
            for (; it && it->inline_; it = it->next()) {
                generate_inline_chunks(gen, it);
            }
            generate_footnotes_html(gen);
            writer.write_file(chunk_root->path_, gen.html);
            for (; it; it = it->next()) {
                assert(!it->inline_);
                generate_chunks(writer, it);
            }
        }

        void generate_inline_chunks(html_gen& gen, chunk* root)
        {
            tag_start(gen, "div");
            tag_attribute(gen, "id", root->id_);
            tag_end(gen);
            generate_chunk_html(gen, root);
            for (chunk* it = root->children(); it; it = it->next()) {
                assert(it->inline_);
                generate_inline_chunks(gen, it);
            }
            close_tag(gen, "div");
        }

        void generate_chunk_html(html_gen& gen, chunk* root)
        {
            gen.callout_numbers.clear();

            number_callouts(gen, root->title_.root());
            number_callouts(gen, root->info_.root());
            number_callouts(gen, root->root_.root());

            generate_tree_html(gen, root->title_.root());
            generate_tree_html(gen, root->info_.root());
            generate_toc_html(gen, root);
            generate_tree_html(gen, root->root_.root());
        }

        void generate_toc_html(html_gen& gen, chunk* root)
        {
            if (root->children()) {
                tag_start(gen, "div");
                tag_attribute(gen, "class", "toc");
                tag_end(gen);
                open_tag(gen, "p");
                open_tag(gen, "b");
                gen.html += "Table of contents";
                close_tag(gen, "b");
                close_tag(gen, "p");
                generate_toc_subtree(gen, root, root);
                close_tag(gen, "div");
            }
        }

        void generate_toc_subtree(html_gen& gen, chunk* page, chunk* chunk_root)
        {
            gen.html += "<ul>";
            for (chunk* it = chunk_root->children(); it; it = it->next()) {
                id_paths_type::const_iterator link = gen.id_paths.find(it->id_);
                gen.html += "<li>";
                if (link != gen.id_paths.end()) {
                    gen.html += "<a href=\"";
                    gen.html += encode_string(
                        relative_path_from(link->second, page->path_));
                    gen.html += "\">";
                    generate_toc_item_html(gen, it->title_.root());
                    gen.html += "</a>";
                }
                else {
                    generate_toc_item_html(gen, it->title_.root());
                }
                if (it->children()) {
                    generate_toc_subtree(gen, page, it);
                }
                gen.html += "</li>";
            }
            gen.html += "</ul>";
        }

        void generate_toc_item_html(html_gen& gen, xml_element* x)
        {
            if (x) {
                bool old = gen.in_toc;
                gen.in_toc = true;
                generate_children_html(gen, x);
                gen.in_toc = false;
            }
            else {
                gen.html += "<i>Untitled</i>";
            }
        }

        void generate_footnotes_html(html_gen& gen)
        {
            if (!gen.footnotes.empty()) {
                tag_start(gen, "div");
                tag_attribute(gen, "class", "footnotes");
                tag_end(gen);
                gen.html += "<br/>";
                gen.html += "<hr/>";
                for (std::vector<xml_element*>::iterator it =
                         gen.footnotes.begin();
                     it != gen.footnotes.end(); ++it) {
                    std::string footnote_label =
                        *(*it)->get_attribute("(((footnote-label)))");
                    tag_start(gen, "div");
                    tag_attribute(gen, "id", "footnote-" + footnote_label);
                    tag_attribute(gen, "class", "footnote");
                    tag_end(gen);

                    // TODO: This should be part of the first paragraph in the
                    // footnote.
                    tag_start(gen, "a");
                    // TODO: Might not have an id.
                    tag_attribute(
                        gen, "href", "#" + *(*it)->get_attribute("id"));
                    tag_end(gen);
                    tag_start(gen, "sup");
                    tag_end(gen);
                    gen.html += "[" + footnote_label + "]";
                    close_tag(gen, "sup");
                    close_tag(gen, "a");
                    generate_children_html(gen, *it);
                    close_tag(gen, "div");
                }
                close_tag(gen, "div");
            }
        }

        void number_callouts(html_gen& gen, xml_element* x)
        {
            for (; x; x = x->next()) {
                if (x->type_ == xml_element::element_node) {
                    if (x->name_ == "calloutlist") {
                        unsigned count = 0;
                        number_callouts2(gen, count, x);
                    }
                    else if (x->name_ == "co") {
                        // TODO: Set id if missing?
                        std::string* linkends = x->get_attribute("linkends");
                        std::string* id = x->get_attribute("id");
                        if (id && linkends) {
                            gen.callout_numbers[*linkends].link_id = *id;
                        }
                    }
                }
                number_callouts(gen, x->children());
            }
        }

        void number_callouts2(html_gen& gen, unsigned& count, xml_element* x)
        {
            for (; x; x = x->next()) {
                if (x->type_ == xml_element::element_node &&
                    x->name_ == "callout") {
                    std::string* id = x->get_attribute("id");
                    if (id) {
                        gen.callout_numbers[*id].number = ++count;
                    }
                }
                number_callouts2(gen, count, x->children());
            }
        }

        void generate_tree_html(html_gen& gen, xml_element* x)
        {
            switch (x->type_) {
            case xml_element::element_text: {
                gen.html += x->contents_;
                break;
            }
            case xml_element::element_node: {
                node_parsers_type::iterator parser =
                    node_parsers.find(x->name_);
                if (parser != node_parsers.end()) {
                    parser->second(gen, x);
                }
                else {
                    quickbook::detail::out()
                        << "Unsupported tag: " << x->name_ << std::endl;
                    generate_children_html(gen, x);
                }
                break;
            }
            default:
                assert(false);
            }
        }

        void generate_children_html(html_gen& gen, xml_element* x)
        {
            for (xml_element* it = x->children(); it; it = it->next()) {
                generate_tree_html(gen, it);
            }
        }

        void write_file(fs::path const& path, std::string const& content)
        {
            fs::ofstream fileout(path);

            if (fileout.fail()) {
                ::quickbook::detail::outerr()
                    << "Error opening output file " << path << std::endl;

                return /*1*/;
            }

            fileout << content;

            if (fileout.fail()) {
                ::quickbook::detail::outerr()
                    << "Error writing to output file " << path << std::endl;

                return /*1*/;
            }
        }

        std::string relative_path_from(
            quickbook::string_view path, quickbook::string_view base)
        {
            string_iterator path_it = path.begin();
            string_iterator base_it = base.begin();
            string_iterator path_diff_start = path_it;
            string_iterator base_diff_start = base_it;

            for (; path_it != path.end() && base_it != base.end() &&
                   *path_it == *base_it;
                 ++path_it, ++base_it) {
                if (*path_it == '/') {
                    path_diff_start = path_it + 1;
                    base_diff_start = base_it + 1;
                }
            }

            auto up_count = std::count(base_diff_start, base.end(), '/');

            std::string result;
            for (int i = 0; i < up_count; ++i) {
                result += "../";
            }
            result.append(path_diff_start, path.end());
            return result;
        }

        // get_id_paths

        id_paths_type get_id_paths(chunk* chunk)
        {
            id_paths_type id_paths;
            if (chunk) {
                get_id_paths_impl(id_paths, chunk);
            }
            return id_paths;
        }

        void get_id_paths_impl(id_paths_type& id_paths, chunk* c)
        {
            std::string p = c->path_;
            if (c->inline_) {
                p += '#';
                p += c->id_;
            }
            id_paths.emplace(c->id_, boost::move(p));

            get_id_paths_impl2(id_paths, c->path_, c->title_.root());
            get_id_paths_impl2(id_paths, c->path_, c->info_.root());
            get_id_paths_impl2(id_paths, c->path_, c->root_.root());
            for (chunk* i = c->children(); i; i = i->next()) {
                get_id_paths_impl(id_paths, i);
            }
        }

        void get_id_paths_impl2(
            id_paths_type& id_paths, string_view path, xml_element* node)
        {
            if (!node) {
                return;
            }
            std::string* id = node->get_attribute("id");
            if (id) {
                // TODO: No need for fragment when id matches chunk.
                std::string p(path.begin(), path.end());
                p += '#';
                p += *id;
                id_paths.emplace(*id, boost::move(p));
            }
            for (xml_element* i = node->children(); i; i = i->next()) {
                get_id_paths_impl2(id_paths, path, i);
            }
        }

        void tag(html_gen& gen, quickbook::string_view name, xml_element* x)
        {
            open_tag_with_id(gen, name, x);
            generate_children_html(gen, x);
            close_tag(gen, name);
        }

        void open_tag(html_gen& gen, quickbook::string_view name)
        {
            tag_start(gen, name);
            tag_end(gen);
        }

        void open_tag_with_id(
            html_gen& gen, quickbook::string_view name, xml_element* x)
        {
            tag_start_with_id(gen, name, x);
            tag_end(gen);
        }

        void close_tag(html_gen& gen, quickbook::string_view name)
        {
            gen.html += "</";
            gen.html.append(name.begin(), name.end());
            gen.html += ">";
        }

        void tag_self_close(
            html_gen& gen, quickbook::string_view name, xml_element* x)
        {
            tag_start_with_id(gen, name, x);
            tag_end_self_close(gen);
        }

        void graphics_tag(
            html_gen& gen,
            quickbook::string_view path,
            quickbook::string_view fallback)
        {
            if (!gen.graphics_path.empty()) {
                std::string url = gen.graphics_path;
                url.append(path.begin(), path.end());
                tag_start(gen, "img");
                tag_attribute(gen, "src", url);
                tag_end(gen);
            }
            else {
                gen.html.append(fallback.begin(), fallback.end());
            }
        }

        void tag_start(html_gen& gen, quickbook::string_view name)
        {
            gen.html += "<";
            gen.html.append(name.begin(), name.end());
        }

        void tag_start_with_id(
            html_gen& gen, quickbook::string_view name, xml_element* x)
        {
            tag_start(gen, name);
            if (!gen.in_toc) {
                std::string* id = x->get_attribute("id");
                if (id) {
                    tag_attribute(gen, "id", *id);
                }
            }
        }

        void tag_end(html_gen& gen) { gen.html += ">"; }

        void tag_end_self_close(html_gen& gen) { gen.html += "/>"; }

        void tag_attribute(
            html_gen& gen,
            quickbook::string_view name,
            quickbook::string_view value)
        {
            gen.html += " ";
            gen.html.append(name.begin(), name.end());
            gen.html += "=\"";
            gen.html.append(value.begin(), value.end());
            gen.html += "\"";
        }

// Handle boostbook nodes

#define NODE_RULE(tag_name, gen, x)                                            \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen&, xml_element*);             \
    static struct BOOST_PP_CAT(register_parser_type_, tag_name)                \
    {                                                                          \
        BOOST_PP_CAT(register_parser_type_, tag_name)()                        \
        {                                                                      \
            node_parsers.emplace(                                              \
                BOOST_PP_STRINGIZE(tag_name),                                  \
                BOOST_PP_CAT(parser_, tag_name));                              \
        }                                                                      \
    } BOOST_PP_CAT(register_parser_, tag_name);                                \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen & gen, xml_element * x)

#define NODE_MAP(tag_name, html_name)                                          \
    NODE_RULE(tag_name, gen, x) { tag(gen, BOOST_PP_STRINGIZE(html_name), x); }

#define NODE_MAP_CLASS(tag_name, html_name, class_name)                        \
    NODE_RULE(tag_name, gen, x)                                                \
    {                                                                          \
        tag_start_with_id(gen, BOOST_PP_STRINGIZE(html_name), x);              \
        tag_attribute(gen, "class", BOOST_PP_STRINGIZE(class_name));           \
        tag_end(gen);                                                          \
        generate_children_html(gen, x);                                        \
        close_tag(gen, BOOST_PP_STRINGIZE(html_name));                         \
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

        // TODO: Header levels
        NODE_MAP(title, h3)
        NODE_MAP(bridgehead, h3)

        NODE_MAP_CLASS(sidebar, div, sidebar) // TODO: sidebar role="blurb"
        NODE_MAP_CLASS(warning, div, warning)
        NODE_MAP_CLASS(caution, div, caution)
        NODE_MAP_CLASS(important, div, important)
        NODE_MAP_CLASS(note, div, note)
        NODE_MAP_CLASS(tip, div, tip)
        NODE_MAP_CLASS(replaceable, em, replaceable)

        NODE_RULE(sbr, gen, x)
        {
            if (!x->children()) {
                tag_self_close(gen, "br", x);
            }
            else {
                tag(gen, "br", x);
            }
        }

        NODE_RULE(ulink, gen, x)
        {
            // TODO: error if missing?
            std::string* value = x->get_attribute("url");

            tag_start_with_id(gen, "a", x);
            if (value) {
                tag_attribute(gen, "href", *value);
            }
            tag_end(gen);
            generate_children_html(gen, x);
            close_tag(gen, "a");
        }

        NODE_RULE(link, gen, x)
        {
            // TODO: error if missing?
            std::string* value = x->get_attribute("linkend");

            id_paths_type::const_iterator it = gen.id_paths.end();
            if (value) {
                it = gen.id_paths.find(*value);
            }

            tag_start_with_id(gen, "a", x);
            if (it != gen.id_paths.end()) {
                tag_attribute(
                    gen, "href", relative_path_from(it->second, gen.path));
            }
            tag_end(gen);
            generate_children_html(gen, x);
            close_tag(gen, "a");
        }

        NODE_RULE(phrase, gen, x)
        {
            std::string* value = x->get_attribute("role");

            tag_start_with_id(gen, "span", x);
            if (value) {
                tag_attribute(gen, "class", *value);
            }
            tag_end(gen);
            generate_children_html(gen, x);
            close_tag(gen, "span");
        }

        NODE_RULE(emphasis, gen, x)
        {
            std::string* value = x->get_attribute("role");
            quickbook::string_view tag_name = "em";
            quickbook::string_view class_name = "";
            // TODO: case insensitive?
            if (value) {
                if (*value == "bold" || *value == "strong") {
                    tag_name = "strong";
                }
                else {
                    tag_name = "span";
                    class_name = *value;
                }
            }
            tag_start_with_id(gen, tag_name, x);
            if (!class_name.empty()) {
                tag_attribute(gen, "class", class_name);
            }
            tag_end(gen);
            generate_children_html(gen, x);
            close_tag(gen, tag_name);
        }

        NODE_RULE(inlinemediaobject, gen, x)
        {
            std::string* image;

            // Get image link
            for (xml_element* i = x->children(); i; i = i->next()) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "imageobject") {
                    for (xml_element* j = i->children(); j; j = j->next()) {
                        if (j->type_ == xml_element::element_node &&
                            j->name_ == "imagedata") {
                            image = j->get_attribute("fileref");
                            if (image) {
                                break;
                            }
                        }
                    }
                }
            }

            std::string alt;
            for (xml_element* i = x->children(); i; i = i->next()) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "textobject") {
                    for (xml_element* j = i->children(); j; j = j->next()) {
                        if (j->type_ == xml_element::element_node &&
                            j->name_ == "phrase") {
                            std::string* role = j->get_attribute("role");
                            if (role && *role == "alt") {
                                html_gen gen2(gen);
                                generate_tree_html(gen2, j);
                                alt = gen2.html;
                            }
                        }
                    }
                }
            }
            // TODO: This was in the original php code, not sure why.
            if (alt.empty()) {
                alt = "[]";
            }
            if (image) {
                tag_start(gen, "span");
                tag_attribute(gen, "class", "inlinemediaobject");
                tag_end(gen);
                tag_start_with_id(gen, "img", x);
                tag_attribute(gen, "src", relative_path_from(*image, gen.path));
                tag_attribute(gen, "alt", alt);
                tag_end_self_close(gen);
                close_tag(gen, "span");
            }
        }

        NODE_RULE(variablelist, gen, x)
        {
            typedef std::vector<std::pair<xml_element*, xml_element*> >
                items_type;
            items_type items;
            for (xml_element* i = x->children(); i; i = i->next()) {
                if (i && i->type_ == xml_element::element_node) {
                    if (i->name_ == "title") {
                        // TODO: What to do with titles?
                        continue;
                    }
                    else if (i->name_ == "varlistentry") {
                        // TODO: What if i has an id?
                        xml_element* term = 0;
                        xml_element* listitem = 0;
                        for (xml_element* j = i->children(); j; j = j->next()) {
                            if (j && j->type_ == xml_element::element_node) {
                                if (j->name_ == "term") {
                                    term = j;
                                }
                                else if (j->name_ == "listitem") {
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
                for (items_type::iterator i = items.begin(); i != items.end();
                     ++i) {
                    tag(gen, "dt", i->first);
                    tag(gen, "dd", i->second);
                }
                close_tag(gen, "dl");
            }
        }

        void write_table_rows(html_gen& gen, xml_element* x, char const* td_tag)
        {
            for (xml_element* i = x->children(); i; i = i->next()) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "row") {
                    open_tag_with_id(gen, "tr", i);
                    for (xml_element* j = i->children(); j; j = j->next()) {
                        if (j->type_ == xml_element::element_node &&
                            j->name_ == "entry") {
                            open_tag_with_id(gen, td_tag, j);
                            generate_children_html(gen, j);
                            close_tag(gen, td_tag);
                        }
                    }
                    close_tag(gen, "tr");
                }
            }
        }

        void write_table(html_gen& gen, xml_element* x)
        {
            xml_element* title = 0;
            xml_element* tgroup = 0;
            xml_element* thead = 0;
            xml_element* tbody = 0;

            for (xml_element* i = x->children(); i; i = i->next()) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "title") {
                    title = i;
                }
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "tgroup") {
                    tgroup = i;
                }
            }

            if (!tgroup) {
                return;
            }

            for (xml_element* i = tgroup->children(); i; i = i->next()) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "thead") {
                    thead = i;
                }
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "tbody") {
                    tbody = i;
                }
            }

            tag_start_with_id(gen, "div", x);
            tag_attribute(gen, "class", x->name_);
            tag_end(gen);
            open_tag(gen, "table");
            if (title) {
                tag(gen, "caption", title);
            }
            if (thead) {
                open_tag(gen, "thead");
                write_table_rows(gen, thead, "th");
                close_tag(gen, "thead");
            }
            if (tbody) {
                open_tag(gen, "tbody");
                write_table_rows(gen, tbody, "td");
                close_tag(gen, "tbody");
            }
            close_tag(gen, "table");
            close_tag(gen, "div");
        }

        NODE_RULE(table, gen, x) { write_table(gen, x); }
        NODE_RULE(informaltable, gen, x) { write_table(gen, x); }

        NODE_MAP(calloutlist, div)

        NODE_RULE(callout, gen, x)
        {
            std::string* id = x->get_attribute("id");
            boost::unordered_map<string_view, callout_data>::const_iterator
                data = gen.callout_numbers.end();
            id_paths_type::const_iterator link = gen.id_paths.end();
            if (id) {
                data = gen.callout_numbers.find(*id);
            }
            if (data != gen.callout_numbers.end() &&
                !data->second.link_id.empty()) {
                link = gen.id_paths.find(data->second.link_id);
            }

            open_tag_with_id(gen, "div", x);
            if (link != gen.id_paths.end()) {
                tag_start(gen, "a");
                tag_attribute(
                    gen, "href", relative_path_from(link->second, gen.path));
                tag_end(gen);
            }
            graphics_tag(
                gen,
                "/callouts/" +
                    boost::lexical_cast<std::string>(data->second.number) +
                    ".png",
                "(" + boost::lexical_cast<std::string>(data->second.number) +
                    ")");
            if (link != gen.id_paths.end()) {
                close_tag(gen, "a");
            }
            gen.html += " ";
            generate_children_html(gen, x);
            close_tag(gen, "div");
        }

        NODE_RULE(co, gen, x)
        {
            std::string* linkends = x->get_attribute("linkends");
            boost::unordered_map<string_view, callout_data>::const_iterator
                data = gen.callout_numbers.end();
            id_paths_type::const_iterator link = gen.id_paths.end();
            if (linkends) {
                data = gen.callout_numbers.find(*linkends);
                link = gen.id_paths.find(*linkends);
            }

            if (link != gen.id_paths.end()) {
                tag_start(gen, "a");
                tag_attribute(
                    gen, "href", relative_path_from(link->second, gen.path));
                tag_end(gen);
            }
            if (data != gen.callout_numbers.end()) {
                graphics_tag(
                    gen,
                    "/callouts/" +
                        boost::lexical_cast<std::string>(data->second.number) +
                        ".png",
                    "(" +
                        boost::lexical_cast<std::string>(data->second.number) +
                        ")");
            }
            else {
                gen.html += "(0)";
            }
            if (link != gen.id_paths.end()) {
                close_tag(gen, "a");
            }
        }

        NODE_RULE(footnote, gen, x)
        {
            // TODO: Better id generation....
            static int footnote_number = 0;
            ++footnote_number;
            std::string footnote_label =
                boost::lexical_cast<std::string>(footnote_number);
            x->attributes_.push_back(
                std::make_pair("(((footnote-label)))", footnote_label));
            gen.footnotes.push_back(x);

            tag_start_with_id(gen, "a", x);
            tag_attribute(gen, "href", "#footnote-" + footnote_label);
            tag_end(gen);
            tag_start(gen, "sup");
            tag_attribute(gen, "class", "footnote");
            tag_end(gen);
            gen.html += "[" + footnote_label + "]";
            close_tag(gen, "sup");
            close_tag(gen, "a");
        }
    }
}
