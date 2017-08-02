/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "bb2html.hpp"
#include "simple_parse.hpp"
#include "native_text.hpp"
#include "utils.hpp"
#include <vector>
#include <cassert>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>

namespace quickbook {
    namespace fs = boost::filesystem;
}

namespace quickbook { namespace detail {
    struct tree_node {
        tree_node* parent_;
        tree_node* children_;
        tree_node* next_;
        tree_node* prev_;

        tree_node() : parent_(), children_(), next_(), prev_() {}
    };

    template <typename T>
    struct tree_node_impl : tree_node {
        T* extract() {
            T* next = static_cast<T*>(next_);
            if (parent_ && !prev_) { parent_->children_ = next; }
            if (prev_) { prev_->next_ = next_; }
            if (next_) { next_->prev_ = prev_; }
            parent_ = 0;
            next_ = 0;
            prev_ = 0;
            return next;
        }

        T* parent() const { return static_cast<T*>(parent_); }
        T* children() const { return static_cast<T*>(children_); }
        T* next() const { return static_cast<T*>(next_); }
        T* prev() const { return static_cast<T*>(prev_); }
    };

    template <typename T>
    struct tree_builder {
        T* root_;
        T* current_;
        T* parent_;

        tree_builder() :
            root_(0),
            current_(0),
            parent_(0) {}

        void add_element(T* n) {
            n->parent_ = parent_;
            n->prev_ = current_;
            if (current_) {
                current_->next_ = n;
            }
            else if (parent_) {
                parent_->children_ = n;
            }
            else {
                root_ = n;
            }
            current_ = n;
        }

        void start_children() {
            parent_ = current_;
            current_ = 0;
        }

        void end_children() {
            current_ = parent_;
            parent_ = current_->parent();
        }
    };

    struct xml_element : tree_node_impl<xml_element> {
        enum element_type { element_node, element_text } type_;
        std::string name_;
        std::vector<std::pair<std::string, std::string> > attributes_;
        std::string contents_;

        explicit xml_element(element_type n) : type_(n) {}

        explicit xml_element(element_type n, quickbook::string_view name) :
            type_(n),
            name_(name.begin(), name.end()) {}

        static xml_element* text_node(quickbook::string_view x) {
            xml_element* n = new xml_element(element_text);
            n->contents_.assign(x.begin(), x.end());
            return n;
        }

        static xml_element* node(quickbook::string_view x) {
            return new xml_element(element_node, x);
        }

        std::string* get_attribute(quickbook::string_view name) {
            for (std::vector<std::pair<std::string, std::string> >::iterator
                it = attributes_.begin(), end = attributes_.end();
                it != end; ++it) {
                if (name == it->first) { return &it->second; }
            }
            return 0;
        }
    };

    typedef tree_builder<xml_element> xml_tree_builder;

    struct chunk : tree_node_impl<chunk> {
        xml_element* title_;
        xml_element* info_;
        xml_element* root_;
        std::string path_;

        chunk() : title_(), info_(), root_() {}
    };

    typedef boost::unordered_map<string_view, std::string> id_paths_type;

    struct html_gen {
        id_paths_type const& id_paths;
        string_view path;
        std::string html;

        html_gen(html_gen const& x) : id_paths(x.id_paths), path(x.path) {}
        explicit html_gen(id_paths_type const& ip, string_view p) : id_paths(ip), path(p) {}
    };

    void generate_html(html_gen&, xml_element*);
    chunk* chunk_document(xml_element*, fs::path const&);
    std::string id_to_path(quickbook::string_view);
    std::string relative_path_from(quickbook::string_view, quickbook::string_view);

    quickbook::string_view read_string(quickbook::string_view::iterator& it, quickbook::string_view::iterator end) {
        assert(it != end && (*it == '"' || *it == '\''));

        quickbook::string_view::iterator start = it;
        char deliminator = *it;
        ++it;
        read_to(it, end, deliminator);
        if (it == end) {
            throw boostbook_parse_error("Invalid string", start);
        }
        ++it;
        return quickbook::string_view(start + 1, it - start - 2);
    }

    void skip_question_mark_tag(quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end && *it == '?');
        ++it;

        while (true) {
            read_to_one_of(it, end, "\"'?<>");
            if (it == end) {
                throw boostbook_parse_error("Invalid tag", start);
            }
            switch (*it) {
            case '"':
            case '\'':
                read_string(it, end);
                break;
            case '?':
                if (read(it, end, "?>")) {
                    return;
                }
                else {
                    ++it;
                }
                break;
            default:
                throw boostbook_parse_error("Invalid tag", start);
            }
        }
    }

