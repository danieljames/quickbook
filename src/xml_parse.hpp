/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_XML_PARSE_HPP)
#define BOOST_QUICKBOOK_XML_PARSE_HPP

#include <string>
#include <vector>
#include "string_view.hpp"
#include "tree.hpp"

namespace quickbook
{
    namespace detail
    {
        struct xml_element;
        typedef tree<xml_element> xml_tree;
        typedef tree_builder<xml_element> xml_tree_builder;
        struct xml_parse_error;

        struct xml_element : tree_node<xml_element>
        {
            enum element_type
            {
                element_node,
                element_text,
                element_html
            } type_;
            std::string name_;
            std::vector<std::pair<std::string, std::string> > attributes_;
            std::string contents_;

            explicit xml_element(element_type n) : type_(n) {}

            explicit xml_element(element_type n, quickbook::string_view name)
                : type_(n), name_(name.begin(), name.end())
            {
            }

            static xml_element* text_node(quickbook::string_view x)
            {
                xml_element* n = new xml_element(element_text);
                n->contents_.assign(x.begin(), x.end());
                return n;
            }

            static xml_element* html_node(quickbook::string_view x)
            {
                xml_element* n = new xml_element(element_html);
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
        };

        struct xml_parse_error
        {
            char const* message;
            string_iterator pos;

            xml_parse_error(char const* m, string_iterator p)
                : message(m), pos(p)
            {
            }
        };

        void write_xml_tree(xml_element*);
        xml_tree xml_parse(quickbook::string_view);
    }
}

#endif