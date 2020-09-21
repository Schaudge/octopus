// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef vcf_header_hpp
#define vcf_header_hpp

#include <string>
#include <vector>
#include <unordered_map>
#include <ostream>
#include <functional>

#include "concepts/equitable.hpp"
#include "concepts/comparable.hpp"
#include "vcf_type.hpp"

namespace octopus {

/*
 There are two types of header lines in VCF.
 
 basic:      key=value
 structured: TAG=<keyA=valueA,...,keyB=valueB>
 
 Basic field 'key's must be unique.
 
 Structured field TAGs are not required to be unique, but for each duplicate TAG, there
 must be at least one unique key that identifies the structured line (usually "ID").
 */
class VcfHeader : public Equitable<VcfHeader>
{
public:
    class Builder;
    
    struct BasicKey : public Comparable<BasicKey>
    {
        template <typename K> BasicKey(K&& k) : value {std::forward<K>(k)} {}
        std::string value;
        operator std::string() const { return value; }
    };
    struct Tag : public Comparable<Tag>
    {
        template <typename T> Tag(T&& t) : value {std::forward<T>(t)} {}
        std::string value;
        operator std::string() const { return value; }
    };
    struct StructuredKey : public Comparable<StructuredKey>
    {
        template <typename K> StructuredKey(K&& k) : value {std::forward<K>(k)} {}
        std::string value;
        operator std::string() const { return value; }
    };
    struct BasicKeyHash
    {
        std::size_t operator()(const BasicKey& k) const
        {
            return std::hash<decltype(k.value)>()(k.value);
        }
    };
    struct StructuredKeyHash
    {
        std::size_t operator()(const StructuredKey& k) const
        {
            return std::hash<decltype(k.value)>()(k.value);
        }
    };
    struct TagHash
    {
        std::size_t operator()(const Tag& k) const
        {
            return std::hash<decltype(k.value)>()(k.value);
        }
    };
    
    using Value              = std::string; // basic & structured fields share the same value type
    using BasicFieldMap      = std::unordered_map<BasicKey, Value, BasicKeyHash>;
    using StructuredField    = std::unordered_map<StructuredKey, Value, StructuredKeyHash>;
    using StructuredFieldMap = std::unordered_multimap<Tag, StructuredField, TagHash>;
    
    VcfHeader();
    
    VcfHeader(std::string file_format);
    
    template <typename T, typename U, typename B, typename S>
    VcfHeader(T&& file_format, U&& samples, B&& basic_fields, S&& structured_fields);
    
    VcfHeader(const VcfHeader&)            = default;
    VcfHeader& operator=(const VcfHeader&) = default;
    VcfHeader(VcfHeader&&)                 = default;
    VcfHeader& operator=(VcfHeader&&)      = default;
    
    ~VcfHeader() = default;
    
    const Value& file_format() const noexcept;
    
    unsigned num_samples() const noexcept;
    
    const std::vector<std::string>& samples() const noexcept;
    
    bool has(const BasicKey& k) const noexcept;
    bool has(const Tag& t) const noexcept;
    bool has(const Tag& tag, const StructuredKey& k) const noexcept;
    
    const Value& at(const BasicKey& k) const;
    const Value& find(const Tag& search_tag, const StructuredKey& search_key,
                      const StructuredKey& id_key, const Value& id_value) const;
    
    std::vector<BasicKey> basic_keys() const;
    std::vector<Tag> tags() const;
    std::vector<StructuredKey> keys(const Tag& t) const;
    const BasicFieldMap& basic_fields() const noexcept;
    std::vector<StructuredField> structured_fields(const Tag& t) const;
    const StructuredFieldMap& structured_fields() const noexcept;
    
    friend bool operator==(const BasicKey& lhs, const BasicKey& rhs)
    {
        return lhs.value == rhs.value;
    }
    
    friend bool operator<(const BasicKey& lhs, const BasicKey& rhs)
    {
        return lhs.value < rhs.value;
    }
    
    friend bool operator==(const StructuredKey& lhs, const StructuredKey& rhs)
    {
        return lhs.value == rhs.value;
    }
    
    friend bool operator<(const StructuredKey& lhs, const StructuredKey& rhs)
    {
        return lhs.value < rhs.value;
    }
    
