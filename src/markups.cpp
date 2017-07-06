/*=============================================================================
    Copyright (c) 2002 2004 2006 Joel de Guzman
    Copyright (c) 2004 Eric Niebler
    http://spirit.sourceforge.net/

    Use, modification and distribution is subject to the Boost Software
    License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
    http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

#include "quickbook.hpp"
#include "markups.hpp"
#include "block_tags.hpp"
#include "phrase_tags.hpp"
#include <boost/foreach.hpp>
#include <ostream>
#include <map>

namespace quickbook
{
    namespace detail
    {
        std::map<value::tag_type, markup> markups[markup::format_end];

        void initialise_markups()
        {
            markup init_markups[] = {
                { block_tags::paragraph, "<para>\n", "</para>\n" },
                { block_tags::paragraph_in_list, "<simpara>\n", "</simpara>\n" },
                { block_tags::blurb, "<sidebar role=\"blurb\">\n", "</sidebar>\n" },
                { block_tags::blockquote, "<blockquote>", "</blockquote>" },
                { block_tags::preformatted, "<programlisting>", "</programlisting>" },
                { block_tags::warning, "<warning>", "</warning>" },
                { block_tags::caution, "<caution>", "</caution>" },
                { block_tags::important, "<important>", "</important>" },
                { block_tags::note, "<note>", "</note>" },
                { block_tags::tip, "<tip>", "</tip>" },
                { block_tags::block, "", "" },
                { block_tags::ordered_list, "<orderedlist>", "</orderedlist>" },
                { block_tags::itemized_list, "<itemizedlist>", "</itemizedlist>" },
                { block_tags::hr, "<para/>", 0 },
                { phrase_tags::url, "<ulink url=\"", "</ulink>" },
                { phrase_tags::link, "<link linkend=\"", "</link>" },
                { phrase_tags::funcref, "<functionname alt=\"", "</functionname>" },
                { phrase_tags::classref, "<classname alt=\"", "</classname>" },
                { phrase_tags::memberref, "<methodname alt=\"", "</methodname>" },
                { phrase_tags::enumref, "<enumname alt=\"", "</enumname>" },
                { phrase_tags::macroref, "<macroname alt=\"", "</macroname>" },
                { phrase_tags::headerref, "<headername alt=\"", "</headername>" },
                { phrase_tags::conceptref, "<conceptname alt=\"", "</conceptname>" },
                { phrase_tags::globalref, "<globalname alt=\"", "</globalname>" },
                { phrase_tags::bold, "<emphasis role=\"bold\">", "</emphasis>" },
                { phrase_tags::italic, "<emphasis>", "</emphasis>" },
                { phrase_tags::underline, "<emphasis role=\"underline\">", "</emphasis>" },
                { phrase_tags::teletype, "<literal>", "</literal>" },
                { phrase_tags::strikethrough, "<emphasis role=\"strikethrough\">", "</emphasis>" },
                { phrase_tags::quote, "<quote>", "</quote>" },
                { phrase_tags::replaceable, "<replaceable>", "</replaceable>" },
                { phrase_tags::escape, "<!--quickbook-escape-prefix-->", "<!--quickbook-escape-postfix-->" },
                { phrase_tags::break_mark, "<sbr/>\n", 0 }
            };

            BOOST_FOREACH(markup m, init_markups)
            {
                markups[markup::boostbook][m.tag] = m;
            }

            markup init_html_markups[] = {
                { block_tags::paragraph, "<p>\n", "</p>\n" },
                { block_tags::paragraph_in_list, "<p>\n", "</p>\n" },
                { block_tags::blurb, "<div class=\"blurb\">\n", "</div>\n" },
                { block_tags::blockquote, "<blockquote>", "</blockquote>" },
                { block_tags::preformatted, "<pre>", "</pre>" },
                { block_tags::warning, "<div class=\"warning\">", "</div>" },
                { block_tags::caution, "<div class=\"caution\">", "</div>" },
                { block_tags::important, "<div class=\"important\">", "</div>" },
                { block_tags::note, "<div class=\"note\">", "</div>" },
                { block_tags::tip, "<div class=\"tip\">", "</div>" },
                { block_tags::block, "", "" },
                { block_tags::ordered_list, "<ol>", "</ol>" },
                { block_tags::itemized_list, "<ul>", "</ul>" },
                { block_tags::hr, "<hr/>", 0 },
                { phrase_tags::url, "<a href=\"", "</a>" },
                { phrase_tags::link, "<a href=\"#", "</a>" },
                { phrase_tags::funcref, "<functionname alt=\"", "</functionname>" },
                { phrase_tags::classref, "<classname alt=\"", "</classname>" },
                { phrase_tags::memberref, "<methodname alt=\"", "</methodname>" },
                { phrase_tags::enumref, "<enumname alt=\"", "</enumname>" },
                { phrase_tags::macroref, "<macroname alt=\"", "</macroname>" },
                { phrase_tags::headerref, "<headername alt=\"", "</headername>" },
                { phrase_tags::conceptref, "<conceptname alt=\"", "</conceptname>" },
                { phrase_tags::globalref, "<globalname alt=\"", "</globalname>" },
                { phrase_tags::bold, "<b>", "</b>" },
                { phrase_tags::italic, "<em>", "</em>" },
                { phrase_tags::underline, "<span style=\"text-decoration: underline\">", "</span>" },
                { phrase_tags::teletype, "<code>", "</code>" },
                { phrase_tags::strikethrough, "<span style=\"text-decoration: line-through\">", "</span>" },
                { phrase_tags::quote, "<q>", "</q>" },
                { phrase_tags::replaceable, "<i class=\"replaceable\">", "</i>" },
                { phrase_tags::escape, "<!--quickbook-escape-prefix-->", "<!--quickbook-escape-postfix-->" },
                { phrase_tags::break_mark, "<br/>\n", 0 }
            };

            BOOST_FOREACH(markup m, init_html_markups)
            {
                markups[markup::html][m.tag] = m;
            }
        }

        markup const& get_markup(markup::format f, value::tag_type t)
        {
            return markups[f][t];
        }

        std::ostream& operator<<(std::ostream& out, markup const& m)
        {
            return out<<"{"<<m.tag<<": \""<<m.pre<<"\", \""<<m.post<<"\"}";
        }
    }
}
