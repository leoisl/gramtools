#include <cassert>
#include <fstream>
#include <vector>

#include "search/search.hpp"

#include "quasimap/utils.hpp"
#include "quasimap/coverage/allele_base.hpp"


using namespace gram;


SitesAlleleBaseCoverage gram::coverage::generate::allele_base_structure(const PRG_Info &prg_info) {
    uint64_t number_of_variant_sites = get_number_of_variant_sites(prg_info);
    SitesAlleleBaseCoverage allele_base_coverage(number_of_variant_sites);

    const auto min_boundary_marker = 5;

    uint64_t allele_size = 0;
    Marker last_marker = 0;

    // Traverse the sites mask, in order to identify alleles.
    for (const auto &mask_value: prg_info.sites_mask) {
        auto within_allele = mask_value != 0;
        if (within_allele) {
            allele_size += 1;
            last_marker = mask_value;
            continue;
        }

        auto no_allele_to_flush = allele_size == 0;
        if (no_allele_to_flush)
            continue;

        // Store room aside for the allele
        BaseCoverage bases(allele_size);
        uint64_t variant_site_cover_index = (last_marker - min_boundary_marker) / 2;
        allele_base_coverage.at(variant_site_cover_index).emplace_back(bases);
        allele_size = 0;
    }
    return allele_base_coverage;
}


uint64_t gram::allele_start_offset_index(const uint64_t within_allele_prg_index, const PRG_Info &prg_info) {
    uint64_t number_markers_before = prg_info.prg_markers_rank(within_allele_prg_index);
    // Rank operation gets index of nearest left marker in prg, marking allele's start.
    uint64_t marker_index = prg_info.prg_markers_select(number_markers_before);
    uint64_t offset = within_allele_prg_index - marker_index - 1;

    return offset;
}


uint64_t gram::set_site_base_coverage(Coverage &coverage,
                                      SitesCoverageBoundaries &sites_coverage_boundaries,
                                      const VariantLocus &path_element,
                                      const uint64_t allele_coverage_offset,
                                      const uint64_t max_bases_to_set) {
    // Extract the variant site of interest using the variant site marker number.
    auto marker = path_element.first;
    auto min_boundary_marker = 5;
    auto variant_site_coverage_index = (marker - min_boundary_marker) / 2;
    auto &site_coverage = coverage.allele_base_coverage.at(variant_site_coverage_index);

    // Extract the allele of interest using the allele id.
    auto allele_id = path_element.second;
    auto allele_coverage_index = allele_id - 1;
    auto &allele_coverage = site_coverage.at(allele_coverage_index);

    // Now: which bases inside the allele are covered by the read?
    // If `index_end_boundary` gets set to `allele_coverage_offset+max_bases_to_set`, the read ends before the allele's end.
    uint64_t index_end_boundary = std::min(allele_coverage_offset + max_bases_to_set, allele_coverage.size());
    assert(index_end_boundary >= allele_coverage_offset);
    uint64_t count_bases_consumed = index_end_boundary - allele_coverage_offset;

    uint64_t index_start_boundary = allele_coverage_offset;
    bool site_seen_previously = sites_coverage_boundaries.find(path_element) != sites_coverage_boundaries.end();
    // If we have already mapped to this `VariantLocus` before, we allow only to map from the end of the previous mapping onwards.
    // TODO: what if we map the end of the allele previously and now map the whole allele? Here, we could not map the missed bases at the beginning...
    if (site_seen_previously)
        index_start_boundary = std::max(allele_coverage_offset, sites_coverage_boundaries[path_element]);
    sites_coverage_boundaries[path_element] = index_end_boundary; // Update the end_index mapped.

    // Actually increment the base counts between specified ranges.
    for (uint64_t i = index_start_boundary; i < index_end_boundary; ++i) {
        if (allele_coverage[i] == UINT16_MAX)
            continue;

#pragma omp atomic
        ++allele_coverage[i];
    }
    return count_bases_consumed;
}


std::pair<uint64_t, uint64_t> gram::site_marker_prg_indexes(const uint64_t &site_marker, const PRG_Info &prg_info) {
    auto alphabet_rank = prg_info.fm_index.char2comp[site_marker];
    auto first_sa_index = prg_info.fm_index.C[alphabet_rank];
    auto second_sa_index = first_sa_index + 1;

    auto first_prg_index = prg_info.fm_index[first_sa_index];
    auto second_prg_index = prg_info.fm_index[second_sa_index];

    if (first_prg_index < second_prg_index)
        return std::make_pair(first_prg_index, second_prg_index);
    else
        return std::make_pair(second_prg_index, first_prg_index);
}


/**
 * For a given `SearchState`, record all base-level coverage.
 * The complexity of this function is here only to deal with reads that:
 * * Start inside an allele
 * * End inside an allele
 * Otherwise, we just increment all bases inside each traversed allele.
 */
