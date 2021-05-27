// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "malformed_file_error.hpp"

#include <utility>
#include <sstream>
#include <algorithm>
#include <iterator>

#include <boost/optional.hpp>
#include <boost/filesystem/operations.hpp>

namespace octopus {

MalformedFileError::MalformedFileError(Path file) : file_ {std::move(file)}
{}

MalformedFileError::MalformedFileError(Path file, std::string required_type)
: file_ {std::move(file)}
, valid_types_ {std::move(required_type)}
{}

MalformedFileError::MalformedFileError(Path file, std::vector<std::string> valid_types)
: file_ {std::move(file)}
, valid_types_ {std::move(valid_types)}
{}

void MalformedFileError::set_reason(std::string reason) noexcept
{
    reason_ = std::move(reason);
}

void MalformedFileError::set_location_specified(std::string location) noexcept
{
    location_ = std::move(location);
}

boost::optional<std::string> get_type(const boost::filesystem::path& file)
{
    auto extension = file.extension().string();
    
    if (extension.empty()) return boost::none;
    
    extension.erase(extension.begin());
    
    if (extension == "bam") {
        return std::string {"bam"};
    } else if (extension == "cram") {
        return std::string {"cram"};
    } else if (extension == "bai") {
        return std::string {"bam index"};
    } else if (extension == "crai") {
        return std::string {"cram index"};
    } else if (extension == "fa") {
        return std::string {"fasta"};
    } else if (extension == "fasta") {
        return std::string {"fasta"};
    } else if (extension == "fai") {
        return std::string {"fasta index"};
    } else if (extension == "vcf") {
        return std::string {"vcf"};
    } else if (extension == "bcf") {
        return std::string {"bcf"};
    } else {
        return boost::none;
    }
}

std::string MalformedFileError::do_why() const
{
    std::ostringstream ss {};
    const auto type = get_type(file_);
    ss << "the ";
    if (type) {
        ss << *type << ' ';
    }
    ss << "file you specified " << file_ << ' ';
    if (boost::filesystem::is_symlink(file_)) {
        ss << '(' << boost::filesystem::read_symlink(file_) << ") ";
    }
    if (location_) {
        ss << "in " << *location_ << ' ';
    }
    if (valid_types_.empty()) {
        if (reason_) {
            ss << "is malformed because " << *reason_;
        } else {
            ss << "is malformed or corrupted";
        }
    } else if (valid_types_.size() == 1) {
        ss << "is not a valid " << valid_types_.front() << " file";
    } else if (valid_types_.size() == 2) {
        ss << "is not a valid " << valid_types_.front() << " or " << valid_types_.back() << " file";
    } else {
        ss << "is not a valid format (from: ";
        std::copy(std::cbegin(valid_types_), std::prev(std::cend(valid_types_)),
                  std::ostream_iterator<std::string> {ss, "; "});
        ss << valid_types_.back() << ')';
    }
    return ss.str();
}

std::string MalformedFileError::do_help() const
{
    if (!valid_types_.empty()) {
        return "check the file is not corrupted";
    }
    return "check you did not mistake the command line option";
}

} // namespace octopus
