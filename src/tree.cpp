/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "tree.hpp"
#include <cassert>

namespace quickbook
{
    namespace detail
    {
        tree_base::tree_base() : root_(0) {}
        tree_base::tree_base(tree_node_base* r) : root_(r) {}
        tree_base::~tree_base() {}

        tree_base::tree_base(tree_base&& x) : root_(x.root_) { x.root_ = 0; }

        tree_base& tree_base::operator=(tree_base&& x)
        {
            root_ = x.root_;
            x.root_ = 0;
            return *this;
        }

        tree_node_base* tree_base::extract(tree_node_base* x)
        {
            if (!x->prev_) {
                if (x->parent_) {
                    x->parent_->children_ = x->next_;
                }
                else {
                    assert(x == root_);
                    root_ = x->next_;
                }
            }
            else {
                x->prev_->next_ = x->next_;
            }
            if (x->next_) {
                x->next_->prev_ = x->prev_;
            }
            x->parent_ = 0;
            x->next_ = 0;
            x->prev_ = 0;
            return x;
        }

        tree_builder_base::tree_builder_base()
            : tree_base(), current_(0), parent_(0)
        {
        }

        tree_builder_base::tree_builder_base(tree_builder_base&& x)
            : tree_base(std::move(x)), current_(x.current_), parent_(x.parent_)
        {
            x.current_ = 0;
            x.parent_ = 0;
        }

        tree_builder_base& tree_builder_base::operator=(tree_builder_base&& x)
        {
            root_ = x.root_;
            current_ = x.current_;
            parent_ = x.parent_;
            x.root_ = 0;
            x.current_ = 0;
            x.parent_ = 0;
            return *this;
        }

        // Nodes are deleted in the subclass, which knows their type.
        tree_builder_base::~tree_builder_base() {}

        tree_node_base* tree_builder_base::extract(tree_node_base* x)
        {
            if (!x->prev_) {
                if (x->parent_) {
                    x->parent_->children_ = x->next_;
                }
                else {
                    assert(x == root_);
                    root_ = x->next_;
                    parent_ = 0;
                    current_ = root_;
                }
            }
            else {
                x->prev_->next_ = x->next_;
            }
            if (x->next_) {
                x->next_->prev_ = x->prev_;
            }
            x->parent_ = 0;
            x->next_ = 0;
            x->prev_ = 0;
            return x;
        }

        tree_node_base* tree_builder_base::release()
        {
            tree_node_base* n = root_;
            root_ = 0;
            current_ = 0;
            parent_ = 0;
            return n;
        }

        void tree_builder_base::add_element(tree_node_base* n)
        {
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

        void tree_builder_base::start_children()
        {
            parent_ = current_;
            current_ = 0;
        }

        void tree_builder_base::end_children()
        {
            current_ = parent_;
            parent_ = current_->parent_;
        }
    }
}