    void skip_exclamation_mark_tag(quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end && *it == '!');
        ++it;

        if (read(it, end, "--")) {
            if (read_past(it, end, "-->")) {
                return;
            }
            else {
                throw boostbook_parse_error("Invalid comment", start);
            }
        }

        while (true) {
            read_to_one_of(it, end, "\"'<>");
            if (it == end) {
                throw boostbook_parse_error("Invalid tag", start);
            }
            switch (*it) {
            case '"':
            case '\'':
                read_string(it, end);
                break;
            case '>':
                ++it;
                return;
            default:
                throw boostbook_parse_error("Invalid tag", start);
            }
        }
    }

    quickbook::string_view read_tag_name(quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        read_some_of(it, end, " \t\n\r");
        quickbook::string_view::iterator name_start = it;
        read_some_of(it, end, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ:-");
        if (name_start == it) {
            throw boostbook_parse_error("Invalid tag", start);
        }
        return quickbook::string_view(name_start, it - name_start);
    }

    quickbook::string_view read_attribute_value(quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        read_some_of(it, end, " \t\n\r");
        quickbook::string_view::iterator value_start = it;
        if (*it == '"' || *it == '\'') {
            return read_string(it, end);
        }
        else {
            throw boostbook_parse_error("Invalid tag", start);
        }
    }

    void read_tag(xml_tree_builder& builder, quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end);
        quickbook::string_view name = read_tag_name(it, start, end);
        xml_element* node = xml_element::node(name);
        builder.add_element(node);

        // Read attributes
        while (true) {
            read_some_of(it, end, " \t\n\r");
            if (it == end) {
                throw boostbook_parse_error("Invalid tag", start);
            }
            if (*it == '>') {
                ++it;
                builder.start_children();
                break;
            }
            if (*it == '/') {
                ++it;
                read_some_of(it, end, " \t\n\r");
                if (it == end || *it != '>') {
                    throw boostbook_parse_error("Invalid tag", start);
                }
                ++it;
                break;
            }
            quickbook::string_view attribute_name = read_tag_name(it, start, end);
            read_some_of(it, end, " \t\n\r");
            if (it == end) {
                throw boostbook_parse_error("Invalid tag", start);
            }
            quickbook::string_view attribute_value;
            if (*it == '=') {
                ++it;
                attribute_value = read_attribute_value(it, start, end);
            }
            // TODO: Decode attribute value
            node->attributes_.push_back(std::make_pair(
                std::string(attribute_name.begin(), attribute_name.end()),
                std::string(attribute_value.begin(), attribute_value.end())
            ));
        }
    }

    void read_close_tag(xml_tree_builder& builder, quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end && *it == '/');
        ++it;
        quickbook::string_view name = read_tag_name(it, start, end);
        read_some_of(it, end, " \t\n\r");
        if (it == end || *it != '>') {
            throw boostbook_parse_error("Invalid close tag", start);
        }
        ++it;

        if (!builder.parent_ || builder.parent_->name_ != name) {
            throw boostbook_parse_error("Close tag doesn't match", start);
        }

        builder.end_children();
    }

    id_paths_type get_id_paths(chunk* chunk);
    void generate_documentation(chunk* root, id_paths_type const&, fs::path const& root_path);

    int boostbook_to_html(quickbook::string_view source, boost::filesystem::path const& fileout_) {
        typedef quickbook::string_view::const_iterator iterator;
        iterator it = source.begin(), end = source.end();

        xml_tree_builder builder;

        while (true) {
            iterator start = it;
            read_to(it, end, '<');
            if (start != it) {
                builder.add_element(xml_element::text_node(
                    quickbook::string_view(start, it - start)));
            }

            if (it == end) { break; }
            start = it++;
            if (it == end) {
                throw boostbook_parse_error("Invalid tag", start);
            }

            switch (*it)
            {
            case '?':
                skip_question_mark_tag(it, start, end);
                break;
            case '!':
                skip_exclamation_mark_tag(it, start, end);
                break;
            case '/':
                read_close_tag(builder, it, start, end);
                break;
            default:
                read_tag(builder, it, start, end);
                break;
            }
        }

        chunk* chunked = chunk_document(builder.root_, fileout_);
        id_paths_type id_paths = get_id_paths(chunked);
        generate_documentation(chunked, id_paths, fileout_);
        return 0;
    }

    void get_id_paths_impl(id_paths_type&, chunk*);
    void get_id_paths_impl2(id_paths_type&, string_view, xml_element*);

    id_paths_type get_id_paths(chunk* chunk) {
        id_paths_type id_paths;
        get_id_paths_impl(id_paths, chunk);
        return id_paths;
    }

    void get_id_paths_impl(id_paths_type& id_paths, chunk* c) {
        get_id_paths_impl2(id_paths, c->path_, c->title_);
        get_id_paths_impl2(id_paths, c->path_, c->info_);
        get_id_paths_impl2(id_paths, c->path_, c->root_);
        for(chunk* i = c->children(); i; i = i->next())
        {
            get_id_paths_impl(id_paths, i);
        }
    }

    void get_id_paths_impl2(id_paths_type& id_paths, string_view path, xml_element* node) {
        if (!node) { return; }
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

    void write_file(fs::path const& path, std::string const& content);
    void generate_contents(html_gen& gen, chunk* root);
    void generate_chunks(chunk* root, id_paths_type const& id_paths, fs::path const& root_path);

    void generate_documentation(chunk* chunked, id_paths_type const& id_paths, fs::path const& path) {
        //write_file(path, generate_contents(chunked));
        generate_chunks(chunked, id_paths, path.parent_path());
    }

    struct chunk_writer {
        fs::path const& root_path;
        id_paths_type const& id_paths;

        explicit chunk_writer(fs::path const& r, id_paths_type const& ip) : root_path(r), id_paths(ip) {}

        void write_file(std::string const& generic_path, std::string const& content) {
            fs::path path = root_path / generic_to_path(generic_path);
            fs::create_directories(path.parent_path());
            quickbook::detail::write_file(path, content);
        }
    };

    void generate_contents_impl(html_gen& gen, chunk*, chunk*);

    void generate_contents(html_gen& gen, chunk* root) {
        assert(root->children() && !root->children()->next());
        chunk* root_chunk = root->children();
        generate_html(gen, root_chunk->title_);
        generate_html(gen, root_chunk->root_->children());
        // TODO: root_chunk is wrong.....
        generate_contents_impl(gen, root_chunk, root_chunk);
    }

    void generate_contents_impl(html_gen& gen, chunk* page, chunk* chunk_root) {
        gen.html += "<ul>";
        for (chunk* it = chunk_root->children(); it; it = it->next())
        {
            gen.html += "<li>";
            gen.html += "<a href=\"";
            gen.html += encode_string(relative_path_from(it->path_, page->path_));
            gen.html += "\">";
            generate_html(gen, it->title_->children());
            gen.html += "</a>";
            if (it->children()) {
                generate_contents_impl(gen, page, it);
            }
            gen.html += "</li>";
        }
        gen.html += "</ul>";
    }

    void generate_chunks_impl(chunk_writer&, chunk*);

    void generate_chunks(chunk* root, id_paths_type const& id_paths, fs::path const& root_path) {
        chunk_writer writer(root_path, id_paths);
        generate_chunks_impl(writer, root);
    }

    void generate_chunks_impl(chunk_writer& writer, chunk* chunk_root) {
        for (chunk* it = chunk_root; it; it = it->next())
        {
            html_gen gen(writer.id_paths, it->path_);
            generate_html(gen, it->title_);
            generate_html(gen, it->info_);
            if (it->children()) {
                generate_contents_impl(gen, it, it);
            }
            generate_html(gen, it->root_->children());
            writer.write_file(it->path_, gen.html);
            generate_chunks_impl(writer, it->children());
        }
    }

    void write_file(fs::path const& path, std::string const& content) {
        fs::ofstream fileout(path);

        if (fileout.fail()) {
            ::quickbook::detail::outerr()
                << "Error opening output file "
                << path
                << std::endl;

            return /*1*/;
        }

        fileout << content;

        if (fileout.fail()) {
            ::quickbook::detail::outerr()
                << "Error writing to output file "
                << path
                << std::endl;

            return /*1*/;
        }
    }

    // Chunker

    boost::unordered_set<std::string> chunk_types;
    boost::unordered_set<std::string> chunkinfo_types;

    static struct init_chunk_type {
        init_chunk_type() {
            chunk_types.insert("book");
            chunk_types.insert("article");
            chunk_types.insert("library");
            chunk_types.insert("chapter");
            chunk_types.insert("part");
            chunk_types.insert("appendix");
            chunk_types.insert("preface");
            chunk_types.insert("qandadiv");
            chunk_types.insert("qandaset");
            chunk_types.insert("reference");
            chunk_types.insert("set");
            chunk_types.insert("section");

            for (boost::unordered_set<std::string>::const_iterator it = chunk_types.begin(); it != chunk_types.end(); ++it) {
                chunkinfo_types.insert(*it + "info");
            }
        }
    } init_chunk;

    struct chunk_builder : tree_builder<chunk> {
        std::string path;
        std::string extension;
        int count;

        chunk_builder(std::string const& p, std::string const& e) : path(p), extension(e), count(0) {}

        std::string next_path_name() {
            std::string result = path;
            if (count) {
                result += "-";
                result += boost::lexical_cast<std::string>(count);
            }
            result += extension;
            ++count;
            return result;
        }
    };

    void chunk_nodes(chunk_builder& builder, xml_element* node);

    chunk* chunk_document(xml_element* root, fs::path const& p) {
        chunk_builder builder(
            path_to_generic(p.stem()),
            path_to_generic(p.extension()));
        chunk_nodes(builder, root);
        return builder.root_;
    }

    void chunk_nodes(chunk_builder& builder, xml_element* node) {
        chunk* parent = builder.parent_;

        for (xml_element* it = node; it;) {
            if (parent && it->type_ == xml_element::element_node && it->name_ == "title")
            {
                parent->title_ = it;
                it = it->extract();
            }
            else if (parent && it->type_ == xml_element::element_node && chunkinfo_types.find(it->name_) != chunkinfo_types.end())
            {
                parent->info_ = it;
                it = it->extract();
            }
            else if (it->type_ == xml_element::element_node && chunk_types.find(it->name_) != chunk_types.end()) {
                chunk* chunk_node = new chunk();
                chunk_node->root_ = it;
                std::string* id = it->get_attribute("id");
                chunk_node->path_ = id ? id_to_path(*id) : builder.next_path_name();
                it = it->extract();
                builder.add_element(chunk_node);
                builder.start_children();
                chunk_nodes(builder, chunk_node->root_->children());
                builder.end_children();
            } else {
                it = it->next();
            }
        }
    }

    // HTML generator

    void document(html_gen&, xml_element*);
    typedef void(*node_parser)(html_gen&, xml_element*);
    typedef boost::unordered_map<quickbook::string_view, node_parser> node_parsers_type;
    static node_parsers_type node_parsers;

    void open_tag(html_gen& gen, quickbook::string_view name) {
        gen.html += "<";
        gen.html.append(name.begin(), name.end());
        gen.html += ">";
    }

    void close_tag(html_gen& gen, quickbook::string_view name) {
        gen.html += "</";
        gen.html.append(name.begin(), name.end());
        gen.html += ">";
    }

    void tag(html_gen& gen, quickbook::string_view name, xml_element* children) {
        open_tag(gen, name);
        document(gen, children);
        close_tag(gen, name);
    }

    void document(html_gen& gen, xml_element* x) {
        for (; x; x = x->next()) {
            switch (x->type_) {
            case xml_element::element_text: {
                gen.html += x->contents_;
                break;
            }
            case xml_element::element_node: {
                node_parsers_type::iterator it = node_parsers.find(x->name_);
                if (it != node_parsers.end()) {
                    it->second(gen, x);
                }
                else if (x->children()) {
                    document(gen, x->children());
                }
                break;
            }
            default:
                assert(false);
            }
        }
    }

#define NODE_RULE(tag_name, gen, x) \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen&, xml_element*); \
    static struct BOOST_PP_CAT(register_parser_type_, tag_name) { \
        BOOST_PP_CAT(register_parser_type_, tag_name)() { \
            node_parsers.emplace(BOOST_PP_STRINGIZE(tag_name), \
                BOOST_PP_CAT(parser_, tag_name)); \
        } \
    } BOOST_PP_CAT(register_parser_, tag_name); \
    void BOOST_PP_CAT(parser_, tag_name)(html_gen& gen, xml_element* x)

#define NODE_MAP(tag_name, html_name) \
    NODE_RULE(tag_name, gen, x) { \
        tag(gen, BOOST_PP_STRINGIZE(html_name), x->children()); \
    }

    NODE_MAP(para, p)
    NODE_MAP(simpara, div)
    NODE_MAP(title, h3)
    NODE_MAP(orderedlist, ol)
    NODE_MAP(itemizedlist, ul)
    NODE_MAP(listitem, li)
    NODE_MAP(blockquote, blockquote)
    NODE_MAP(code, code)
    NODE_MAP(macronname, code)
    NODE_MAP(classname, code)
    NODE_MAP(programlisting, pre)
    NODE_MAP(literal, tt)
    NODE_MAP(subscript, sub)
    NODE_MAP(superscript, sup)

    NODE_RULE(section, gen, x) {
        std::string* value = x->get_attribute("id");

        gen.html += "<div";
        if (value) {
            gen.html += " id = \"";
            gen.html += *value;
            gen.html += "\"";
        }
        gen.html += ">";
        document(gen, x->children());
        gen.html += "</div>";
    }

    NODE_RULE(ulink, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("url");

        gen.html += "<a";
        if (value) {
            gen.html += " href=\"";
            gen.html += *value;
            gen.html += "\"";
        }
        gen.html += ">";
        document(gen, x->children());
        gen.html += "</a>";
    }

    NODE_RULE(link, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("linkend");

        id_paths_type::const_iterator it = gen.id_paths.end();
        if (value) { it = gen.id_paths.find(*value); }

        gen.html += "<a";
        if (it != gen.id_paths.end()) {
            gen.html += " href=\"";
            gen.html += relative_path_from(it->second, gen.path);
            gen.html += "\"";
        }
        gen.html += ">";
        document(gen, x->children());
        gen.html += "</a>";
    }

    NODE_RULE(phrase, gen, x) {
        std::string* value = x->get_attribute("role");

        gen.html += "<span";
        if (value) {
            gen.html += " class = \"";
            gen.html += *value;
            gen.html += "\"";
        }
        gen.html += ">";
        document(gen, x->children());
        gen.html += "</span>";
    }

    NODE_RULE(emphasis, gen, x) {
        std::string* value = x->get_attribute("role");
        quickbook::string_view tag_name = "em";
        if (value && (*value == "bold" || *value == "strong")) {
            tag_name = "strong";
        }
        // TODO: Error on unrecognized role + case insensitive
        return tag(gen, tag_name, x->children());
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
                    if (j->type_ == xml_element::element_node && j->name_ == "pharse") {
                        std::string* role = j->get_attribute("role");
                        if (role && *role == "alt") {
                            html_gen gen2(gen);
                            generate_html(gen2, j->children());
                            alt = gen2.html;
                        }
                    }
                }
            }
        }
        // TODO: This was in the original php code, not sure why.
        if (alt.empty()) { alt = "[]"; }
        if (image) {
            gen.html += "<img src=\"";
            gen.html += *image;
            gen.html += "\" alt=\"";
            gen.html += alt;
            gen.html += "\">";
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
            open_tag(gen, "dl");
            for(items_type::iterator i = items.begin(); i != items.end(); ++i) {
                open_tag(gen, "dt");
                document(gen, i->first->children());
                close_tag(gen, "dt");
                open_tag(gen, "dd");
                document(gen, i->second->children());
                close_tag(gen, "dd");
            }
            close_tag(gen, "dl");
        }
    }

    void write_table_rows(html_gen& gen, xml_element* x, char const* td_tag) {
        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "row") {
                open_tag(gen, "tr");
                for(xml_element* j = i->children(); j; j = j->next()) {
                    if (j->type_ == xml_element::element_node && j->name_ == "entry") {
                        open_tag(gen, td_tag);
                        document(gen, j->children());
                        close_tag(gen, td_tag);
                    }
                }
                close_tag(gen, "tr");
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

        open_tag(gen, "table");
        if (title) {
            open_tag(gen, "caption");
            document(gen, title->children());
            close_tag(gen, "caption");
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
    }

    NODE_RULE(table, gen, x) { write_table(gen, x); }
    NODE_RULE(informaltable, gen, x) { write_table(gen, x); }

    void generate_html(html_gen& gen, xml_element* x) {
        document(gen, x);
    }

    std::string id_to_path(quickbook::string_view id) {
        std::string result(id.begin(), id.end());
        boost::replace_all(result, ".", "/");
        result += ".html";
        return result;
    }

    std::string relative_path_from(quickbook::string_view path, quickbook::string_view base) {
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
            }
        }

        int up_count = std::count(base_it, base.end(), '/');

        std::string result;
        for (int i = 0; i < up_count; ++i) { result += "../"; }
        result.append(path_diff_start, path.end());
        if (result.empty()) { result = '.'; }
        return result;
    }
}}
