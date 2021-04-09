// Copyright (c) 2015-2021 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef threshold_filter_hpp
#define threshold_filter_hpp

#include <vector>
#include <string>
#include <functional>

#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/any.hpp>

#include "single_pass_variant_call_filter.hpp"
#include "logging/progress_meter.hpp"
#include "../facets/facet_factory.hpp"

namespace octopus {

class VcfHeader;

namespace csr {

class ThresholdVariantCallFilter : public SinglePassVariantCallFilter
{
public:
    struct Threshold
    {
        virtual ~Threshold() = default;
        virtual std::unique_ptr<Threshold> clone() const = 0;
        virtual bool operator()(const Measure::ResultType& value) const noexcept = 0;
    };
    
    struct ThresholdWrapper
    {
        ThresholdWrapper(std::unique_ptr<Threshold> threshold) : threshold {std::move(threshold)} {}
        ThresholdWrapper(const ThresholdWrapper& other) : threshold {other.threshold->clone()} {}
        ThresholdWrapper& operator=(const ThresholdWrapper& other)
        {
            if (&other != this) threshold = other.threshold->clone();
            return *this;
        }
        bool operator()(Measure::ResultType value) const noexcept { return (*threshold)(value); }
        std::unique_ptr<Threshold> threshold;
    };
    
    struct Condition
    {
        MeasureWrapper measure;
        ThresholdWrapper threshold;
        std::string vcf_filter_key = ".";
    };
    
    struct ConditionVectorPair
    {
        std::vector<Condition> hard, soft;
    };
    
    ThresholdVariantCallFilter() = delete;
    
    ThresholdVariantCallFilter(FacetFactory facet_factory,
                               ConditionVectorPair conditions,
                               OutputOptions output_config,
                               ConcurrencyPolicy threading,
                               boost::optional<ProgressMeter&> progress = boost::none,
                               std::vector<MeasureWrapper> other_measures = {});
    
    ThresholdVariantCallFilter(const ThresholdVariantCallFilter&)            = delete;
    ThresholdVariantCallFilter& operator=(const ThresholdVariantCallFilter&) = delete;
    ThresholdVariantCallFilter(ThresholdVariantCallFilter&&)                 = delete;
    ThresholdVariantCallFilter& operator=(ThresholdVariantCallFilter&&)      = delete;
    
    virtual ~ThresholdVariantCallFilter() override = default;

protected:
    using ThresholdVector   = std::vector<ThresholdWrapper>;
    using ThresholdIterator = ThresholdVector::const_iterator;
    using MeasureIterator   = MeasureVector::const_iterator;
    
    ThresholdVector hard_thresholds_, soft_thresholds_;
    std::vector<std::string> vcf_filter_keys_;
    bool all_unique_filter_keys_;
    
    bool passes_all_filters(MeasureIterator first_measure, MeasureIterator last_measure,
                            ThresholdIterator first_threshold) const;
    
private:
    std::string do_name() const override;
    virtual void annotate(VcfHeader::Builder& header) const override;
    virtual Classification classify(const MeasureVector& measures) const override;
    
    virtual bool passes_all_hard_filters(const MeasureVector& measures) const;
    virtual bool passes_all_soft_filters(const MeasureVector& measures) const;
    virtual std::vector<std::string> get_failing_vcf_filter_keys(const MeasureVector& measures) const;
};

template <typename M, typename... Args>
decltype(auto) make_wrapped_threshold(Args&&... args)
{
    using Wrapper = ThresholdVariantCallFilter::ThresholdWrapper;
    return Wrapper {std::make_unique<M>(std::forward<Args>(args)...)};
}

namespace detail {

template <typename Cmp, typename T = double>
struct UnaryThreshold
{
    explicit UnaryThreshold(T target, Cmp cmp) : visitor_ {target, cmp} {}
    bool operator()(Measure::ResultType value) const noexcept
    {
        return boost::apply_visitor(visitor_, value);
    }
private:
    struct UnaryVisitor : public boost::static_visitor<bool>
    {
        explicit UnaryVisitor(T target, Cmp cmp) : target {target}, cmp {cmp} {}
        bool operator()(const Measure::ValueType& value) const noexcept
        {
            return boost::apply_visitor(*this, value);
        }
        template <typename U>
        bool operator()(U value) const noexcept
        {
             return !cmp(value, target);
        }
        template <typename U>
        bool operator()(const Measure::Optional<U>& value) const noexcept
        {
            return !value || (*this)(*value);
        }
        template <typename U>
        bool operator()(const Measure::Array<U>& values) const noexcept
        {
            return std::all_of(std::cbegin(values), std::cend(values), [this] (const auto& value) { return (*this)(value); });
        }
        T target;
        Cmp cmp;
    };
    UnaryVisitor visitor_;
};

} // namespace detail

template <typename T = double>
struct EqualThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit EqualThreshold(T target) : base_ {target, std::equal_to<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<EqualThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::equal_to<>, T> base_;
};

template <typename T = double>
struct NotEqualThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit NotEqualThreshold(T target) : base_ {target, std::not_equal_to<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<NotEqualThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::not_equal_to<>, T> base_;
};

template <typename T = double>
struct LessThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit LessThreshold(T target) : base_ {target, std::less<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<LessThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::less<>, T> base_;
};

template <typename T = double>
struct LessEqualThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit LessEqualThreshold(T target) : base_ {target, std::less_equal<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<LessEqualThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::less_equal<>, T> base_;
};

template <typename T = double>
struct GreaterThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit GreaterThreshold(T target) : base_ {target, std::greater<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<GreaterThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::greater<>, T> base_;
};

template <typename T = double>
struct GreaterEqualThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit GreaterEqualThreshold(T target) : base_ {target, std::greater_equal<> {}} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<GreaterEqualThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return base_(value);
    }
private:
    detail::UnaryThreshold<std::greater_equal<>, T> base_;
};

template <typename T = double>
struct BetweenThreshold : public ThresholdVariantCallFilter::Threshold
{
    explicit BetweenThreshold(T lower_bound, T upper_bound) : visitor_ {lower_bound, upper_bound} {}
    std::unique_ptr<ThresholdVariantCallFilter::Threshold> clone() const
    {
        return std::make_unique<BetweenThreshold>(*this);
    }
    bool operator()(const Measure::ResultType& value) const noexcept
    {
        return boost::apply_visitor(visitor_, value);
    }
private:
    struct BetweenVisitor : public boost::static_visitor<bool>
    {
        explicit BetweenVisitor(T lower_bound, T upper_bound)
        : lower_bound {lower_bound}, upper_bound {upper_bound} {}
        template <typename U>
        bool operator()(const U& value) const noexcept
        {
            return lower_bound <= value && value <= upper_bound;
        }
        template <typename U>
        bool operator()(const boost::optional<U>& value) const noexcept
        {
            return !value || (*this)(*value);
        }
        T lower_bound, upper_bound;
    };
    BetweenVisitor visitor_;
};

} // namespace csr
} // namespace octopus

#endif
