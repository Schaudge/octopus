//
//  program_options.cpp
//  Octopus
//
//  Created by Daniel Cooke on 27/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "program_options.hpp"

#include <string>
#include <iostream>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <functional>
#include <utility>
#include <thread>
#include <sstream>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

#include "genomic_region.hpp"
#include "reference_genome.hpp"
#include "aligned_read.hpp"
#include "read_manager.hpp"

#include "read_utils.hpp"
#include "read_filters.hpp"
#include "downsampler.hpp"
#include "read_transform.hpp"
#include "read_transformations.hpp"

#include "haplotype_prior_model.hpp"
#include "variant_caller_builder.hpp"

#include "vcf_reader.hpp"
#include "vcf_writer.hpp"

#include "mappable_algorithms.hpp"
#include "string_utils.hpp"

#include "maths.hpp"

#include "logging.hpp"

namespace Octopus
{
    namespace Options
    {
    void conflicting_options(const po::variables_map& vm, const std::string& opt1, const std::string& opt2)
    {
        if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()) {
            throw std::logic_error(std::string("conflicting options '") + opt1 + "' and '" + opt2 + "'.");
        }
    }
    
    void option_dependency(const po::variables_map& vm, const std::string& for_what,
                           const std::string& required_option)
    {
        if (vm.count(for_what) && !vm[for_what].defaulted())
            if (vm.count(required_option) == 0 || vm[required_option].defaulted()) {
                throw std::logic_error(std::string("option '") + for_what
                                       + "' requires option '" + required_option + "'.");
            }
    }
    
    struct ContigPloidy
    {
        GenomicRegion::ContigNameType contig;
        unsigned ploidy;
    };
    
    std::istream& operator>>(std::istream& in, ContigPloidy& result)
    {
        std::string token;
        in >> token;
        
        if (std::count(std::cbegin(token), std::cend(token), '=') != 1) {
            throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
                "contig-ploidies"};
        }
        
        const auto pos = token.find('=');
        
        const auto rhs = token.substr(pos + 1);
        
