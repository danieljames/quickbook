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
#include <iostream>

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
        T* parent() const { return static_cast<T*>(parent_); }
        T* children() const { return static_cast<T*>(children_); }
        T* next() const { return static_cast<T*>(next_); }
        T* prev() const { return static_cast<T*>(prev_); }
    };

    template <typename Node>
    void delete_nodes(Node* n) {
        while (n) {
            Node* to_delete = n;
            n = n->next();
            delete_nodes(to_delete->children());
            delete_nodes(to_delete);
        }
    }

    template <typename T>
    struct tree_builder {
    private:
        tree_builder(tree_builder const&);
        tree_builder& operator=(tree_builder const&);

    public:
        T* root_;
        T* current_;
        T* parent_;

        tree_builder() :
            root_(0),
            current_(0),
            parent_(0) {}

        ~tree_builder() {
            delete_nodes(root_);
        }

        T* extract(T* x) {
            T* next = static_cast<T*>(x->next_);
            if (!x->prev_) {
                if (x->parent_) { x->parent_->children_ = next; }
                else { assert(x == root_); root_ = next; parent_ = 0; current_ = next; }
            } else { x->prev_->next_ = x->next_; }
            if (x->next_) { x->next_->prev_ = x->prev_; }
            x->parent_ = 0;
            x->next_ = 0;
            x->prev_ = 0;
            return next;
        }

        T* release() {
            T* n = root_;
            root_ = 0;
            current_ = 0;
            parent_ = 0;
            return n;
        }

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
        bool inline_;
        std::string id_;
        std::string path_;

        chunk() : title_(), info_(), root_(), inline_(false) {}

        ~chunk() {
            delete_nodes(title_);
            delete_nodes(info_);
            delete_nodes(root_);
        }
    };

    typedef boost::unordered_map<string_view, std::string> id_paths_type;

    struct html_gen {
        id_paths_type const& id_paths;
        string_view path;
        std::string html;

        html_gen(html_gen const& x) : id_paths(x.id_paths), path(x.path) {}
        explicit html_gen(id_paths_type const& ip, string_view p) : id_paths(ip), path(p) {}
    };

    void tag_attribute(html_gen& gen, quickbook::string_view name, quickbook::string_view value) {
        gen.html += " ";
        gen.html.append(name.begin(), name.end());
        gen.html += "=\"";
        gen.html.append(value.begin(), value.end());
        gen.html += "\"";
    }

    void tag_start(html_gen& gen, quickbook::string_view name) {
        gen.html += "<";
        gen.html.append(name.begin(), name.end());
    }

    void tag_start_with_id(html_gen& gen, quickbook::string_view name, xml_element* x) {
        tag_start(gen, name);
        std::string* id = x->get_attribute("id");
        if (id) {
            tag_attribute(gen, "id", *id);
        }
    }

    void tag_end(html_gen& gen) {
        gen.html += ">";
    }

    void tag_end_self_close(html_gen& gen) {
        gen.html += "/>";
    }

    void open_tag(html_gen& gen, quickbook::string_view name) {
        tag_start(gen, name);
        tag_end(gen);
    }

    void open_tag_with_id(html_gen& gen, quickbook::string_view name, xml_element* x) {
        tag_start_with_id(gen, name, x);
        tag_end(gen);
    }

    void close_tag(html_gen& gen, quickbook::string_view name) {
        gen.html += "</";
        gen.html.append(name.begin(), name.end());
        gen.html += ">";
    }

    void write_xml_tree(xml_element* it, unsigned int depth = 0) {
        for (; it; it = it->next()) {
            for(unsigned i = 0; i < depth; ++i) {
                std::cout << "  ";
            }
            switch (it->type_) {
            case xml_element::element_node:
                std::cout << "Node: " << it->name_;
                break;
            case xml_element::element_text:
                std::cout << "Text";
                break;
            default:
                std::cout << "Unknown node type";
                break;
            }
            std::cout << std::endl;
            write_xml_tree(it->children(), depth + 1);
        }
    }

    void generate_html(html_gen&, xml_element*);
    chunk* chunk_document(xml_tree_builder&);
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
    void generate_chunked_documentation(chunk* root, id_paths_type const&, fs::path const& root_path);
    void generate_file_documentation(chunk* root, id_paths_type const&, fs::path const& path);

    void inline_chunks(chunk* c, string_view path) {
        for(;c; c = c->next()) {
            c->inline_ = true;
            c->path_.assign(path.begin(), path.end());
            inline_chunks(c->children(), path);
        }
    }

    int boostbook_to_html(quickbook::string_view source, boost::filesystem::path const& output_path,
        bool chunked_output)
    {
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

        chunk* chunked = chunk_document(builder);
        // Overwrite paths depending on whether output is chunked or not.
        // Really want to do something better, e.g. incorporate many section chunks into their parent.
        if (chunked_output) {
            chunked->path_ = "index.html";
        } else {
            std::string path = path_to_generic(output_path.filename());
            chunked->path_ = path;
            inline_chunks(chunked->children(), path);
        }
        id_paths_type id_paths = get_id_paths(chunked);
        if (chunked_output) {
            generate_chunked_documentation(chunked, id_paths, output_path);
        } else {
            generate_file_documentation(chunked, id_paths, output_path);
        }
        delete_nodes(chunked);
        return 0;
    }

    void get_id_paths_impl(id_paths_type&, chunk*);
    void get_id_paths_impl2(id_paths_type&, string_view, xml_element*);

    id_paths_type get_id_paths(chunk* chunk) {
        id_paths_type id_paths;
        if (chunk) { get_id_paths_impl(id_paths, chunk); }
        return id_paths;
    }

    void get_id_paths_impl(id_paths_type& id_paths, chunk* c) {
        std::string p = c->path_;
        if (c->inline_) {
            p += '#';
            p += c->id_;
        }
        id_paths.emplace(c->id_, boost::move(p));

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
    void generate_inline_chunks(html_gen& gen, chunk* root);

    void generate_chunked_documentation(chunk* chunked, id_paths_type const& id_paths, fs::path const& path) {
        //write_file(path, generate_contents(chunked));
        //TODO: Error check this:
        fs::create_directory(path);
        generate_chunks(chunked, id_paths, path);
    }

    void generate_file_documentation(chunk* chunked, id_paths_type const& id_paths, fs::path const& path) {
        html_gen gen(id_paths, chunked->path_);
        generate_inline_chunks(gen, chunked);
        write_file(path, gen.html);
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
        write_xml_tree(root_chunk->title_);
        generate_html(gen, root_chunk->title_);
        generate_html(gen, root_chunk->root_->children());
        // TODO: root_chunk is wrong.....
        generate_contents_impl(gen, root_chunk, root_chunk);
    }

    void generate_contents_impl(html_gen& gen, chunk* page, chunk* chunk_root) {
        gen.html += "<ul>";
        for (chunk* it = chunk_root->children(); it; it = it->next())
        {
            id_paths_type::const_iterator link = gen.id_paths.find(it->id_);
            gen.html += "<li>";
            if (link != gen.id_paths.end()) {
                gen.html += "<a href=\"";
                gen.html += encode_string(relative_path_from(link->second, page->path_));
                gen.html += "\">";
                generate_html(gen, it->title_->children());
                gen.html += "</a>";
            } else {
                generate_html(gen, it->title_->children());
            }
            if (it->children()) {
                generate_contents_impl(gen, page, it);
            }
            gen.html += "</li>";
        }
        gen.html += "</ul>";
    }

    void generate_inline_chunks(html_gen& gen, chunk* root) {
        for (chunk* it = root; it; it = it->next())
        {
            tag_start(gen, "div");
            tag_attribute(gen, "id", it->id_);
            tag_end(gen);
            generate_html(gen, it->title_);
            generate_html(gen, it->info_);
            if (it->children()) {
                generate_contents_impl(gen, it, it);
            }
            generate_html(gen, it->root_->children());
            generate_inline_chunks(gen, it->children());
            close_tag(gen, "div");
        }
    }

    void generate_chunks_impl(chunk_writer&, chunk*);

    void generate_chunks(chunk* root, id_paths_type const& id_paths, fs::path const& root_path) {
        chunk_writer writer(root_path, id_paths);
        if (root) { generate_chunks_impl(writer, root); }
    }

    void generate_chunks_impl(chunk_writer& writer, chunk* chunk_root) {
        html_gen gen(writer.id_paths, chunk_root->path_);
        generate_html(gen, chunk_root->title_);
        generate_html(gen, chunk_root->info_);
        if (chunk_root->children()) {
            generate_contents_impl(gen, chunk_root, chunk_root);
        }
        generate_html(gen, chunk_root->root_->children());
        writer.write_file(chunk_root->path_, gen.html);
        for (chunk* it = chunk_root->children(); it; it = it->next())
        {
            generate_chunks_impl(writer, it);
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
        int count;

        chunk_builder() : count(0) {}

        std::string next_path_name() {
            ++count;
            std::string result = "page-";
            result += boost::lexical_cast<std::string>(count);
            ++count;
            return result;
        }
    };

    void chunk_nodes(chunk_builder& builder, xml_tree_builder& tree, xml_element* node);

    chunk* chunk_document(xml_tree_builder& tree) {
        chunk_builder builder;
        chunk_nodes(builder, tree, tree.root_);
        return builder.release();
    }

    void chunk_nodes(chunk_builder& builder, xml_tree_builder& tree, xml_element* node) {
        chunk* parent = builder.parent_;

        for (xml_element* it = node; it;) {
            if (parent && it->type_ == xml_element::element_node && it->name_ == "title")
            {
                parent->title_ = it;
                it = tree.extract(it);
            }
            else if (parent && it->type_ == xml_element::element_node && chunkinfo_types.find(it->name_) != chunkinfo_types.end())
            {
                parent->info_ = it;
                it = tree.extract(it);
            }
            else if (it->type_ == xml_element::element_node && chunk_types.find(it->name_) != chunk_types.end()) {
                chunk* chunk_node = new chunk();
                chunk_node->root_ = it;
                std::string* id = it->get_attribute("id");
                chunk_node->id_ = id ? *id : builder.next_path_name();
                chunk_node->path_ = id_to_path(chunk_node->id_);
                it = tree.extract(it);
                builder.add_element(chunk_node);
                builder.start_children();
                chunk_nodes(builder, tree, chunk_node->root_->children());
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

    void tag(html_gen& gen, quickbook::string_view name, xml_element* x) {
        open_tag_with_id(gen, name, x);
        document(gen, x->children());
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
                else {
                    std::cout << "Unsupported tag: " << x->name_ << std::endl;
                    if (x->children()) { document(gen, x->children()); }
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
        tag(gen, BOOST_PP_STRINGIZE(html_name), x); \
    }

#define NODE_MAP_CLASS(tag_name, html_name, class_name) \
    NODE_RULE(tag_name, gen, x) { \
        tag_start_with_id(gen, BOOST_PP_STRINGIZE(html_name), x); \
        tag_attribute(gen, "class", BOOST_PP_STRINGIZE(class_name)); \
        tag_end(gen); \
        document(gen, x->children()); \
        close_tag(gen, BOOST_PP_STRINGIZE(html_name)); \
    }


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
    NODE_MAP(programlisting, pre)
    NODE_MAP(literal, tt)
    NODE_MAP(subscript, sub)
    NODE_MAP(superscript, sup)
    NODE_MAP(section, div)

    // TODO: Header levels
    NODE_MAP(title, h3)
    NODE_MAP(bridgehead, h3)

    NODE_MAP_CLASS(note, div, note)
    NODE_MAP_CLASS(tip, div, tip)
    NODE_MAP_CLASS(sidebar, div, sidebar)

    NODE_RULE(ulink, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("url");

        tag_start_with_id(gen, "a", x);
        if (value) { tag_attribute(gen, "href", *value); }
        tag_end(gen);
        document(gen, x->children());
        close_tag(gen, "a");
    }

    NODE_RULE(link, gen, x) {
        // TODO: error if missing?
        std::string* value = x->get_attribute("linkend");

        id_paths_type::const_iterator it = gen.id_paths.end();
        if (value) { it = gen.id_paths.find(*value); }

        tag_start_with_id(gen, "a", x);
        if (it != gen.id_paths.end()) {
            tag_attribute(gen, "href", relative_path_from(it->second, gen.path));
        }
        tag_end(gen);
        document(gen, x->children());
        close_tag(gen, "a");
    }

    NODE_RULE(phrase, gen, x) {
        std::string* value = x->get_attribute("role");

        tag_start_with_id(gen, "span", x);
        if (value) { tag_attribute(gen, "class", *value); }
        tag_end(gen);
        document(gen, x->children());
        close_tag(gen, "span");
    }

    NODE_RULE(emphasis, gen, x) {
        std::string* value = x->get_attribute("role");
        quickbook::string_view tag_name = "em";
        if (value && (*value == "bold" || *value == "strong")) {
            tag_name = "strong";
        }
        // TODO: Error on unrecognized role + case insensitive
        return tag(gen, tag_name, x);
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
            tag_start_with_id(gen, "img", x);
            tag_attribute(gen, "src", *image);
            tag_attribute(gen, "alt", alt);
            tag_end_self_close(gen);
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
            close_tag(gen, "dl");
        }
    }

    void write_table_rows(html_gen& gen, xml_element* x, char const* td_tag) {
        for(xml_element* i = x->children(); i; i = i->next()) {
            if (i->type_ == xml_element::element_node && i->name_ == "row") {
                open_tag_with_id(gen, "tr", i);
                for(xml_element* j = i->children(); j; j = j->next()) {
                    if (j->type_ == xml_element::element_node && j->name_ == "entry") {
                        open_tag_with_id(gen, td_tag, j);
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

        open_tag_with_id(gen, "table", x);
        if (title) { tag(gen, "caption", title); }
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
        return result;
    }
}}
