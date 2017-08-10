/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_TREE_HPP)
#define BOOST_QUICKBOOK_TREE_HPP

#include <utility>

namespace quickbook
{
    namespace detail
    {
        struct tree_node_base
        {
            friend struct tree_builder_base;

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

        template <typename T> struct tree
        {
          private:
            tree(tree const&);
            tree& operator=(tree const&);

            T* root_;

          public:
            explicit tree(T* r) : root_(r) {}
            tree(tree<T>&& x) : root_(x.root_) { x.root_ = 0; }
            ~tree() { delete_nodes(root()); }

            T* root() const { return static_cast<T*>(root_); }
        };

        struct tree_builder_base
        {
          private:
            tree_builder_base(tree_builder_base const&);
            tree_builder_base& operator=(tree_builder_base const&);

          protected:
            tree_node_base* root_;
            tree_node_base* current_;
            tree_node_base* parent_;

            tree_builder_base();
            tree_builder_base(tree_builder_base&&);
            ~tree_builder_base();

            tree_node_base* extract(tree_node_base*);
            tree_node_base* release();
            void add_element(tree_node_base* n);

          public:
            void start_children();
            void end_children();
        };

        template <typename T> struct tree_builder : tree_builder_base
        {
          public:
            tree_builder() : tree_builder_base() {}
            tree_builder(tree_builder<T>&& x) : tree_builder_base(std::move(x))
            {
            }

            ~tree_builder() { delete_nodes(root()); }

            T* parent() const { return static_cast<T*>(parent_); }
            T* current() const { return static_cast<T*>(current_); }
            T* root() const { return static_cast<T*>(root_); }
            T* extract(T* x)
            {
                return static_cast<T*>(tree_builder_base::extract(x));
            }
            tree<T> release()
            {
                return tree<T>(static_cast<T*>(tree_builder_base::release()));
            }
            void add_element(T* n) { tree_builder_base::add_element(n); }
        };
    }
}

#endif