        try {
            using PloidyType = decltype(result.ploidy);
            result.ploidy = boost::lexical_cast<PloidyType>(rhs);
        } catch (const boost::bad_lexical_cast&) {
            throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
                "contig-ploidies"};
        }
        
        token.erase(pos);
        
        result.contig = std::move(token);
        
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const ContigPloidy contig_ploidy)
    {
        out << contig_ploidy.contig << "=" << contig_ploidy.ploidy;
        return out;
    }
    
    std::istream& operator>>(std::istream& in, ContigOutputOrder& result)
    {
        std::string token;
        in >> token;
        if (token == "lexicographical-ascending")
            result = ContigOutputOrder::LexicographicalAscending;
        else if (token == "lexicographical-descending")
            result = ContigOutputOrder::LexicographicalDescending;
        else if (token == "contig-size-ascending")
            result = ContigOutputOrder::ContigSizeAscending;
        else if (token == "contig-size-descending")
            result = ContigOutputOrder::ContigSizeDescending;
        else if (token == "as-in-reference")
            result = ContigOutputOrder::AsInReferenceIndex;
        else if (token == "as-in-reference-reversed")
            result = ContigOutputOrder::AsInReferenceIndexReversed;
        else if (token == "unspecified")
            result = ContigOutputOrder::Unspecified;
        else throw po::validation_error {po::validation_error::kind_t::invalid_option_value, token,
            "contig-output-order"};
        return in;
    }
    
    std::ostream& operator<<(std::ostream& out, const ContigOutputOrder order)
    {
        switch (order) {
            case ContigOutputOrder::LexicographicalAscending:
                out << "lexicographical-ascending";
                break;
            case ContigOutputOrder::LexicographicalDescending:
                out << "lexicographical-descending";
                break;
            case ContigOutputOrder::ContigSizeAscending:
                out << "contig-size-ascending";
                break;
            case ContigOutputOrder::ContigSizeDescending:
                out << "contig-size-descending";
                break;
            case ContigOutputOrder::AsInReferenceIndex:
                out << "as-in-reference";
                break;
            case ContigOutputOrder::AsInReferenceIndexReversed:
                out << "as-in-reference-reversed";
                break;
            case ContigOutputOrder::Unspecified:
                out << "unspecified";
                break;
        }
        return out;
    }
    
    boost::optional<po::variables_map> parse_options(int argc, const char** argv)
    {
        try {
            po::positional_options_description p;
            
            p.add("caller", -1);
            
            po::options_description general("General options");
            general.add_options()
            ("help,h", "Produce help message")
            ("version", "Output the version number")
            ("debug", po::bool_switch()->default_value(false),
             "Writes verbose debug information to debug.log in the working directory")
            ("trace", po::bool_switch()->default_value(false),
             "Writes very verbose debug information to trace.log in the working directory. For developer use only")
            ;
            
            po::options_description backend("Backend options");
            backend.add_options()
            ("threads,t", po::value<unsigned>()->default_value(1),
             "Sets the number of threads used by the application, set to 0 to let the"
             " application decide the number of threads")
            ("reference-cache-size", po::value<size_t>()->default_value(50000),
             "The maximum number of bytes that can be used to cache reference sequence")
            ("target-read-buffer-size", po::value<float>()->default_value(0.5),
             "Will try to limit the amount of memory (in gigabytes) occupied by reads to this amount")
            ("compress-reads", po::bool_switch()->default_value(false),
             "Compresses all read data (slower)")
            ("max-open-read-files", po::value<unsigned>()->default_value(200),
             "Limits the number of read files that can be open simultaneously")
            ("working-directory,w", po::value<std::string>(),
             "Sets the working directory")
            ;
            
            po::options_description input("Input/output options");
            input.add_options()
            ("reference,R", po::value<std::string>()->required(),
             "The reference genome file")
            ("reads,I", po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of BAM/CRAM paths")
            ("reads-file", po::value<std::string>(),
             "File of BAM/CRAM paths, one per line")
            ("use-one-based-indexing", po::bool_switch()->default_value(false),
             "Uses one based indexing for input regions rather than zero based")
            ("regions,L", po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of regions (chrom:begin-end) that will be analysed")
            ("regions-file", po::value<std::string>(),
             "File of regions (chrom:begin-end), one per line")
            ("skip-regions", po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of regions (chrom:begin-end) to skip")
            ("skip-regions-file", po::value<std::string>(),
             "File of regions (chrom:begin-end) to skip, one per line")
            ("samples,S", po::value<std::vector<std::string>>()->multitoken(),
             "Space-seperated list of sample names to analyse")
            ("samples-file", po::value<std::string>(),
             "File of sample names to analyse, one per line")
            ("output,o", po::value<std::string>()->default_value("octopus_calls.vcf"),
             "File to where output is written")
            ("contig-output-order", po::value<ContigOutputOrder>()->default_value(ContigOutputOrder::AsInReferenceIndex),
             "The order contigs should be written to the output")
            ;
            
            po::options_description filters("Read filter options");
            filters.add_options()
            ("consider-unmapped-reads", po::bool_switch()->default_value(false),
             "Allows reads marked as unmapped to be used for calling")
            ("min-mapping-quality", po::value<unsigned>()->default_value(20),
             "Minimum read mapping quality required to consider a read for calling")
            ("good-base-quality", po::value<unsigned>()->default_value(20),
             "Base quality threshold used by min-good-bases filter")
            ("min-good-base-fraction", po::value<double>(),
             "Base quality threshold used by min-good-bases filter")
            ("min-good-bases", po::value<AlignedRead::SizeType>()->default_value(20),
             "Minimum number of bases with quality min-base-quality before read is considered")
            ("allow-qc-fails", po::bool_switch()->default_value(false),
             "Filters reads marked as QC failed")
            ("min-read-length", po::value<AlignedRead::SizeType>(),
             "Filters reads shorter than this")
            ("max-read-length", po::value<AlignedRead::SizeType>(),
             "Filter reads longer than this")
            ("allow-marked-duplicates", po::bool_switch()->default_value(false),
             "Allows reads marked as duplicate in alignment record")
            ("allow-octopus-duplicates", po::bool_switch()->default_value(false),
             "Allows reads considered duplicates by Octopus")
            ("no-secondary-alignments", po::bool_switch()->default_value(false),
             "Filters reads marked as secondary alignments")
            ("no-supplementary-alignmenets", po::bool_switch()->default_value(false),
             "Filters reads marked as supplementary alignments")
            ("consider-reads-with-unmapped-segments", po::bool_switch()->default_value(false),
             "Allows reads with unmapped template segmenets to be used for calling")
            ("no-downsampling", po::bool_switch()->default_value(false),
             "Diables all downsampling")
            ("downsample-above", po::value<unsigned>()->default_value(500),
             "Downsample reads in regions where coverage is over this")
            ("downsample-target", po::value<unsigned>()->default_value(400),
             "The target coverage for the downsampler")
            ;
            
            po::options_description transforms("Read transform options");
            transforms.add_options()
            ("disable-soft-clip-masking", po::bool_switch()->default_value(false),
             "Disables soft clipped masking, thus allowing all soft clipped bases to be used for candidate generation")
            ("tail-trim-size", po::value<AlignedRead::SizeType>()->default_value(0),
             "Trims this number of bases off the tail of all reads")
            ("disable-adapter-masking", po::bool_switch()->default_value(false),
             "Disables adapter detection and masking")
            ("disable-overlap-masking", po::bool_switch()->default_value(false),
             "Disables read segment overlap masking")
            ;
            
            po::options_description candidates("Candidate generation options");
            candidates.add_options()
            ("no-raw-cigar-candidates", po::bool_switch()->default_value(false),
             "Disables candidate generation from raw read alignmenets (CIGAR strings)")
            ("no-assembly-candidates", po::bool_switch()->default_value(false),
             "Disables candidate generation using local re-assembly")
            ("candidates-from-source", po::value<std::string>(),
             "Variant file path containing known variants. These variants will automatically become candidates")
            ("regenotype", po::bool_switch()->default_value(false),
             "Disables all generators other than source which must be present")
            ("min-base-quality", po::value<unsigned>()->default_value(20),
             "Only bases with quality above this value are considered for candidate generation")
            ("min-supporting-reads", po::value<unsigned>()->default_value(2),
             "Minimum number of reads that must support a variant if it is to be considered a candidate")
            ("max-variant-size", po::value<AlignedRead::SizeType>()->default_value(500),
             "Maximum candidate varaint size from alignmenet CIGAR")
            ("kmer-size", po::value<std::vector<unsigned>>()->multitoken()
                ->default_value(std::vector<unsigned> {15, 25}, "15 25")->composing(),
             "K-mer sizes to use for local re-assembly")
            ("min-assembler-base-quality", po::value<unsigned>()->default_value(15),
             "Only bases with quality above this value are considered for candidate generation by the assembler")
            ;
            
            po::options_description caller("Caller options");
            caller.add_options()
            ("caller", po::value<std::string>()->default_value("population"),
             "Which of the Octopus callers to use")
            ("organism-ploidy,ploidy", po::value<unsigned>()->default_value(2),
             "Organism ploidy, all contigs with unspecified ploidy are assumed this ploidy")
            ("contig-ploidies", po::value<std::vector<ContigPloidy>>()->multitoken(),
             "Space-seperated list of contig=ploidy pairs")
            ("contig-ploidies-file", po::value<std::string>(),
             "List of contig=ploidy pairs, one per line")
            ("transition-prior", po::value<double>()->default_value(0.003),
             "Prior probability of a transition snp from the reference")
            ("transversion-prior", po::value<double>()->default_value(0.003),
             "Prior probability of a transversion snp from the reference")
            ("insertion-prior", po::value<double>()->default_value(0.003),
             "Prior probability of an insertion into the reference")
            ("deletion-prior", po::value<double>()->default_value(0.003),
             "Prior probability of a deletion from the reference")
            ("prior-precision", po::value<double>()->default_value(0.003),
             "Precision (inverse variance) of the given variant priors")
            ("max-haplotypes", po::value<unsigned>()->default_value(128),
             "Maximum number of haplotypes the model may consider")
            ("min-variant-posterior", po::value<float>()->default_value(20.0),
             "Minimum variant call posterior probability (phred scale)")
            ("min-refcall-posterior", po::value<float>()->default_value(10.0),
             "Minimum homozygous reference call posterior probability (phred scale)")
            ("make-positional-refcalls", po::bool_switch()->default_value(false),
             "Caller will output positional REFCALLs")
            ("make-blocked-refcalls", po::bool_switch()->default_value(false),
             "Caller will output blocked REFCALLs")
            ("sites-only", po::bool_switch()->default_value(false),
             "Only outout variant call sites (i.e. without sample genotype information)")
            ("disable-haplotype-lagging", po::bool_switch()->default_value(false),
             "Disables lagging in the haplotype generator, so each candidate variant will be considered"
             " exactly once.")
            ("min-phase-score", po::value<float>()->default_value(20),
             "Minimum phase score required to output a phased call (phred scale)")
            ;
            
            po::options_description cancer("Cancer caller specific options");
            cancer.add_options()
            ("normal-sample", po::value<std::string>(),
             "Normal sample used in cancer model")
            ("somatic-mutation-rate", po::value<float>()->default_value(0.00001),
             "Expected somatic mutation rate, per megabase pair, for this sample")
            ("min-somatic-posterior", po::value<float>()->default_value(10.0),
             "The minimum somatic mutation call posterior probability (phred scale)")
            ("somatics-only", po::bool_switch()->default_value(false),
             "Only output somatic calls (for somatic calling models only)")
            ;
            
            po::options_description trio("Trio caller specific options");
            trio.add_options()
            ("maternal-sample", po::value<std::string>(),
             "Maternal sample for trio caller")
            ("paternal-sample", po::value<std::string>(),
             "Paternal sample for trio caller")
            ;
            
            po::options_description all("Allowed options");
            all.add(general).add(backend).add(input).add(filters).add(transforms)
            .add(candidates).add(caller).add(cancer);
            
            po::variables_map vm;
            po::store(po::command_line_parser(argc, argv).options(all).positional(p).run(), vm);
            
            if (vm.count("help")) {
                std::cout << "Usage: octopus <command> [options]" << std::endl;
                std::cout << all << std::endl;
                return vm;
            }
            
            if (vm.count("version")) {
                std::cout << "Octopus version " + Octopus_version << std::endl;
                return vm;
            }
            
            // boost::option cannot handle option dependencies so we must do our own checks
            
            if (vm.count("reads") == 0 && vm.count("reads-file") == 0) {
                throw po::required_option {"--reads | --reads-file"};
            }
            
            if (vm.at("caller").as<std::string>() == "trio"
                && (vm.count("maternal-sample") == 0 || vm.count("paternal-sample") == 0)) {
                throw std::logic_error {"option maternal-sample and paternal-sample are required when caller=trio"};
            }
            
            conflicting_options(vm, "make-positional-refcalls", "make-blocked-refcalls");
            
            po::notify(vm);
            
            return vm;
        } catch (std::exception& e) {
            std::cout << "Option error: " << e.what() << std::endl;
            return boost::none;
        }
    }
    
    bool is_run_command(const po::variables_map& options)
    {
        return options.count("help") == 0 && options.count("version") == 0;
    }
    
    bool is_debug_mode(const po::variables_map& options)
    {
        return options.at("debug").as<bool>();
    }
    
    bool is_trace_mode(const po::variables_map& options)
    {
        return options.at("trace").as<bool>();
    }
    
    struct Line
    {
        std::string line_data;
        
        operator std::string() const
        {
            return line_data;
        }
    };
    
    std::istream& operator>>(std::istream& str, Line& data)
    {
        std::getline(str, data.line_data);
        return str;
    }
    
    boost::optional<fs::path> get_home_dir()
    {
        static const auto result = fs::path(std::getenv("HOME"));
        
        if (fs::is_directory(result)) {
            return result;
        }
        
        return boost::none;
    }
    
    bool is_shorthand_user_path(const fs::path& path)
    {
        return !path.empty() && path.string().front() == '~';
    }
    
    boost::optional<fs::path> expand_user_path(const fs::path& path)
    {
        if (is_shorthand_user_path(path)) {
            if (path.string().size() > 1 && path.string()[1] == '/') {
                const auto home_dir = get_home_dir();
                
                if (home_dir) {
                    return fs::path {home_dir->string() + path.string().substr(1)};
                }
                
                return boost::none;
            }
            return boost::none;
        }
        return path;
    }
    
    boost::optional<fs::path> get_working_directory(const po::variables_map& options)
    {
        if (options.count("working-directory") == 1) {
            return expand_user_path(options.at("working-directory").as<std::string>());
        }
        return fs::current_path();
    }
    
    boost::optional<fs::path> resolve_path(const fs::path& path, const po::variables_map& options)
    {
        if (is_shorthand_user_path(path)) {
            return expand_user_path(path); // must be a root path
        }
        
        if (fs::exists(path)) {
            return path; // must be a root path
        }
        
        const auto parent_dir = path.parent_path();
        
        if (fs::exists(parent_dir)) {
            return path; // must yet-to-be-created root path
        }
        
        auto result = get_working_directory(options);
        
        if (result) {
            *result /= path;
            return *result;
        }
        
        return result;
    }
    
    std::vector<fs::path>
    extract_paths_from_file(const fs::path& file_path, const po::variables_map& options)
    {
        const auto resolved_path = resolve_path(file_path, options);
        
        std::vector<fs::path> result {};
        
        std::ifstream file {file_path.string()};
        
        if (!file.good()) {
            throw std::runtime_error {"Could not read from file " + file_path.string()};
        }
        
        std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                       std::back_inserter(result), [] (const Line& line) { return line.line_data; });
        
        const auto it = std::remove_if(std::begin(result), std::end(result),
                                       [] (const auto& path) { return path.empty(); });
        
        result.erase(it, std::end(result));
        
        return result;
    }
    
    auto resolve_paths(const std::vector<fs::path>& paths, const po::variables_map& options)
    {
        std::vector<fs::path> good_paths {}, bad_paths {};
        
        good_paths.reserve(paths.size());
        
        for (const auto& path : paths) {
            auto resolved_path = resolve_path(path, options);
            
            if (resolved_path) {
                good_paths.emplace_back(*std::move(resolved_path));
            } else {
                bad_paths.emplace_back(*std::move(resolved_path));
            }
        }
        
        good_paths.shrink_to_fit();
        bad_paths.shrink_to_fit();
        
        return std::make_pair(std::move(good_paths), std::move(bad_paths));
    }
    
    auto resolve_paths(const std::vector<std::string>& path_strings,
                           const po::variables_map& options)
    {
        std::vector<fs::path> paths {std::cbegin(path_strings), std::cend(path_strings)};
        return resolve_paths(paths, options);
    }
        
    bool is_file_readable(const fs::path& path)
    {
        std::ifstream tmp {path.string()};
        return tmp.good();
    }
    
    bool is_file_writable(const fs::path& path)
    {
        return true; // TODO
    }
    
    bool is_threading_allowed(const po::variables_map& options)
    {
        const auto num_threads = options.at("threads").as<unsigned>();
        return num_threads != 1;
    }
    
    boost::optional<unsigned> get_num_threads(const po::variables_map& options)
    {
        const auto num_threads = options.at("threads").as<unsigned>();
        
        if (num_threads > 0) return num_threads;
        
        return boost::none;
    }
    
    std::size_t get_target_read_buffer_size(const po::variables_map& options)
    {
        static constexpr std::size_t scale {1'000'000'000};
        return static_cast<std::size_t>(scale * options.at("target-read-buffer-size").as<float>());
    }
    
    boost::optional<fs::path> get_debug_log_file_name(const po::variables_map& options)
    {
        if (options.at("debug").as<bool>()) {
            const auto result = resolve_path("octopus_debug.log", options);
            
            if (!result) {
                throw std::runtime_error {"Could not resolve debug log file"};
            }
            
            return *result;
        }
        
        return boost::none;
    }
    
    boost::optional<fs::path> get_trace_log_file_name(const po::variables_map& options)
    {
        if (options.at("trace").as<bool>()) {
            const auto result = resolve_path("octopus_trace.log", options);
            
            if (!result) {
                throw std::runtime_error {"Could not resolve trace log file"};
            }
            
            return *result;
        }
        
        return boost::none;
    }
    
    boost::optional<ReferenceGenome> make_reference(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        const fs::path input_path {options.at("reference").as<std::string>()};
        
        auto resolved_path = resolve_path(input_path, options);
        
        if (!resolved_path) {
            stream(log) << "Could not resolve the path " << input_path
                << " given in the input option (--reference)";
            return boost::none;
        }
        
        if (!fs::exists(*resolved_path)) {
            stream(log) << "The path " << input_path
                << " given in the input option (--reference) does not exist";
            return boost::none;
        }
        
        if (!is_file_readable(*resolved_path)) {
            stream(log) << "The path " << input_path
                << " given in the input option (--reference) is not readable";
            return boost::none;
        }
        
        const auto ref_cache_size = options.at("reference-cache-size").as<std::size_t>();
        
        return ::make_reference(*std::move(resolved_path),
                                static_cast<ReferenceGenome::SizeType>(ref_cache_size),
                                is_threading_allowed(options));
    }
    
    template <typename T, typename S>
    std::vector<T>& append(std::vector<T>& target, const std::vector<S>& source)
    {
        target.insert(std::end(target), std::begin(source), std::end(source));
        return target;
    }
    
    template <typename T>
    std::vector<T>& append(std::vector<T>& target, std::vector<T>&& source)
    {
        target.insert(std::end(target),
                      std::make_move_iterator(std::begin(source)),
                      std::make_move_iterator(std::end(source)));
        source.clear();
        source.shrink_to_fit();
        return target;
    }
    
    bool is_bed_file(const fs::path& path)
    {
        return path.extension().string() == ".bed";
    }
    
    void seek_past_bed_header(std::ifstream& bed_file)
    {
        // TODO
    }
    
    boost::optional<std::string> convert_bed_line_to_region_str(const std::string& bed_line)
    {
        constexpr static char bed_delim {'\t'};
        
        const auto tokens = split(bed_line, bed_delim);
        
        switch (tokens.size()) {
            case 0:
                return boost::none;
            case 1:
                return std::string {tokens[0]};
            case 2:
                // Assume this represents a half range rather than a point
                return std::string {tokens[0] + ':' + tokens[1] + '-'};
            default:
                return std::string {tokens[0] + ':' + tokens[1] + '-' + tokens[2]};
        }
    }
    
    std::function<boost::optional<GenomicRegion>(const std::string&)>
    make_region_line_parser(const fs::path& region_path, const ReferenceGenome& reference)
    {
        if (is_bed_file(region_path)) {
            return [&] (const std::string& line) -> boost::optional<GenomicRegion>
            {
                auto region_str = convert_bed_line_to_region_str(line);
                if (region_str) {
                    return parse_region(*std::move(region_str), reference);
                }
                return boost::none;
            };
        } else {
            return [&] (const std::string& line) { return parse_region(line, reference); };
        }
    }
    
    std::vector<GenomicRegion> extract_regions_from_file(const fs::path& file_path,
                                                         const ReferenceGenome& reference)
    {
        if (!fs::exists(file_path)) {
            Logging::ErrorLogger log {};
            stream(log) << "File does not exist " << file_path;
            return {};
        }
        
        std::ifstream file {file_path.string()};
        
        if (is_bed_file(file_path)) {
            seek_past_bed_header(file);
        }
        
        std::vector<boost::optional<GenomicRegion>> parsed_lines {};
        
        std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                       std::back_inserter(parsed_lines), make_region_line_parser(file_path, reference));
        
        file.close();
        
        std::vector<GenomicRegion> result {};
        result.reserve(parsed_lines.size());
        
        for (auto&& region : parsed_lines) {
            if (region) {
                result.emplace_back(*std::move(region));
            }
        }
        
        result.shrink_to_fit();
        
        return result;
    }
    
    SearchRegions make_search_regions(const std::vector<GenomicRegion>& regions)
    {
        SearchRegions contig_mapped_regions {};
        
        for (const auto& region : regions) {
            contig_mapped_regions[region.get_contig_name()].insert(region);
        }
        
        SearchRegions result {};
        
        for (const auto& contig_regions : contig_mapped_regions) {
            auto covered_contig_regions = extract_covered_regions(contig_regions.second);
            result[contig_regions.first].insert(std::make_move_iterator(std::begin(covered_contig_regions)),
                                                std::make_move_iterator(std::end(covered_contig_regions)));
        }
        
        result.rehash(result.size());
        
        return result;
    }
    
    SearchRegions extract_search_regions(const ReferenceGenome& reference)
    {
        return make_search_regions(get_all_contig_regions(reference));
    }
    
    SearchRegions extract_search_regions(const std::vector<GenomicRegion>& regions,
                                         std::vector<GenomicRegion>& skip_regions)
    {
        auto input_regions = make_search_regions(regions);
        auto skipped = make_search_regions(skip_regions);
        
        SearchRegions result {input_regions.size()};
        
        for (const auto& contig_regions : input_regions) {
            result.emplace(contig_regions.first,
                           splice_all(skipped[contig_regions.first], contig_regions.second));
        }
        
        return result;
    }
    
    SearchRegions extract_search_regions(const ReferenceGenome& reference,
                                         std::vector<GenomicRegion>& skip_regions)
    {
        return extract_search_regions(get_all_contig_regions(reference), skip_regions);
    }
    
    std::vector<GenomicRegion> parse_regions(const std::vector<std::string>& unparsed_regions,
                                             const ReferenceGenome& reference)
    {
        std::vector<GenomicRegion> result {};
        result.reserve(unparsed_regions.size());
        
        bool all_region_parsed {true};
        
        for (const auto& unparsed_region : unparsed_regions) {
            Logging::WarningLogger log {};
            try {
                result.emplace_back(parse_region(unparsed_region, reference));
            } catch (std::exception& e) {
                all_region_parsed = false;
                stream(log) << "Could not parse input region \"" << unparsed_region
                            << "\". Check the format is correct, the contig is in the reference genome \""
                            << reference.get_name() << "\", and is in bounds.";
            }
        }
        
        if (!all_region_parsed) {
            result.clear();
        }
        
        return result;
    }
    
    auto transform_to_zero_based(std::vector<GenomicRegion>&& one_based_regions)
    {
        std::vector<GenomicRegion> result {};
        result.reserve(one_based_regions.size());
        
        for (auto&& region : one_based_regions) {
            result.emplace_back(shift(std::move(region), -1));
        }
        
        return result;
    }
    
    auto transform_to_zero_based(MappableFlatMultiSet<GenomicRegion>&& one_based_regions)
    {
        MappableFlatMultiSet<GenomicRegion> result {};
        result.reserve(one_based_regions.size());
        
        for (auto&& region : one_based_regions) {
            result.emplace(shift(std::move(region), -1));
        }
        
        return result;
    }
    
    auto transform_to_zero_based(SearchRegions&& one_based_search_regions)
    {
        SearchRegions result {one_based_search_regions.size()};
        
        for (auto&& contig_regions : one_based_search_regions) {
            result.emplace(contig_regions.first, transform_to_zero_based(std::move(contig_regions.second)));
        }
        
        return result;
    }
    
    SearchRegions get_search_regions(const po::variables_map& options, const ReferenceGenome& reference)
    {
        std::vector<GenomicRegion> skip_regions {};
        
        bool all_parsed {true};
        
        if (options.count("skip-regions") == 1) {
            const auto& region_strings = options.at("skip-regions").as<std::vector<std::string>>();
            auto parsed_regions = parse_regions(region_strings, reference);
            if (region_strings.size() == parsed_regions.size()) {
                append(skip_regions, std::move(parsed_regions));
            } else {
                all_parsed = false;
            }
        }
        
        if (options.count("skip-regions-file") == 1) {
            const auto& skip_path = options.at("skip-regions-file").as<std::string>();
            append(skip_regions, extract_regions_from_file(skip_path, reference));
        }
        
        if (options.at("use-one-based-indexing").as<bool>()) {
            skip_regions = transform_to_zero_based(std::move(skip_regions));
        }
        
        if (options.count("regions") == 0 && options.count("regions-file") == 0) {
            return extract_search_regions(reference, skip_regions);
        }
        
        std::vector<GenomicRegion> input_regions {};
        
        if (options.count("regions") == 1) {
            const auto& region_strings = options.at("regions").as<std::vector<std::string>>();
            auto parsed_regions = parse_regions(region_strings, reference);
            if (region_strings.size() == parsed_regions.size()) {
                append(input_regions, std::move(parsed_regions));
            } else {
                all_parsed = false;
            }
        }
        
        if (options.count("regions-file") == 1) {
            const auto& regions_path = options.at("regions-file").as<std::string>();
            append(input_regions, extract_regions_from_file(regions_path, reference));
        }
        
        if (!all_parsed) {
            Logging::WarningLogger log {};
            stream(log) << "Detected unparsed input regions so dumping all regions";
            input_regions.clear();
            skip_regions.clear();
        }
        
        auto result = extract_search_regions(input_regions, skip_regions);
        
        if (options.at("use-one-based-indexing").as<bool>()) {
            return transform_to_zero_based(std::move(result));
        }
        
        return result;
    }
    
    ContigOutputOrder get_contig_output_order(const po::variables_map& options)
    {
        return options.at("contig-output-order").as<ContigOutputOrder>();
    }
    
    boost::optional<std::vector<SampleIdType>> get_user_samples(const po::variables_map& options)
    {
        if (options.count("samples") == 1) {
            return options.at("samples").as<std::vector<SampleIdType>>();
        }
        return boost::none;
    }
    
    namespace
    {
        void log_unresolved_read_paths(const std::vector<fs::path>& paths,
                                       const std::string& option)
        {
            Logging::WarningLogger log {};
            for (const auto& path : paths) {
                stream(log) << "Could not resolve the path " << path
                << " given in the input option (--" + option +")";
            }
        }
        
        auto parition_existent_paths(std::vector<fs::path>& paths)
        {
            return std::partition(std::begin(paths), std::end(paths),
                                  [] (const auto& path) { return fs::exists(path); });
        }
        
        template <typename InputIt>
        void log_nonexistent_read_paths(InputIt first, InputIt last, const std::string& option)
        {
            Logging::WarningLogger log {};
            std::for_each(first, last, [&option, &log] (const auto& path) {
                stream(log) << "The path " << path
                << " given in the input option (--" + option + ") does not exist";
            });
        }
        
        auto parition_readable_paths(std::vector<fs::path>& paths)
        {
            return std::partition(std::begin(paths), std::end(paths),
                                  [] (const auto& path) { return is_file_readable(path); });
        }
        
        template <typename InputIt>
        void log_unreadable_read_paths(InputIt first, InputIt last, const std::string& option)
        {
            Logging::WarningLogger log {};
            std::for_each(first, last, [&option, &log] (const auto& path) {
                stream(log) << "The path " << path
                            << " given in the input option (--" + option + ") is not readable";
            });
        }
    } // namespace
    
    boost::optional<std::vector<fs::path>> get_read_paths(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        std::vector<fs::path> result {};
        
        bool all_paths_good {true};
        
        std::vector<fs::path> resolved_paths {}, unresolved_paths {};
        
        if (options.count("reads") == 1) {
            const auto& read_paths = options.at("reads").as<std::vector<std::string>>();
            
            std::tie(resolved_paths, unresolved_paths) = resolve_paths(read_paths, options);
            
            if (!unresolved_paths.empty()) {
                log_unresolved_read_paths(unresolved_paths, "reads");
                all_paths_good = false;
            }
            
            auto it = parition_existent_paths(resolved_paths);
            
            if (it != std::end(resolved_paths)) {
                log_nonexistent_read_paths(it, std::end(resolved_paths), "reads");
                all_paths_good = false;
                resolved_paths.erase(it, std::end(resolved_paths));
            }
            
            it = parition_readable_paths(resolved_paths);
            
            if (it != std::end(resolved_paths)) {
                log_unreadable_read_paths(it, std::end(resolved_paths), "reads");
                all_paths_good = false;
            }
            
            append(result, std::move(resolved_paths));
        }
        
        if (options.count("reads-file") == 1) {
            // first we need to make sure the path to the paths is okay
            const fs::path input_path {options.at("reads-file").as<std::string>()};
            
            auto resolved_path = resolve_path(input_path, options);
            
            if (!resolved_path) {
                stream(log) << "Could not resolve the path " << input_path
                    << " given in the input option (--reads-file)";
                all_paths_good = false;
            } else if (!fs::exists(*resolved_path)) {
                stream(log) << "The path " << input_path
                    << " given in the input option (--reads-file) does not exist";
                all_paths_good = false;
            } else if (!is_file_readable(*resolved_path)) {
                stream(log) << "The path " << input_path
                    << " given in the input option (--reads-file) is not readable";
                all_paths_good = false;
            } else {
                auto paths = extract_paths_from_file(*resolved_path, options);
                
                std::tie(resolved_paths, unresolved_paths) = resolve_paths(paths, options);
                
                if (!unresolved_paths.empty()) {
                    log_unresolved_read_paths(unresolved_paths, "reads-file");
                    all_paths_good = false;
                }
                
                auto it = parition_existent_paths(resolved_paths);
                
                if (it != std::end(resolved_paths)) {
                    log_nonexistent_read_paths(it, std::end(resolved_paths), "reads-file");
                    all_paths_good = false;
                    resolved_paths.erase(it, std::end(resolved_paths));
                }
                
                it = parition_readable_paths(resolved_paths);
                
                if (it != std::end(resolved_paths)) {
                    log_unreadable_read_paths(it, std::end(resolved_paths), "reads-file");
                    all_paths_good = false;
                }
                
                append(result, std::move(resolved_paths));
            }
        }
        
        std::sort(std::begin(result), std::end(result));
        
        const auto it = std::unique(std::begin(result), std::end(result));
                         
        const auto num_duplicates = std::distance(it, std::end(result));
        
        if (num_duplicates > 0) {
            Logging::WarningLogger log {};
            stream(log) << "There are " << num_duplicates
                        << " duplicate read paths, only unique paths will be considered";
        }
        
        result.erase(it, std::end(result));
        
        if (!all_paths_good && result.size() > 0) {
            Logging::WarningLogger log {};
            auto slog = stream(log);
            slog << "There are bad read paths so dumping " << result.size() << " good path";
            if (result.size() > 1) slog << "s";
            result.clear();
        }
        
        return result;
    }
    
    boost::optional<ReadManager> make_read_manager(const po::variables_map& options)
    {
        auto read_paths = get_read_paths(options);
        
        if (read_paths) {
            const auto max_open_files = options.at("max-open-read-files").as<unsigned>();
            return ReadManager {*std::move(read_paths), max_open_files};
        }
        
        return boost::none;
    }
    
    ReadFilterer make_read_filter(const po::variables_map& options)
    {
        using QualityType = AlignedRead::QualityType;
        using SizeType    = AlignedRead::SizeType;
        
        ReadFilterer result {};
        
        if (!options.at("consider-unmapped-reads").as<bool>()) {
            result.register_filter(ReadFilters::is_mapped());
        }
        
        const auto min_mapping_quality = options.at("min-mapping-quality").as<unsigned>();
        
        if (min_mapping_quality > 0) {
            result.register_filter(ReadFilters::is_good_mapping_quality(min_mapping_quality));
        }
        
        const auto min_base_quality = options.at("good-base-quality").as<unsigned>();
        const auto min_good_bases   = options.at("min-good-bases").as<unsigned>();
        
        if (min_good_bases > 0) {
            result.register_filter(ReadFilters::has_sufficient_good_quality_bases(min_base_quality, min_good_bases));
        }
        
        if (options.count("min-good-base-fraction") == 1) {
            auto min_good_base_fraction =  options.at("min-good-base-fraction").as<double>();
            result.register_filter(ReadFilters::has_good_base_fraction(min_base_quality, min_good_base_fraction));
        }
        
        if (options.count("min-read-length") == 1) {
            result.register_filter(ReadFilters::is_short(options.at("min-read-length").as<SizeType>()));
        }
        
        if (options.count("max-read-length") == 1) {
            result.register_filter(ReadFilters::is_long(options.at("max-read-length").as<SizeType>()));
        }
        
        if (!options.at("allow-marked-duplicates").as<bool>()) {
            result.register_filter(ReadFilters::is_not_marked_duplicate());
        }
        
        if (!options.at("allow-octopus-duplicates").as<bool>()) {
            result.register_filter(ReadFilters::filter_duplicates());
        }
        
        if (!options.at("allow-qc-fails").as<bool>()) {
            result.register_filter(ReadFilters::is_not_marked_qc_fail());
        }
        
        if (options.at("no-secondary-alignments").as<bool>()) {
            result.register_filter(ReadFilters::is_not_secondary_alignment());
        }
        
        if (options.at("no-supplementary-alignmenets").as<bool>()) {
            result.register_filter(ReadFilters::is_not_supplementary_alignment());
        }
        
        if (!options.at("consider-reads-with-unmapped-segments").as<bool>()) {
            result.register_filter(ReadFilters::is_next_segment_mapped());
        }
        
        return result;
    }
    
    boost::optional<Downsampler> make_downsampler(const po::variables_map& options)
    {
        if (options.at("no-downsampling").as<bool>()) {
            return boost::none;
        }
        
        auto max_coverage    = options.at("downsample-above").as<unsigned>();
        auto target_coverage = options.at("downsample-target").as<unsigned>();
        
        return Downsampler(max_coverage, target_coverage);
    }
    
    ReadTransform make_read_transform(const po::variables_map& options)
    {
        using SizeType = AlignedRead::SizeType;
        
        ReadTransform result {};
        
        bool trim_soft_clipped = !options.at("disable-soft-clip-masking").as<bool>();
        
        auto tail_trim_size = options.at("tail-trim-size").as<SizeType>();
        
        if (trim_soft_clipped && tail_trim_size > 0) {
            result.register_transform(ReadTransforms::trim_soft_clipped_tails(tail_trim_size));
        } else if (tail_trim_size > 0) {
            result.register_transform(ReadTransforms::trim_tail(tail_trim_size));
        } else if (trim_soft_clipped) {
            result.register_transform(ReadTransforms::trim_soft_clipped());
        }
        
        if (!options.at("disable-adapter-masking").as<bool>()) {
            result.register_transform(ReadTransforms::trim_adapters());
        }
        
        if (!options.at("disable-overlap-masking").as<bool>()) {
            result.register_transform(ReadTransforms::trim_overlapping());
        }
        
        return result;
    }
    
    CandidateGeneratorBuilder make_candidate_generator_builder(const po::variables_map& options,
                                                               const ReferenceGenome& reference)
    {
        CandidateGeneratorBuilder result {};
        
        result.set_reference(reference);
        
        if (options.count("candidates-from-source") == 1) {
            result.add_generator(CandidateGeneratorBuilder::Generator::External);
            
            const fs::path input_path {options.at("candidates-from-source").as<std::string>()};
            
            auto resolved_path = resolve_path(input_path, options);
            
            Logging::ErrorLogger log {};
            
            if (!resolved_path) {
                stream(log) << "Could not resolve the path " << input_path
                            << " given in the input option (--candidates-from-source)";
                //return boost::none;
            }
            
            if (!fs::exists(*resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--candidates-from-source) does not exist";
                //return boost::none;
            }
            
            result.set_variant_source(*std::move(resolved_path));
        }
        
        if (options.at("regenotype").as<bool>()) {
            if (options.count("candidates-from-source") == 0) {
                Logging::WarningLogger log {};
                log << "Source variant file(s) must be present in regenotype mode";
            }
            return result;
        }
        
        result.set_min_base_quality(options.at("min-base-quality").as<unsigned>());
        result.set_max_variant_size(options.at("max-variant-size").as<CandidateGeneratorBuilder::SizeType>());
        
        auto min_supporting_reads = options.at("min-supporting-reads").as<unsigned>();
        
        if (min_supporting_reads == 0) {
            Logging::WarningLogger log {};
            log << "Given option --min_supporting_reads 0, assuming this is a type and setting to 1";
            ++min_supporting_reads;
        }
        
        result.set_min_supporting_reads(min_supporting_reads);
        
        if (!options.at("no-raw-cigar-candidates").as<bool>()) {
            result.add_generator(CandidateGeneratorBuilder::Generator::Alignment);
        }
        
        if (!options.at("no-assembly-candidates").as<bool>()) {
            result.add_generator(CandidateGeneratorBuilder::Generator::Assembler);
            const auto kmer_sizes = options.at("kmer-size").as<std::vector<unsigned>>();
            for (const auto k : kmer_sizes) {
                result.add_kmer_size(k);
            }
            result.set_assembler_min_base_quality(options.at("min-assembler-base-quality").as<unsigned>());
        }
        
        return result;
    }
    
    void print_ambiguous_contig_ploidies(const std::vector<ContigPloidy>& contig_ploidies,
                                         const po::variables_map& options)
    {
        Logging::WarningLogger log {};
        
        log << "Ambiguous ploidies found";
        
        for (auto it = std::cbegin(contig_ploidies), end = std::cend(contig_ploidies); it != end;) {
            it = std::adjacent_find(it, std::cend(contig_ploidies),
                                    [] (const auto& lhs, const auto& rhs) {
                                        return lhs.contig == rhs.contig;
                                    });
            if (it != std::cend(contig_ploidies)) {
                const auto it2 = std::find_if(std::next(it), std::cend(contig_ploidies),
                                              [=] (const auto& cp) {
                                                  return it->contig != cp.contig;
                                              });
                
                std::ostringstream ss {};
                
                std::copy(it, it2, std::ostream_iterator<ContigPloidy>(ss, " "));
                
                log << ss.str();
                
                it = it2;
            }
        }
    }
    
    void remove_duplicate_ploidies(std::vector<ContigPloidy>& contig_ploidies)
    {
        std::sort(std::begin(contig_ploidies), std::end(contig_ploidies),
                  [] (const auto& lhs, const auto& rhs) {
                      return (lhs.contig == rhs.contig) ? lhs.ploidy < rhs.ploidy : lhs.contig < rhs.contig;
                  });
        
        const auto it = std::unique(std::begin(contig_ploidies), std::end(contig_ploidies),
                                    [] (const auto& lhs, const auto& rhs) {
                                        return lhs.contig == rhs.contig && lhs.ploidy == rhs.ploidy;
                                    });
        
        contig_ploidies.erase(it, std::end(contig_ploidies));
    }
    
    bool has_ambiguous_ploidies(const std::vector<ContigPloidy>& contig_ploidies)
    {
        const auto it2 = std::adjacent_find(std::cbegin(contig_ploidies), std::cend(contig_ploidies),
                                            [] (const auto& lhs, const auto& rhs) {
                                                return lhs.contig == rhs.contig;
                                            });
        return it2 != std::cend(contig_ploidies);
    }
    
    boost::optional<std::vector<ContigPloidy>> extract_contig_ploidies(const po::variables_map& options)
    {
        std::vector<ContigPloidy> result {};
        
        if (options.count("contig-ploidies-file") == 1) {
            const fs::path input_path {options.at("contig-ploidies-file").as<std::string>()};
            
            const auto resolved_path = resolve_path(input_path, options);
            
            Logging::ErrorLogger log {};
            
            if (!resolved_path) {
                stream(log) << "Could not resolve the path " << input_path
                            << " given in the input option (--contig-ploidies-file)";
                return boost::none;
            }
            
            if (!fs::exists(*resolved_path)) {
                stream(log) << "The path " << input_path
                            << " given in the input option (--contig-ploidies-file) does not exist";
                return boost::none;
            }
            
            std::ifstream file {resolved_path->string()};
            
            std::transform(std::istream_iterator<Line>(file), std::istream_iterator<Line>(),
                           std::back_inserter(result), [] (const Line& line) {
                               std::istringstream ss {line.line_data};
                               ContigPloidy result {};
                               ss >> result;
                               return result;
                           });
        }
        
        if (options.count("contig-ploidies") == 1) {
            append(result, options.at("contig-ploidies").as<std::vector<ContigPloidy>>());
        }
        
        remove_duplicate_ploidies(result);
        
        if (has_ambiguous_ploidies(result)) {
            print_ambiguous_contig_ploidies(result, options);
            return boost::none;
        }
        
        return result;
    }
    
    bool call_sites_only(const po::variables_map& options)
    {
        return options.at("sites-only").as<bool>();
    }
    
    VariantCallerFactory make_variant_caller_factory(const ReferenceGenome& reference,
                                                     ReadPipe& read_pipe,
                                                     const CandidateGeneratorBuilder& candidate_generator_builder,
                                                     const SearchRegions& regions,
                                                     const po::variables_map& options)
    {
        using Maths::phred_to_probability;
        
        VariantCallerBuilder vc_builder {reference, read_pipe, candidate_generator_builder};
        
        auto caller = options.at("caller").as<std::string>();
        
        if (caller == "population" && read_pipe.num_samples() == 1) {
            caller = "individual";
        }
        
        vc_builder.set_caller(caller);
        
        if (options.at("make-positional-refcalls").as<bool>()) {
            vc_builder.set_refcall_type(VariantCaller::RefCallType::Positional);
        } else if (options.at("make-blocked-refcalls").as<bool>()) {
            vc_builder.set_refcall_type(VariantCaller::RefCallType::Blocked);
        }
        
        const auto min_variant_posterior_phred = options.at("min-variant-posterior").as<float>();
        vc_builder.set_min_variant_posterior(phred_to_probability(min_variant_posterior_phred));
        
        const auto min_refcall_posterior_phred = options.at("min-refcall-posterior").as<float>();
        vc_builder.set_min_refcall_posterior(phred_to_probability(min_refcall_posterior_phred));
        
        vc_builder.set_max_haplotypes(options.at("max-haplotypes").as<unsigned>());
        
        const auto min_phase_score_phred = options.at("min-phase-score").as<float>();
        vc_builder.set_min_phase_score(phred_to_probability(min_phase_score_phred));
        
        vc_builder.set_lagging(!options.at("disable-haplotype-lagging").as<bool>());
        
        if (caller == "cancer") {
            if (options.count("normal-sample") == 1) {
                auto normal_sample = options.at("normal-sample").as<std::string>();
                
                const auto& samples = read_pipe.get_samples();
                
                if (std::find(std::cbegin(samples), std::cend(samples),
                              normal_sample) == std::cend(samples)) {
                    Logging::WarningLogger log {};
                    stream(log) << "The given normal sample \"" << normal_sample
                                << "\" was not found in the read files";
                } else {
                    vc_builder.set_normal_sample(std::move(normal_sample));
                }
            } else {
                Logging::WarningLogger log {};
                stream(log) << "No normal sample was given so assuming all samples are tumour";
            }
            
            vc_builder.set_somatic_mutation_rate(options.at("somatic-mutation-rate").as<float>());
            
            const auto min_somatic_posterior_phred = options.at("min-somatic-posterior").as<float>();
            vc_builder.set_min_somatic_posterior(phred_to_probability(min_somatic_posterior_phred));
            
            if (options.at("somatics-only").as<bool>()) {
                vc_builder.set_somatic_only_calls();
            } else {
                vc_builder.set_somatic_and_variant_calls();
            }
        } else if (caller == "trio") {
            vc_builder.set_maternal_sample(options.at("maternal-sample").as<std::string>());
            vc_builder.set_paternal_sample(options.at("paternal-sample").as<std::string>());
        }
        
        const auto contig_ploidies = extract_contig_ploidies(options);
        
        if (!contig_ploidies) {
            // TODO
        }
        
        if (call_sites_only(options)) {
            vc_builder.set_sites_only();
        }
        
        VariantCallerFactory result {std::move(vc_builder), options.at("organism-ploidy").as<unsigned>()};
        
        for (const auto& p : regions) {
            const auto it = std::find_if(std::cbegin(*contig_ploidies), std::cend(*contig_ploidies),
                                         [&] (const auto& cp) { return cp.contig == p.first; });
            if (it != std::cend(*contig_ploidies)) {
                result.set_contig_ploidy(p.first, it->ploidy);
            }
        }
        
        return result;
    }
    
    boost::optional<fs::path> get_final_output_path(const po::variables_map& options)
    {
        Logging::ErrorLogger log {};
        
        const auto input_path = options.at("output").as<std::string>();
        
        if (input_path == "-") {
            return fs::path {input_path}; // Output goes to stdout
        }
        
        const auto resolved_path = resolve_path(input_path, options);
        
        if (!resolved_path) {
            stream(log) << "Could not resolve the path " << input_path
                << " given in the input option output";
            return boost::none;
        }
        
        if (!is_file_writable(*resolved_path)) {
            stream(log) << "The path " << input_path
                << " given in the input option output is not readable";
            return boost::none;
        }
        
        return resolved_path;
    }
    
    VcfWriter make_output_vcf_writer(const po::variables_map& options)
    {
        auto out_path = get_final_output_path(options);
        
        if (out_path) {
            return VcfWriter {*std::move(out_path)};
        }
        
        return VcfWriter {};
    }
    
    boost::optional<fs::path> create_temp_file_directory(const po::variables_map& options)
    {
        const auto working_directory = get_working_directory(options);
        
        if (working_directory) {
            auto result = *working_directory;
            
            const fs::path temp_dir_base_name {"octopus-temp"};
            
            result /= temp_dir_base_name;
            
            constexpr unsigned temp_dir_name_count_limit {10000};
            
            unsigned temp_dir_counter {2};
            
            while (fs::exists(result) && !fs::is_empty(result)
                   && temp_dir_counter <= temp_dir_name_count_limit) {
                result = *working_directory;
                result /= temp_dir_base_name.string() + "-" + std::to_string(temp_dir_counter);
                ++temp_dir_counter;
            }
            
            if (temp_dir_counter > temp_dir_name_count_limit) {
                std::cout << "Too many temprary directories in working directory" << std::endl;
                return boost::none;
            }
            
            if (!fs::create_directory(result)) {
                std::cout << "Could not create temporary directory" << std::endl;
                return boost::none;
            }
            
            return result;
        }
        
        return boost::none;
    }
    } // namespace Options
} // namespace Octopus
