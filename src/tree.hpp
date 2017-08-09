/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_TREE_HPP)
#define BOOST_QUICKBOOK_TREE_HPP

namespace quickbook
{
    namespace detail
    {
        struct tree_node_base
        {
          protected:
            tree_node_base* parent_;
            tree_node_base* children_;
            tree_node_base* next_;
            tree_node_base* prev_;

          public:
            tree_node_base() : parent_(), children_(), next_(), prev_() {}
        };

        template <typename T> struct tree_node : tree_node_base
        {
            T* parent() const { return static_cast<T*>(parent_); }
            T* children() const { return static_cast<T*>(children_); }
            T* next() const { return static_cast<T*>(next_); }
            T* prev() const { return static_cast<T*>(prev_); }
        };

        template <typename Node> void delete_nodes(Node* n)
        {
            while (n) {
                Node* to_delete = n;
                n = n->next();
                delete_nodes(to_delete->children());
                delete (to_delete);
            }
        }

        template <typename T> struct tree_builder
        {
          private:
            tree_builder(tree_builder const&);
            tree_builder& operator=(tree_builder const&);

          public:
            T* root_;
            T* current_;
            T* parent_;

            tree_builder() : root_(0), current_(0), parent_(0) {}

            ~tree_builder() { delete_nodes(root_); }

            T* extract(T* x)
            {
                T* next = static_cast<T*>(x->next_);
                if (!x->prev_) {
                    if (x->parent_) {
                        x->parent_->children_ = next;
                    }
                    else {
                        assert(x == root_);
                        root_ = next;
                        parent_ = 0;
                        current_ = next;
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
                return next;
            }

            T* release()
            {
                T* n = root_;
                root_ = 0;
                current_ = 0;
                parent_ = 0;
                return n;
            }

            void add_element(T* n)
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

            void start_children()
            {
                parent_ = current_;
                current_ = 0;
            }

            void end_children()
            {
                current_ = parent_;
                parent_ = current_->parent();
            }
        };
    }
}

#endif