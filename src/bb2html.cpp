/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "bb2html.hpp"
#include "simple_parse.hpp"
#include <vector>
#include <cassert>

namespace quickbook { namespace detail {
    struct xml_element {
        enum element_type { element_root, element_node, element_text } type_;
        std::string name_;
        std::vector<std::pair<std::string, std::string> > attributes_;
        xml_element* parent_;
        xml_element* children_;
        xml_element* next_;
        std::string contents_;

        explicit xml_element(element_type n) :
            type_(n),
            name_(),
            attributes_(),
            parent_(),
            children_(),
            next_() {}

        explicit xml_element(element_type n, quickbook::string_view name) :
            type_(n),
            name_(name.begin(), name.end()),
            parent_(),
            attributes_(),
            children_(),
            next_() {}

        static xml_element* text_node(quickbook::string_view x) {
            xml_element* n = new xml_element(element_text);
            n->contents_.assign(x.begin(), x.end());
            return n;
        }

        static xml_element* node(quickbook::string_view x) {
            return new xml_element(element_node, x);
        }
    };

    struct xml_tree_builder {
        xml_element* root_;
        xml_element* current_;
        xml_element* parent_;

        xml_tree_builder() :
            root_(new xml_element(xml_element::element_root, 0)),
            current_(0),
            parent_(root_) {}

        void add_text_node(quickbook::string_view x) {
            add_node(xml_element::text_node(x));
        }

        void add_node(xml_element* n) {
            n->parent_ = parent_;
            if (current_) {
                current_->next_ = n;
            }
            else {
                parent_->children_ = n;
            }
            current_ = n;
        }

        void start_children() {
            parent_ = current_;
            current_ = 0;
        }

        void end_children() {
            current_ = parent_;
            parent_ = current_->parent_;
        }
    };

    void read_string(quickbook::string_view::iterator& it, quickbook::string_view::iterator end) {
        assert(it != end && (*it == '"' || *it == '\''));

        quickbook::string_view::iterator start = it;
        char deliminator = *it;
        ++it;
        read_to(it, end, deliminator);
        if (it == end) {
            throw boostbook_parse_error("Invalid string", start);
        }
        ++it;
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
            default:
                throw boostbook_parse_error("Invalid tag", start);
            }
        }
    }

    quickbook::string_view read_tag_name(quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        read_some_of(it, end, " \t\n\r");
        quickbook::string_view::iterator name_start = it;
        read_some_of(it, end, "abcdefghijklmnopqrstuvwzyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        if (name_start == it) {
            throw boostbook_parse_error("Invalid tag", start);
        }
        return quickbook::string_view(name_start, it - name_start);
    }

    void read_tag(xml_tree_builder& builder, quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end);
        quickbook::string_view name = read_tag_name(it, start, end);
    }

    void read_close_tag(xml_tree_builder& builder, quickbook::string_view::iterator& it, quickbook::string_view::iterator start, quickbook::string_view::iterator end) {
        assert(it == start + 1 && it != end && *it == '/');
        quickbook::string_view name = read_tag_name(it, start, end);
    }

    std::string boostbook_to_html(quickbook::string_view source) {
        typedef quickbook::string_view::const_iterator iterator;
        iterator it = source.begin(), end = source.end();

        xml_tree_builder builder;

        while (true) {
            iterator start = it;
            read_to_one_of(it, end, "<");
            if (start != it) {
                builder.add_text_node(quickbook::string_view(start, it - start));
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

        return "";
    }
}}
