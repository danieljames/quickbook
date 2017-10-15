#include "../../src/bb2html.hpp"
#include <iostream>

int main() {
    std::istreambuf_iterator<char> begin(std::cin);
    std::istreambuf_iterator<char> end;
    std::string s(begin, end);

    quickbook::detail::html_options options;
    options.home_path = "output/x.html";

    std::cout << quickbook::detail::boostbook_to_html(s, options) << std::endl;
}
