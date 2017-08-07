/*=============================================================================
    Copyright (c) 2017 Daniel James

    Use, modification and distribution is subject to the Boost Software
    License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
    http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP)
#define BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP

#include <string>
#include <boost/filesystem/path.hpp>
#include "string_view.hpp"

namespace quickbook
{
    namespace detail
    {
        struct boostbook_parse_error
        {
            char const* message;
            string_iterator pos;

            boostbook_parse_error(char const* m, string_iterator p)
                : message(m), pos(p)
            {
            }
        };

        struct html_options
        {
            bool chunked_output;
            boost::filesystem::path output_path;
            boost::filesystem::path css_path;
            boost::filesystem::path graphics_path;

            html_options() : chunked_output(false) {}
        };

        int boostbook_to_html(quickbook::string_view, html_options const&);
    }
}

#endif // BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP
