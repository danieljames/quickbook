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
#include <boost/lexical_cast.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include "simple_parse.hpp"
#include "stream.hpp"

namespace quickbook
{
    namespace fs = boost::filesystem;
}

namespace quickbook
{
    namespace detail
    {
        struct xml_element
        {
            enum element_type
            {
                element_root,
                element_node,
                element_text,
                element_chunk
            } type_;
            std::string name_;
            std::vector<std::pair<std::string, std::string> > attributes_;
            xml_element* parent_;
            xml_element* children_;
            xml_element* next_;
            xml_element* prev_;
            std::string contents_;

            explicit xml_element(element_type n)
                : type_(n)
                , name_()
                , attributes_()
                , parent_()
                , children_()
                , next_()
                , prev_()
            {
            }

            explicit xml_element(element_type n, quickbook::string_view name)
                : type_(n)
                , name_(name.begin(), name.end())
                , attributes_()
                , parent_()
                , children_()
                , next_()
                , prev_()
            {
            }

            static xml_element* text_node(quickbook::string_view x)
            {
                xml_element* n = new xml_element(element_text);
                n->contents_.assign(x.begin(), x.end());
                return n;
            }

            static xml_element* node(quickbook::string_view x)
            {
                return new xml_element(element_node, x);
            }

            std::string* get_attribute(quickbook::string_view name)
            {
                for (std::vector<std::pair<std::string, std::string> >::iterator
                         it = attributes_.begin(),
                         end = attributes_.end();
                     it != end; ++it) {
                    if (name == it->first) {
                        return &it->second;
                    }
                }
                return 0;
            }

            xml_element* extract()
            {
                xml_element* next = next_;
                if (parent_ && !prev_) {
                    parent_->children_ = next;
                }
                if (prev_) {
                    prev_->next_ = next_;
                }
                if (next_) {
                    next_->prev_ = prev_;
                }
                return next_;
            }
        };

        struct xml_tree_builder
        {
            xml_element* root_;
            xml_element* current_;
            xml_element* parent_;

            xml_tree_builder()
                : root_(new xml_element(xml_element::element_root))
                , current_(0)
                , parent_(root_)
            {
            }

            void add_text_node(quickbook::string_view x)
            {
                add_element(xml_element::text_node(x));
            }

            xml_element* add_node(quickbook::string_view name)
            {
                xml_element* node = xml_element::node(name);
                add_element(node);
                return node;
            }

            void add_element(xml_element* n)
            {
                n->parent_ = parent_;
                n->prev_ = current_;
                if (current_) {
                    current_->next_ = n;
                }
                else {
                    parent_->children_ = n;
                }
                current_ = n;
            }

            void start_children()
            {
                parent_ = current_;
                current_ = 0;
            }

            void end_children()
            {
                current_ = parent_;
                parent_ = current_->parent_;
            }
        };

        struct xml_chunk : xml_element
        {
            xml_element* title_;
            xml_element* root_;
            std::string path_;

            xml_chunk() : xml_element(element_chunk), title_(), root_() {}
        };

        std::string generate_html(xml_element*);
        xml_element* chunk_document(xml_element*, fs::path const&);

