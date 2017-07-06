/*=============================================================================
    Copyright (c) 2017 Daniel James

    Use, modification and distribution is subject to the Boost Software
    License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
    http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#if !defined(BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP)
#define BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP

#include <string>
#include "string_view.hpp"

namespace quickbook
{
    namespace detail
    {
        struct boostbook_parse_error
        {
            boostbook_parse_error(char const*, quickbook::string_view::iterator)
            {
            }
        };
        std::string boostbook_to_html(quickbook::string_view);
    }
}

#endif // BOOST_QUICKBOOK_BOOSTBOOK_TO_HTML_HPP
