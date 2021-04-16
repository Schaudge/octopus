// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "compression.hpp"

#include <sstream>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/copy.hpp>

namespace octopus { namespace utils {

std::string compress(const std::string& data)
{
    std::stringstream decompressed {data};
    boost::iostreams::filtering_streambuf<boost::iostreams::input> stream;
    stream.push(boost::iostreams::zlib_compressor());
    stream.push(decompressed);
    std::stringstream compressed {};
    boost::iostreams::copy(stream, compressed);
    return compressed.str();
}

std::string decompress(const std::string& data)
{
    std::stringstream compressed {data};
    boost::iostreams::filtering_streambuf<boost::iostreams::input> stream;
    stream.push(boost::iostreams::zlib_decompressor());
    stream.push(compressed);
    std::stringstream decompressed;
    boost::iostreams::copy(stream, decompressed);
    return decompressed.str();
}

std::string Compress::operator()(const std::string str) const
{
    return compress(str);
}

std::string Decompress::operator()(const std::string str) const
{
    return decompress(str);
}

} // namespace utils
} // namespace octopus
