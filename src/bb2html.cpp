/*=============================================================================
Copyright (c) 2017 Daniel James

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "bb2html.hpp"
#include "simple_parse.hpp"

namespace quickbook { namespace detail {
    std::string boostbook_to_html(std::string const& source) {
        return "Convert to html: " + source;
    }
}}
