// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef vcf_reader_hpp
#define vcf_reader_hpp

#include <vector>
#include <string>
#include <cstddef>
#include <memory>
#include <mutex>
#include <iterator>
#include <typeindex>
#include <utility>
#include <functional>

#include <boost/filesystem.hpp>

#include "vcf_reader_impl.hpp"

namespace octopus {

class VcfHeader;
class VcfRecord;
class GenomicRegion;

class VcfReader
{
public:
    using Path = boost::filesystem::path;
    using UnpackPolicy = IVcfReaderImpl::UnpackPolicy;
    using RecordContainer = IVcfReaderImpl::RecordContainer;
    
    class RecordIterator;
    using RecordIteratorPair = std::pair<RecordIterator, RecordIterator>;
    
    VcfReader() = default;
    
    VcfReader(Path file_path);
    
    VcfReader(const VcfReader&)            = delete;
    VcfReader& operator=(const VcfReader&) = delete;
    VcfReader(VcfReader&&);
    VcfReader& operator=(VcfReader&&);
    
    ~VcfReader() = default;
    
    friend void swap(VcfReader& lhs, VcfReader& rhs) noexcept;
    
    bool is_open() const noexcept;
    void open() noexcept;
    void close() noexcept;
    
    const Path& path() const noexcept;
    
    VcfHeader fetch_header() const;
    
    std::size_t count_records() const;
    std::size_t count_records(const std::string& contig) const;
    std::size_t count_records(const GenomicRegion& region) const;
    
    RecordContainer fetch_records(UnpackPolicy level = UnpackPolicy::all) const;
    RecordContainer fetch_records(const std::string& contig, UnpackPolicy level = UnpackPolicy::all) const;
    RecordContainer fetch_records(const GenomicRegion& region, UnpackPolicy level = UnpackPolicy::all) const;
    
    RecordIteratorPair iterate(UnpackPolicy level = UnpackPolicy::all) const;
    RecordIteratorPair iterate(const std::string& contig, UnpackPolicy level = UnpackPolicy::all) const;
    RecordIteratorPair iterate(const GenomicRegion& region, UnpackPolicy level = UnpackPolicy::all) const;
    
private:
    Path file_path_;
    std::unique_ptr<IVcfReaderImpl> reader_;
    
    mutable std::mutex mutex_;
};

bool operator==(const VcfReader& lhs, const VcfReader& rhs);

class VcfReader::RecordIterator
{
public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = VcfRecord;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const VcfRecord*;
    using reference         = const VcfRecord&;
    
    RecordIterator(IVcfReaderImpl::RecordIteratorPtr itr);
    
    RecordIterator(const RecordIterator&);
    RecordIterator& operator=(const RecordIterator&) = delete;
    RecordIterator(RecordIterator&&)                 = default;
    RecordIterator& operator=(RecordIterator&&)      = default;
    
    reference operator*() const;
    pointer operator->() const;
    
    RecordIterator& operator++();
    
    friend bool operator==(const RecordIterator& lhs, const RecordIterator& rhs);
    
private:
    IVcfReaderImpl::RecordIteratorPtr itr_;
    std::type_index type_;
};
    
bool operator!=(const VcfReader::RecordIterator& lhs, const VcfReader::RecordIterator& rhs);

} // namespace octopus

namespace std {

template <> struct hash<octopus::VcfReader>
{
    size_t operator()(const octopus::VcfReader& reader) const
    {
        return hash<string>()(reader.path().string());
    }
};

template <> struct hash<reference_wrapper<octopus::VcfReader>>
{
    size_t operator()(reference_wrapper<octopus::VcfReader> reader) const
    {
        return hash<octopus::VcfReader>()(reader);
    }
};

template <> struct hash<reference_wrapper<const octopus::VcfReader>>
{
    size_t operator()(reference_wrapper<const octopus::VcfReader> reader) const
    {
        return hash<octopus::VcfReader>()(reader);
    }
};

} // namespace std

#endif
