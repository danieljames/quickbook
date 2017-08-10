/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_BOOSTBOOK_CHUNKER_HPP)
#define BOOST_QUICKBOOK_BOOSTBOOK_CHUNKER_HPP

#include "xml_parse.hpp"

namespace quickbook { namespace detail {
    struct chunk : tree_node<chunk> {
        xml_element* title_;
        xml_element* info_;
        xml_element* root_;
        bool inline_;
        std::string id_;
        std::string path_;

        explicit chunk(xml_element* root) : title_(), info_(), root_(root), inline_(false) {}

        ~chunk() {
            delete_nodes(title_);
            delete_nodes(info_);
            delete_nodes(root_);
        }
    };

    tree<chunk> chunk_document(xml_tree_builder&);
    void inline_sections(chunk*, int depth);
    void inline_chunks(chunk*);
}}

#endif