        quickbook::string_view read_string(
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator end)
        {
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

        void skip_question_mark_tag(
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
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

        void skip_exclamation_mark_tag(
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
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

        quickbook::string_view read_tag_name(
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
            read_some_of(it, end, " \t\n\r");
            quickbook::string_view::iterator name_start = it;
            read_some_of(
                it, end,
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ:-");
            if (name_start == it) {
                throw boostbook_parse_error("Invalid tag", start);
            }
            return quickbook::string_view(name_start, it - name_start);
        }

        quickbook::string_view read_attribute_value(
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
            read_some_of(it, end, " \t\n\r");
            if (*it == '"' || *it == '\'') {
                return read_string(it, end);
            }
            else {
                throw boostbook_parse_error("Invalid tag", start);
            }
        }

        void read_tag(
            xml_tree_builder& builder,
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
            assert(it == start + 1 && it != end);
            quickbook::string_view name = read_tag_name(it, start, end);
            xml_element* node = builder.add_node(name);

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
                quickbook::string_view attribute_name =
                    read_tag_name(it, start, end);
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
                    std::string(
                        attribute_value.begin(), attribute_value.end())));
            }
        }

        void read_close_tag(
            xml_tree_builder& builder,
            quickbook::string_view::iterator& it,
            quickbook::string_view::iterator start,
            quickbook::string_view::iterator end)
        {
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

        void generate_chunks(xml_element* root, fs::path const& root_path);

        int boostbook_to_html(
            quickbook::string_view source,
            boost::filesystem::path const& fileout_)
        {
            typedef quickbook::string_view::const_iterator iterator;
            iterator it = source.begin(), end = source.end();

            xml_tree_builder builder;

            while (true) {
                iterator start = it;
                read_to(it, end, '<');
                if (start != it) {
                    builder.add_text_node(
                        quickbook::string_view(start, it - start));
                }

                if (it == end) {
                    break;
                }
                start = it++;
                if (it == end) {
                    throw boostbook_parse_error("Invalid tag", start);
                }

                switch (*it) {
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

            xml_element* chunked = chunk_document(builder.root_, fileout_);
            generate_chunks(chunked, fileout_.parent_path());
            return 0;
        }

        struct chunk_writer
        {
            fs::path const& root_path;

            explicit chunk_writer(fs::path const& r) : root_path(r) {}
            void write_file(
                std::string const& generic_path, std::string const& content)
            {
                fs::path path = root_path / generic_to_path(generic_path);
                fs::ofstream fileout(path);

                if (fileout.fail()) {
                    ::quickbook::detail::outerr()
                        << "Error opening output file " << generic_path
                        << std::endl;

                    return /*1*/;
                }

                fileout << content;

                if (fileout.fail()) {
                    ::quickbook::detail::outerr()
                        << "Error writing to output file " << generic_path
                        << std::endl;

                    return /*1*/;
                }
            }
        };

        void generate_chunks_impl(chunk_writer&, xml_element*);

        void generate_chunks(xml_element* root, fs::path const& root_path)
        {
            chunk_writer writer(root_path);
            generate_chunks_impl(writer, root);
        }

        void generate_chunks_impl(chunk_writer& writer, xml_element* chunk_root)
        {
            std::string output;
            for (xml_chunk* it = static_cast<xml_chunk*>(chunk_root->children_);
                 it; it = static_cast<xml_chunk*>(it->next_)) {
                output = generate_html(it->root_->children_);
                writer.write_file(it->path_, output);
                generate_chunks_impl(writer, it);
            }
        }

        // Chunker

        boost::unordered_set<std::string> chunk_types;

        static struct init_chunk_type
        {
            init_chunk_type()
            {
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
            }
        } init_chunk;

        struct xml_chunk_builder : xml_tree_builder
        {
            std::string path;
            std::string extension;
            int count;

            xml_chunk_builder(std::string const& p, std::string const& e)
                : path(p), extension(e), count(0)
            {
            }

            std::string next_path_name()
            {
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

        void chunk(xml_chunk_builder& builder, xml_element* node);

        xml_element* chunk_document(xml_element* root, fs::path const& p)
        {
            xml_chunk_builder builder(
                path_to_generic(p.stem()), path_to_generic(p.extension()));
            chunk(builder, root);
            return builder.root_;
        }

        void chunk(xml_chunk_builder& builder, xml_element* node)
        {
            xml_chunk* parent =
                builder.parent_ &&
                        builder.parent_->type_ == xml_element::element_chunk
                    ? static_cast<xml_chunk*>(builder.parent_)
                    : 0;

            for (xml_element* it = node->children_; it;) {
                if (parent && it->type_ == xml_element::element_node &&
                    it->name_ == "title") {
                    parent->title_ = it;
                    it = it->extract();
                }
                else if (
                    it->type_ == xml_element::element_node &&
                    chunk_types.find(it->name_) != chunk_types.end()) {
                    xml_chunk* chunk_node = new xml_chunk();
                    chunk_node->root_ = it;
                    chunk_node->path_ = builder.next_path_name();
                    it = it->extract();
                    builder.add_element(chunk_node);
                    builder.start_children();
                    chunk(builder, chunk_node->root_);
                    builder.end_children();
                }
                else {
                    it = it->next_;
                }
            }
        }

        // HTML generator

        struct html_gen
        {
            std::string html;
        };

        void document(html_gen&, xml_element*);
        typedef void (*node_parser)(html_gen&, xml_element*);
        typedef boost::unordered_map<quickbook::string_view, node_parser>
            node_parsers_type;
        static node_parsers_type node_parsers;

        void open_tag(html_gen& gen, quickbook::string_view name)
        {
            gen.html += "<";
            gen.html.append(name.begin(), name.end());
            gen.html += ">";
        }

        void close_tag(html_gen& gen, quickbook::string_view name)
        {
            gen.html += "</";
            gen.html.append(name.begin(), name.end());
            gen.html += ">";
        }

        void tag(
            html_gen& gen, quickbook::string_view name, xml_element* children)
        {
            open_tag(gen, name);
            document(gen, children);
            close_tag(gen, name);
        }

        void document(html_gen& gen, xml_element* x)
        {
            for (; x; x = x->next_) {
                switch (x->type_) {
                case xml_element::element_text: {
                    gen.html += x->contents_;
                    break;
                }
                case xml_element::element_node: {
                    node_parsers_type::iterator it =
                        node_parsers.find(x->name_);
                    if (it != node_parsers.end()) {
                        it->second(gen, x);
                    }
                    else if (x->children_) {
                        document(gen, x->children_);
                    }
                    break;
                }
                default:
                    assert(false);
                }
            }
        }

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
    NODE_RULE(tag_name, gen, x)                                                \
    {                                                                          \
        tag(gen, BOOST_PP_STRINGIZE(html_name), x->children_);                 \
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

        NODE_RULE(section, gen, x)
        {
            std::string* value = x->get_attribute("id");

            gen.html += "<div";
            if (value) {
                gen.html += " id = \"";
                gen.html += *value;
                gen.html += "\"";
            }
            gen.html += ">";
            document(gen, x->children_);
            gen.html += "</div>";
        }

        NODE_RULE(ulink, gen, x)
        {
            // TODO: error if missing?
            std::string* value = x->get_attribute("url");

            gen.html += "<a";
            if (value) {
                gen.html += " href = \"";
                gen.html += *value;
                gen.html += "\"";
            }
            gen.html += ">";
            document(gen, x->children_);
            gen.html += "</a>";
        }

        NODE_RULE(link, gen, x)
        {
            gen.html += "<span class=\"link\">";
            document(gen, x->children_);
            gen.html += "</span>";
        }

        NODE_RULE(phrase, gen, x)
        {
            std::string* value = x->get_attribute("role");

            gen.html += "<span";
            if (value) {
                gen.html += " class = \"";
                gen.html += *value;
                gen.html += "\"";
            }
            gen.html += ">";
            document(gen, x->children_);
            gen.html += "</span>";
        }

        NODE_RULE(emphasis, gen, x)
        {
            std::string* value = x->get_attribute("role");
            quickbook::string_view tag_name = "em";
            if (value && (*value == "bold" || *value == "strong")) {
                tag_name = "strong";
            }
            // TODO: Error on unrecognized role + case insensitive
            return tag(gen, tag_name, x->children_);
        }

        NODE_RULE(inlinemediaobject, gen, x)
        {
            std::string* image;

            // Get image link
            for (xml_element* i = x->children_; i; i = i->next_) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "imageobject") {
                    for (xml_element* j = i->children_; j; j = j->next_) {
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
            for (xml_element* i = x->children_; i; i = i->next_) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "textobject") {
                    for (xml_element* j = i->children_; j; j = j->next_) {
                        if (j->type_ == xml_element::element_node &&
                            j->name_ == "pharse") {
                            std::string* role = j->get_attribute("role");
                            if (role && *role == "alt") {
                                alt = generate_html(j->children_);
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
                gen.html += "<img src=\"";
                gen.html += *image;
                gen.html += "\" alt=\"";
                gen.html += alt;
                gen.html += "\">";
            }
        }

        NODE_RULE(variablelist, gen, x)
        {
            typedef std::vector<std::pair<xml_element*, xml_element*> >
                items_type;
            items_type items;
            for (xml_element* i = x->children_; i; i = i->next_) {
                if (i && i->type_ == xml_element::element_node) {
                    if (i->name_ == "title") {
                        // TODO: What to do with titles?
                        continue;
                    }
                    else if (i->name_ == "varlistentry") {
                        xml_element* term = 0;
                        xml_element* listitem = 0;
                        for (xml_element* j = i->children_; j; j = j->next_) {
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
                open_tag(gen, "dl");
                for (items_type::iterator i = items.begin(); i != items.end();
                     ++i) {
                    open_tag(gen, "dt");
                    document(gen, i->first->children_);
                    close_tag(gen, "dt");
                    open_tag(gen, "dd");
                    document(gen, i->second->children_);
                    close_tag(gen, "dd");
                }
                close_tag(gen, "dl");
            }
        }

        void write_table_rows(html_gen& gen, xml_element* x, char const* td_tag)
        {
            for (xml_element* i = x->children_; i; i = i->next_) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "row") {
                    open_tag(gen, "tr");
                    for (xml_element* j = i->children_; j; j = j->next_) {
                        if (j->type_ == xml_element::element_node &&
                            j->name_ == "entry") {
                            open_tag(gen, td_tag);
                            document(gen, j->children_);
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

            for (xml_element* i = x->children_; i; i = i->next_) {
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

            for (xml_element* i = tgroup->children_; i; i = i->next_) {
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "thead") {
                    thead = i;
                }
                if (i->type_ == xml_element::element_node &&
                    i->name_ == "tbody") {
                    tbody = i;
                }
            }

            open_tag(gen, "table");
            if (title) {
                open_tag(gen, "caption");
                document(gen, title->children_);
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

        std::string generate_html(xml_element* x)
        {
            html_gen gen;
            document(gen, x);
            return gen.html;
        }
    }
}