void sa_index_allele_base_coverage(Coverage &coverage,
                                   SitesCoverageBoundaries &sites_coverage_boundaries,
                                   const uint64_t &sa_index,
                                   const uint64_t &read_length,
                                   const SearchState &search_state,
                                   const PRG_Info &prg_info) {
    uint64_t read_bases_consumed = 0;
    uint64_t last_site_marker = 0;
    std::pair<uint64_t, uint64_t> last_site_prg_start_end = std::make_pair(0, 0);
    std::pair<uint64_t, uint64_t> site_prg_start_end = std::make_pair(0, 0);
    auto path_it = search_state.variant_site_path.begin();

    auto read_start_index = prg_info.fm_index[sa_index]; // Where the mapping instance starts in the prg.
    auto start_site_marker = prg_info.sites_mask[read_start_index];
    bool read_starts_within_site = start_site_marker != 0; // Are we inside a variant site?
    if (read_starts_within_site) {
        const auto &path_element = *path_it;
        auto site_marker = path_element.first;
        last_site_prg_start_end = site_marker_prg_indexes(site_marker, prg_info);

        auto allele_coverage_offset = allele_start_offset_index(read_start_index, prg_info);
        auto max_bases_to_set = read_length - read_bases_consumed;
        read_bases_consumed += set_site_base_coverage(coverage,
                                                      sites_coverage_boundaries,
                                                      path_element,
                                                      allele_coverage_offset,
                                                      max_bases_to_set);
        ++path_it;
    } else {
        // Fast-forward to next variant site. Just need to consume bases going up to there.
        const auto &path_element = *path_it;
        auto site_marker = path_element.first;
        site_prg_start_end = site_marker_prg_indexes(site_marker, prg_info);
        read_bases_consumed += site_prg_start_end.first - read_start_index;
    }

    auto last_path_it = search_state.variant_site_path.end();
    while (read_bases_consumed < read_length and path_it != last_path_it) {
        const auto &path_element = *path_it;
        auto site_marker = path_element.first;

        if (last_site_prg_start_end.first != 0) {
            site_prg_start_end = site_marker_prg_indexes(site_marker, prg_info);
            read_bases_consumed += site_prg_start_end.first - last_site_prg_start_end.second - 1;
        }
        last_site_prg_start_end = site_prg_start_end;

        uint64_t allele_coverage_offset = 0;
        auto max_bases_to_set = read_length - read_bases_consumed;
        read_bases_consumed += set_site_base_coverage(coverage,
                                                      sites_coverage_boundaries,
                                                      path_element,
                                                      allele_coverage_offset,
                                                      max_bases_to_set);
        ++path_it;
    }
}


void coverage::record::allele_base(Coverage &coverage,
                                   const SearchStates &search_states,
                                   const uint64_t &read_length,
                                   const PRG_Info &prg_info) {
    SitesCoverageBoundaries sites_coverage_boundaries;

    for (const auto &search_state: search_states) {
        if (search_state.variant_site_path.empty())
            continue;

        auto first_sa_index = search_state.sa_interval.first;
        auto last_sa_index = search_state.sa_interval.second;
        // Record base-level coverage for each mapped instance of the read.
        // TODO: can you have a sa_index bigger than one for SearchState with non empty variant_site_path?
        for (auto sa_index = first_sa_index; sa_index <= last_sa_index; ++sa_index)
            sa_index_allele_base_coverage(coverage,
                                          sites_coverage_boundaries,
                                          sa_index,
                                          read_length,
                                          search_state,
                                          prg_info);
    }
}

/**
 * String serialise the base coverages for one allele.
 */
std::string dump_allele(const BaseCoverage &allele) {
    std::stringstream stream;
    stream << "[";
    auto i = 0;
    for (const auto &base_coverage: allele) {
        stream << (int) base_coverage;
        if (i++ < allele.size() - 1)
            stream << ",";
    }
    stream << "]";
    return stream.str();
}

/**
 * String serialise the alleles of a site.
 * @see dump_allele()
 */
std::string dump_site(const AlleleCoverage &site) {
    std::stringstream stream;
    auto i = 0;
    for (const auto &allele: site) {
        stream << dump_allele(allele);
        if (i++ < site.size() - 1)
            stream << ",";
    }
    return stream.str();
}

/**
 * String serialise all base-level coverages for all sites of the prg.
 * @see dump_site()
 */
std::string dump_sites(const SitesAlleleBaseCoverage &sites) {
    std::stringstream stream;
    auto i = 0;
    for (const auto &site: sites) {
        stream << "[";
        stream << dump_site(site);
        stream << "]";
        if (i++ < sites.size() - 1)
            stream << ",";
    }
    return stream.str();
}

std::string gram::dump_allele_base_coverage(const SitesAlleleBaseCoverage &sites) {
    std::stringstream stream;
    stream << "{\"allele_base_counts\":[";
    stream << dump_sites(sites);
    stream << "]}";
    return stream.str();
}

void coverage::dump::allele_base(const Coverage &coverage,
                                 const Parameters &parameters) {
    std::string json_string = dump_allele_base_coverage(coverage.allele_base_coverage);
    std::ofstream file;
    file.open(parameters.allele_base_coverage_fpath);
    file << json_string << std::endl;
}