// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "string_utils.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/classification.hpp>

namespace octopus { namespace utils {

std::vector<std::string> split(const std::string& str, const char delim) {
    std::vector<std::string> elems;
    elems.reserve(std::count(std::cbegin(str), std::cend(str), delim) + 1);
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string& str, const std::string delims)
{
    std::vector<std::string> elems;
    boost::split(elems, str, boost::is_any_of(delims));
    return elems;
}

std::string join(const std::vector<std::string>& strings, const std::string delim)
{
    return boost::algorithm::join(strings, delim);
}

std::string join(const std::vector<std::string>& strings, const char delim)
{
    const std::array<char, 2> Delim {delim, '\0'};
    return join(strings, Delim.data());
}

bool is_prefix(const std::string& prefix, const std::string& text) noexcept
{
    return text.size() >= prefix.size() && std::equal(std::cbegin(prefix), std::cend(prefix), std::cbegin(text));
}

bool is_suffix(const std::string& suffix, const std::string& text) noexcept
{
    return text.size() >= suffix.size() && std::equal(std::crbegin(suffix), std::crend(suffix), std::crbegin(text));
}

std::size_t length(const char* str)
{
    return std::strlen(str);
}

std::size_t length(const std::string& str)
{
    return str.length();
}

bool find(const std::string& lhs, const std::string& rhs)
{
    return lhs.find(rhs) != std::string::npos;
}

std::string& capitalise(std::string& str) noexcept
{
	std::transform(std::cbegin(str), std::cend(str), std::begin(str), [] (char c) { return std::toupper(c); });
    return str;
}

std::string capitalise(const std::string& str)
{
	std::string result(str.size(), char {});
	std::transform(std::cbegin(str), std::cend(str), std::begin(result), [] (char c) { return std::toupper(c); });
	return result;
}

std::string& capitalise_front(std::string& str) noexcept
{
    if (!str.empty()) str.front() = std::toupper(str.front());
        return str;
}

std::string capitalise_front(const std::string& str)
{
    auto result = str;
    return capitalise_front(result);
}

std::string& to_lower(std::string& str) noexcept
{
	std::transform(std::cbegin(str), std::cend(str), std::begin(str), [] (char c) { return std::tolower(c); });
    return str;
}

std::string to_lower(const std::string& str)
{
	std::string result(str.size(), char {});
	std::transform(std::cbegin(str), std::cend(str), std::begin(result), [] (char c) { return std::tolower(c); });
	return result;
}

std::string& strip_leading_zeroes(std::string& str)
{
    str.erase(0, str.find_first_not_of("0"));
    return str;
}

std::string strip_leading_zeroes(const std::string& str)
{
    auto result = str;
    strip_leading_zeroes(result);
    return result;
}

bool is_vowel(const char c)
{
    static constexpr std::array<char, 5> vowels {'a', 'e', 'i', 'o', 'u'};
    return std::find(std::cbegin(vowels), std::cend(vowels), std::tolower(c)) != std::cend(vowels);
}

bool begins_with_vowel(const std::string& str)
{
    return !str.empty() && is_vowel(str.front());
}

} // namespace utils
} // namespace octopus
