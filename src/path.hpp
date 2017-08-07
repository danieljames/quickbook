/*=============================================================================
    Copyright (c) 2002 2004 2006 Joel de Guzman
    Copyright (c) 2004 Eric Niebler
    Copyright (c) 2005 Thomas Guest
    Copyright (c) 2013, 2017 Daniel James

    Use, modification and distribution is subject to the Boost Software
    License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
    http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include <boost/filesystem/path.hpp>

namespace quickbook
{
    namespace fs = boost::filesystem;

    // The relative path from base to path
    fs::path path_difference(fs::path const& base, fs::path const& path);

    // Convert a Boost.Filesystem path to a URL.
    std::string file_path_to_url(fs::path const&);
    std::string dir_path_to_url(fs::path const&);
}