    friend bool operator==(const Tag& lhs, const Tag& rhs)
    {
        return lhs.value == rhs.value;
    }
    
    friend bool operator<(const Tag& lhs, const Tag& rhs)
    {
        return lhs.value < rhs.value;
    }
    
    friend std::ostream& operator<<(std::ostream& os, const VcfHeader& header);
    
private:
    // required fields
    std::string file_format_;
    std::vector<std::string> samples_;
    
    // optional fields
    BasicFieldMap basic_fields_;
    StructuredFieldMap structured_fields_;
};

template <typename T, typename U, typename B, typename S>
VcfHeader::VcfHeader(T&& file_format, U&& samples, B&& basic_fields, S&& structured_fields)
: file_format_ {std::forward<T>(file_format)}
, samples_ {std::forward<U>(samples)}
, basic_fields_ {std::forward<B>(basic_fields)}
, structured_fields_ {std::forward<S>(structured_fields)}
{}

const std::string& get_id_field_value(const VcfHeader& header, const VcfHeader::Tag& t,
                                      const VcfHeader::StructuredKey& id_value,
                                      const VcfHeader::StructuredKey& lookup_key);

const std::string& get_id_field_type(const VcfHeader& header, const VcfHeader::Tag& t,
                                     const VcfHeader::Value& id_value);

VcfType get_typed_value(const VcfHeader& header, const VcfHeader::Tag& t,
                        const VcfHeader::StructuredKey& key, const VcfHeader::Value& value);

VcfType get_typed_info_value(const VcfHeader& header,
                             const VcfHeader::StructuredKey& key,
                             const VcfHeader::Value& value);

VcfType get_typed_format_value(const VcfHeader& header,
                               const VcfHeader::StructuredKey& key,
                               const VcfHeader::Value& value);

std::vector<VcfType> get_typed_values(const VcfHeader& header,
                                      const VcfHeader::StructuredKey& format_key,
                                      const VcfHeader::StructuredKey& field_key,
                                      const std::vector<VcfHeader::Value>& values);

std::vector<VcfType> get_typed_info_values(const VcfHeader& header,
                                           const VcfHeader::StructuredKey& field_key,
                                           const std::vector<VcfHeader::Value>& values);

std::vector<VcfType> get_typed_format_values(const VcfHeader& header,
                                             const VcfHeader::StructuredKey& field_key,
                                             const std::vector<VcfHeader::Value>& values);

bool contig_line_exists(const VcfHeader& header, const std::string& contig);

bool operator==(const VcfHeader& lhs, const VcfHeader& rhs);

std::ostream& operator<<(std::ostream& os, const VcfHeader& header);

class VcfHeader::Builder
{
public:
    Builder() = default;
    
    Builder(const VcfHeader& header);
    
    Builder& set_file_format(std::string file_format);
    Builder& add_sample(std::string sample);
    Builder& set_samples(std::vector<std::string> samples);
    Builder& add_basic_field(std::string key, std::string value);
    Builder& add_structured_field(std::string tag, std::unordered_map<std::string, std::string> values);
    Builder& add_info(std::string id, std::string number, std::string type, std::string description,
                      std::unordered_map<std::string, std::string> other_values = {});
    Builder& add_filter(std::string id, std::string description,
                        std::unordered_map<std::string, std::string> other_values = {});
    Builder& add_format(std::string id, std::string number, std::string type, std::string description,
                        std::unordered_map<std::string, std::string> other_values = {});
    Builder& add_contig(std::string id, std::unordered_map<std::string, std::string> other_values = {});
    
    Builder& clear_info() noexcept;
    Builder& clear_format() noexcept;
    
    VcfHeader build() const;
    VcfHeader build_once() noexcept;
    
private:
    std::string file_format_;
    std::vector<std::string> samples_ = {};
    VcfHeader::BasicFieldMap basic_fields_ = {};
    VcfHeader::StructuredFieldMap structured_fields_ = {};
};

// A VcfHeader::Builder pre-filled with all reserved INFO and FORMAT fields
VcfHeader::Builder get_default_header_builder();

} // namespace octopus    

namespace std {
    template <> struct hash<octopus::VcfHeader>
    {
        size_t operator()(const octopus::VcfHeader& header) const
        {
            return hash<octopus::VcfHeader::Value>()(header.file_format());
        }
    };
} // namespace std

#endif
