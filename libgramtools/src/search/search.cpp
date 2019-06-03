#include <sdsl/suffix_arrays.hpp>
#include "search/search.hpp"

using namespace gram;

/**
 * A caching object used to temporarily store a single search state
 * @see handle_allele_encapsulated_state()
 */
class SearchStateCache {
public:
    SearchState search_state = {};
    bool empty = true;

    void set(const SearchState &search_state) {
        this->search_state = search_state;
        this->empty = false;
    }

    void flush(SearchStates &search_states) {
        if (this->empty)
            return;
        search_states.emplace_back(this->search_state);
        this->empty = true;
    }

    void update_sa_interval_max(const SA_Index &new_sa_interval_max) {
        assert(not this->empty);
        assert(this->search_state.sa_interval.second + 1 == new_sa_interval_max);
        this->search_state.sa_interval.second = new_sa_interval_max;
    }
};


SearchStates gram::handle_allele_encapsulated_state(const SearchState &search_state,
                                                    const PRG_Info &prg_info) {
    bool has_path = not search_state.variant_site_path.empty();
    assert(not has_path);

    SearchStates new_search_states = {};
    SearchStateCache cache;

    for (uint64_t sa_index = search_state.sa_interval.first;
         sa_index <= search_state.sa_interval.second;
         ++sa_index) {

        auto prg_index = prg_info.fm_index[sa_index];
        auto site_marker = prg_info.sites_mask[prg_index];
        auto allele_id = prg_info.allele_mask[prg_index];

        bool within_site = site_marker != 0;
        if (not within_site) {
            cache.flush(new_search_states);
            cache.set(SearchState{
                    SA_Interval{sa_index, sa_index},
                    VariantSitePath{},
                    SearchVariantSiteState::outside_variant_site
            });
            cache.flush(new_search_states);
            continue;
        }

        //  else: read is completely encapsulated within allele
        if (cache.empty) {
            cache.set(SearchState{
                    SA_Interval{sa_index, sa_index},
                    VariantSitePath{
                            VariantLocus{site_marker, allele_id}
                    },
                    SearchVariantSiteState::within_variant_site
            });
            continue;
        }

        VariantSitePath current_path = {VariantLocus{site_marker, allele_id}};
        bool cache_has_same_path = current_path == cache.search_state.variant_site_path;
        if (cache_has_same_path) {
            cache.update_sa_interval_max(sa_index);
            continue;
        } else {
            cache.flush(new_search_states);
            cache.set(SearchState{
                    SA_Interval{sa_index, sa_index},
                    current_path,
                    SearchVariantSiteState::within_variant_site
            });
        }
    }
    cache.flush(new_search_states);
    return new_search_states;
}

SearchStates gram::handle_allele_encapsulated_states(const SearchStates &search_states,
                                                     const PRG_Info &prg_info) {
    SearchStates new_search_states = {};

    for (const auto &search_state: search_states) {
        bool has_path = not search_state.variant_site_path.empty();
        if (has_path) {
            new_search_states.emplace_back(search_state);
            continue;
        }

        SearchStates split_search_states = handle_allele_encapsulated_state(search_state,
                                                                            prg_info);
        for (const auto &split_search_state: split_search_states)
            new_search_states.emplace_back(split_search_state);
    }
    return new_search_states;
}

SearchStates gram::search_read_backwards(const Pattern &read,
                                         const Pattern &kmer,
                                         const KmerIndex &kmer_index,
                                         const PRG_Info &prg_info) {
    // Test if kmer has been indexed
    bool kmer_in_index = kmer_index.find(kmer) != kmer_index.end();
    if (not kmer_in_index)
        return SearchStates{};

    // Test if kmer has been indexed, but has no search states in prg
    auto kmer_index_search_states = kmer_index.at(kmer);
    if (kmer_index_search_states.empty())
        return kmer_index_search_states;


    // Reverse iterator + skipping through indexed kmer in read
    auto read_begin = read.rbegin();
    std::advance(read_begin, kmer.size());

    SearchStates new_search_states = kmer_index_search_states;

    for (auto it = read_begin; it != read.rend(); ++it) { /// Iterates end to start of read
        const Base &pattern_char = *it;
        new_search_states = process_read_char_search_states(pattern_char,
                                                            new_search_states,
                                                            prg_info);
        // Test if no mapping found upon character extension
        auto read_not_mapped = new_search_states.empty();
        if (read_not_mapped)
            break;
    }

    new_search_states = handle_allele_encapsulated_states(new_search_states, prg_info);
    return new_search_states;
}


/**
 * Backward search followed by check whether the extended searched pattern maps somewhere in the prg.
 */
SearchState search_fm_index_base_backwards(const Base &pattern_char,
                                           const uint64_t char_first_sa_index,
                                           const SearchState &search_state,
                                           const PRG_Info &prg_info) {
    auto next_sa_interval = base_next_sa_interval(pattern_char,
                                                  char_first_sa_index,
                                                  search_state.sa_interval,
                                                  prg_info);
    //  An 'invalid' SA interval (i,j) is defined by i-1=j, which occurs when the read no longer maps anywhere in the prg.
    auto valid_sa_interval = next_sa_interval.first - 1 != next_sa_interval.second;
    if (not valid_sa_interval) { // Create an empty, invalid search state.
        SearchState new_search_state;
        new_search_state.invalid = true;
        return new_search_state;
    }

    auto new_search_state = search_state;
    new_search_state.sa_interval.first = next_sa_interval.first;
    new_search_state.sa_interval.second = next_sa_interval.second;
    return new_search_state;
}

SearchStates gram::process_read_char_search_states(const Base &pattern_char,
                                                   const SearchStates &old_search_states,
                                                   const PRG_Info &prg_info) {
    //  Before extending backward search with next character, check for variant markers in the current SA intervals
    //  This is the v part of vBWT.
    auto post_markers_search_states = process_markers_search_states(old_search_states,
                                                                    prg_info);
    //  Regular backward searching
    auto new_search_states = search_base_backwards(pattern_char,
                                                   post_markers_search_states,
                                                   prg_info);
    return new_search_states;
}


SA_Interval gram::base_next_sa_interval(const Marker &next_char,
                                        const SA_Index &next_char_first_sa_index,
                                        const SA_Interval &current_sa_interval,
                                        const PRG_Info &prg_info) {
    const auto &current_sa_start = current_sa_interval.first;
    const auto &current_sa_end = current_sa_interval.second;

    SA_Index sa_start_offset;
    if (current_sa_start <= 0)
        sa_start_offset = 0;
    else {
        //  TODO: Consider deleting this if-clause, next_char should never be > 4, it probably never runs
        if (next_char > 4)
            sa_start_offset = prg_info.fm_index.bwt.rank(current_sa_start, next_char);
        else {
            sa_start_offset = dna_bwt_rank(current_sa_start,
                                           next_char,
                                           prg_info);
        }
    }

    SA_Index sa_end_offset;
    //  TODO: Consider deleting this if-clause, next_char should never be > 4, it probably never runs
    if (next_char > 4)
        sa_end_offset = prg_info.fm_index.bwt.rank(current_sa_end + 1, next_char);
    else {
        sa_end_offset = dna_bwt_rank(current_sa_end + 1,
                                     next_char,
                                     prg_info);
    }

    auto new_start = next_char_first_sa_index + sa_start_offset;
    auto new_end = next_char_first_sa_index + sa_end_offset - 1;
    return SA_Interval{new_start, new_end};
}

SearchStates gram::search_base_backwards(const Base &pattern_char,
                                         const SearchStates &search_states,
                                         const PRG_Info &prg_info) {
    // Compute the first occurrence of `pattern_char` in the suffix array. Necessary for backward search.
    auto char_alphabet_rank = prg_info.fm_index.char2comp[pattern_char];
    auto char_first_sa_index = prg_info.fm_index.C[char_alphabet_rank];

    SearchStates new_search_states = {};

    for (const auto &search_state: search_states) {
        SearchState new_search_state = search_fm_index_base_backwards(pattern_char,
                                                                      char_first_sa_index,
                                                                      search_state,
                                                                      prg_info);
        if (new_search_state.invalid)
            continue;
        new_search_states.emplace_back(new_search_state);
    }

    return new_search_states;
}


SearchStates gram::process_markers_search_states(const SearchStates &old_search_states,
                                                 const PRG_Info &prg_info) {
    SearchStates new_search_states = old_search_states;
    SearchStates all_markers_new_search_states;
    for (const auto &search_state: old_search_states) {
        auto markers_search_states = process_markers_search_state(search_state, prg_info);
        all_markers_new_search_states.splice(all_markers_new_search_states.end(),
                                             markers_search_states);
    }
    new_search_states.splice(new_search_states.end(),
                             all_markers_new_search_states);
    return new_search_states;
}


struct SiteBoundaryMarkerInfo {
    bool is_start_boundary = false;
    SA_Interval sa_interval;
    Marker marker_char;
};


/**
 * Generates information about a site marker using the character after it in the prg and the marker site ID.
 * Finds the marker's SA interval and whether it marks the start or the end of the variant site.
 */
SiteBoundaryMarkerInfo site_boundary_marker_info(const Marker &marker_char,
                                                 const SA_Index &sa_right_of_marker,
                                                 const PRG_Info &prg_info) {

    // char2comp -> rank of ordered alphabet set
    auto alphabet_rank = prg_info.fm_index.char2comp[marker_char];
    auto first_sa_index = prg_info.fm_index.C[alphabet_rank];

    uint64_t marker_sa_index_offset;
    //  TODO: how can sa_right_of_marker be 0? how can it be <0?
    if (sa_right_of_marker <= 0)
        marker_sa_index_offset = 0;
    else
        // The offset is calculated as it would be during a backward search, using BWT
        // Note that the rank query is **non-inclusive** of arg1
        marker_sa_index_offset = prg_info.fm_index.bwt.rank(sa_right_of_marker,
                                                            marker_char);
    // The marker is found by updating the SA interval as for a backward search
    auto marker_sa_index = first_sa_index + marker_sa_index_offset;

    // Get marker PRG index
    const auto marker_text_idx = prg_info.fm_index[marker_sa_index];

    // Get other site marker PRG index
    uint64_t other_marker_text_idx;
    if (marker_sa_index == first_sa_index)
        other_marker_text_idx = prg_info.fm_index[first_sa_index + 1];
    else
        other_marker_text_idx = prg_info.fm_index[first_sa_index];

    // If the marker of interest's position is lower than the other marker, the marker is at the start of the variant site.
    const bool marker_is_boundary_start = marker_text_idx <= other_marker_text_idx;
    return SiteBoundaryMarkerInfo{
            marker_is_boundary_start,
            SA_Interval{marker_sa_index, marker_sa_index},
            marker_char
    };
}

/**
 * Computes the full SA interval of a given allele marker.
 */
SA_Interval gram::get_allele_marker_sa_interval(const Marker &site_marker_char,
                                                const PRG_Info &prg_info) {
    const auto allele_marker_char = site_marker_char + 1;
    const auto alphabet_rank = prg_info.fm_index.char2comp[allele_marker_char];
    const auto start_sa_index = prg_info.fm_index.C[alphabet_rank];

    const auto next_boundary_marker =
            allele_marker_char + 1; // TODO: this assumes a continuous integer ordering of the site markers...

    // sigma: is the size (=number of unique symbols) of the alphabet
    const auto max_alphabet_char = prg_info.fm_index.comp2char[prg_info.fm_index.sigma - 1];

    // Check the next variant site marker exists.
    // `max_alphabet_char` is an allele marker and so cannot be equal to `next_boundary_marker` which is a site marker.
    const bool next_boundary_marker_valid = next_boundary_marker < max_alphabet_char;

    SA_Index end_sa_index;
    if (next_boundary_marker_valid) { //  Condition: the allele marker is not the largest existing allele marker number.
        const auto next_boundary_marker_rank =
                prg_info.fm_index.char2comp[next_boundary_marker];
        const auto next_boundary_marker_start_sa_index =
                prg_info.fm_index.C[next_boundary_marker_rank];
        end_sa_index = next_boundary_marker_start_sa_index - 1;
    } else { // If it does not exist, the last prg position is variant site exit point.
        end_sa_index = prg_info.fm_index.size() - 1;
    }
    return SA_Interval{start_sa_index, end_sa_index};
}

AlleleId gram::get_allele_id(const SA_Index &allele_marker_sa_index,
                             const PRG_Info &prg_info) {
    //  What is the index of the character just before the marker allele in the original text?
    auto internal_allele_text_index = prg_info.fm_index[allele_marker_sa_index] - 1;
    auto allele_id = (AlleleId) prg_info.allele_mask[internal_allele_text_index];
    assert(allele_id > 0);
    return allele_id;
}

/**
 * Given an allele (=even) marker SA interval, make one search state for each index in that interval.
 * The allele SA interval is broken up into distinct search states to record the path taken through each allele.
 */
SearchStates get_allele_search_states(const Marker &site_boundary_marker,
                                      const SA_Interval &allele_marker_sa_interval,
                                      const SearchState &current_search_state,
                                      const PRG_Info &prg_info) {
    SearchStates search_states = {};

    const auto first_sa_interval_index = allele_marker_sa_interval.first;
    const auto last_sa_interval_index = allele_marker_sa_interval.second;

    for (auto allele_marker_sa_index = first_sa_interval_index;
         allele_marker_sa_index <= last_sa_interval_index;
         ++allele_marker_sa_index) {

        SearchState search_state = current_search_state;
        search_state.sa_interval.first = allele_marker_sa_index;
        search_state.sa_interval.second = allele_marker_sa_index;

        search_state.variant_site_state
                = SearchVariantSiteState::within_variant_site;

        // Populate the cache with which site/allele combination the `SearchState` maps into.
        auto allele_number = get_allele_id(allele_marker_sa_index,
                                           prg_info); // The alleles are not sorted by ID in the SA, need a specific routine.

        search_state.variant_site_path.push_front(VariantLocus{site_boundary_marker,allele_number});

        search_states.emplace_back(search_state);
    }
    return search_states;
}

/**
 * Deals with the last allele in a variant site, which is terminated by a site (=odd) marker
 * A search state has to be created for this allele separately from the other allele search states (constructed in `get_allele_search_states`)
 */
SearchState get_site_search_state(const AlleleId &final_allele_id,
                                  const SiteBoundaryMarkerInfo &boundary_marker_info,
                                  const SearchState &current_search_state,
                                  const PRG_Info &prg_info) {
    // Update the `SearchState` which hit the site marker with the site marker's exit point.
    SearchState search_state = current_search_state;
    search_state.sa_interval.first = boundary_marker_info.sa_interval.first;
    search_state.sa_interval.second = boundary_marker_info.sa_interval.second;

    search_state.variant_site_state
            = SearchVariantSiteState::within_variant_site;

    search_state.variant_site_path.push_front(VariantLocus{boundary_marker_info.marker_char,final_allele_id});

    return search_state;
}


/**
 * Compute number of alleles in a site from the allele marker's full SA interval.
 */
uint64_t get_number_of_alleles(const SA_Interval &allele_marker_sa_interval) {
    auto num_allele_markers = allele_marker_sa_interval.second
                              - allele_marker_sa_interval.first
                              + 1;
    // The allele marker's full SA interval does not include the variant site exit point, which also marks the last allele's end point.
    auto num_alleles = num_allele_markers + 1;
    return num_alleles;
}

/**
 * Deals with a read mapping into a variant site's end point.
 * The SA index of each allele's end gets added as a new SearchState.
 * Because a variant site end is found, the read needs to be able to map through all alleles of this site.
 */
SearchStates entering_site_search_states(const SiteBoundaryMarkerInfo &boundary_marker_info,
                                         const SearchState &current_search_state,
                                         const PRG_Info &prg_info) {
    // Get full SA interval of the corresponding allele marker.
    auto allele_marker_sa_interval =
            get_allele_marker_sa_interval(boundary_marker_info.marker_char,
                                          prg_info);
    // Get one `SearchState` per allele in the site, with populated cache.
    auto new_search_states = get_allele_search_states(boundary_marker_info.marker_char,
                                                      allele_marker_sa_interval,
                                                      current_search_state,
                                                      prg_info);

    // One more SA interval needs to be added: that of the final allele in the site.
    auto final_allele_id = get_number_of_alleles(
            allele_marker_sa_interval); // Used for populating the `SearchState`'s cache.
    auto site_search_state = get_site_search_state(final_allele_id,
                                                   boundary_marker_info,
                                                   current_search_state,
                                                   prg_info);
    new_search_states.emplace_back(site_search_state);
    return new_search_states;
}


/**
 * Deals with a read mapping leaving a variant site.
 * Create a new `SearchState` with SA interval the index of the site variant's entry point.
 * Populate the cache with variant path taken, if such information was not yet recorded.
 * @note we need to check whether we have previously entered the site.
 * For an explanation why, @see process_allele_marker()
 */
SearchState exiting_site_search_state(const SiteBoundaryMarkerInfo &boundary_marker_info,
                                      const SearchState &current_search_state,
                                      const PRG_Info &prg_info) {
    SearchState new_search_state = current_search_state;

    // A check is required if we do not have certainty that we have previously entered the variant site.
    bool check_required = new_search_state.variant_site_state != SearchVariantSiteState::within_variant_site;
    if (check_required) {

        bool started_in_site = new_search_state.variant_site_path.empty();
        if (started_in_site){
            const auto boundary_marker_char = boundary_marker_info.marker_char;
            auto allele_id = 1; // We are at site exit point mapping backwards, so at first allele of variant site.
            new_search_state.variant_site_path.push_front(VariantLocus{boundary_marker_char,allele_id});
        }
    }

    new_search_state.sa_interval.first = boundary_marker_info.sa_interval.first;
    new_search_state.sa_interval.second = boundary_marker_info.sa_interval.second;
    new_search_state.variant_site_state
            = SearchVariantSiteState::outside_variant_site;

    return new_search_state;
}

MarkersSearchResults gram::left_markers_search(const SearchState &search_state,
                                               const PRG_Info &prg_info) {
    MarkersSearchResults markers_search_results;

    const auto &sa_interval = search_state.sa_interval;

    for (int index=sa_interval.first; index<= sa_interval.second; index++) {
        if (prg_info.bwt_markers_mask[index] == 0) continue;

        auto marker = prg_info.fm_index.bwt[index];
        auto search_result = std::make_pair(index, marker);
        markers_search_results.emplace_back(search_result);
    }

    return markers_search_results;
}

/**
 * Generates new SearchStates from a variant site marker, based on whether it marks the start or the
 * end of the variant site.
 */
SearchStates process_boundary_marker(const Marker &marker_char,
                                     const SA_Index &sa_right_of_marker,
                                     const SearchState &current_search_state,
                                     const PRG_Info &prg_info) {
    //  Have a look at the site boundary marker, and find if it marks the start or the end of the site.
    auto boundary_marker_info = site_boundary_marker_info(marker_char,
                                                          sa_right_of_marker,
                                                          prg_info);

    bool entering_variant_site = not boundary_marker_info.is_start_boundary;
    if (entering_variant_site) {
        auto new_search_states = entering_site_search_states(boundary_marker_info,
                                                             current_search_state,
                                                             prg_info);
        return new_search_states;
    }

    // Case: exiting a variant site. A single SearchState, the SA index of the site entry point, is returned.
    bool exiting_variant_site = boundary_marker_info.is_start_boundary;
    if (exiting_variant_site) {
        auto new_search_state = exiting_site_search_state(boundary_marker_info,
                                                          current_search_state,
                                                          prg_info);
        return SearchStates{new_search_state};
    }
}

/**
 * Procedure for exiting a variant site due to having hit an allele marker.
 * Builds a size 1 SA interval corresponding to the entry point of the corresponding site marker.
 * @note we need to check whether we have previously entered the site.
 * If we have not, this can be due to two things:
 * 1. We started mapping from inside the variant site. In which case, we need to record traversing this site.
 * 2. We started mapping from outside the variant site, went in- and recorded traversal.
 *  But the information of being within_site was lost when serialising the kmer index to disk.
 *  We do not want to duplicate recording this site.
 *
 * Checking whether we have never recorded traversing a single site, means that we started in-site, and so we record traversal (case 1).
 * Conversely, if if we have ever recorded traversing a site, we know it has been committed to the variant site path, so we do not record (case 2).
 * @see exiting_site_search_state()
 */
SearchState process_allele_marker(const Marker &allele_marker_char,
                                  const SA_Index &sa_right_of_marker,
                                  const SearchState &current_search_state,
                                  const PRG_Info &prg_info) {

    //  end of allele found, skipping to variant site start boundary marker
    const Marker &boundary_marker_char = allele_marker_char - 1;

    auto alphabet_rank = prg_info.fm_index.char2comp[boundary_marker_char];
    auto first_sa_index = prg_info.fm_index.C[alphabet_rank];
    auto second_sa_index = first_sa_index + 1; // Variant site markers are adjacent in the suffix array.

    // Determine which SA index position marks the variant site entrance by comparing the prg indices.
    SA_Index boundary_start_sa_index;
    bool boundary_start_is_first_sa = prg_info.fm_index[first_sa_index]
                                      < prg_info.fm_index[second_sa_index];
    if (boundary_start_is_first_sa)
        boundary_start_sa_index = first_sa_index;
    else
        boundary_start_sa_index = second_sa_index;

    auto new_search_state = current_search_state;

    // A check is required if we do not have certainty that we have previously entered the variant site.
    bool check_required = new_search_state.variant_site_state != SearchVariantSiteState::within_variant_site;
    if (check_required) {

        bool started_in_site = new_search_state.variant_site_path.empty();
        if (started_in_site){
            auto internal_allele_text_index = prg_info.fm_index[sa_right_of_marker];
            auto allele_id = (AlleleId) prg_info.allele_mask[internal_allele_text_index]; // Query the allele mask with the prg position of the character to the right of the allele marker.
            new_search_state.variant_site_path.push_front(VariantLocus{boundary_marker_char,allele_id});
        }
    }

    new_search_state.sa_interval.first = boundary_start_sa_index;
    new_search_state.sa_interval.second = boundary_start_sa_index;
    new_search_state.variant_site_state = SearchVariantSiteState::outside_variant_site;

    return new_search_state;
}

SearchStates gram::process_markers_search_state(const SearchState &current_search_state,
                                                const PRG_Info &prg_info) {
    const auto markers = left_markers_search(current_search_state,
                                             prg_info);
    if (markers.empty())
        return SearchStates{};

    SearchStates markers_search_states = {};

    for (const auto &marker: markers) {
        const auto &sa_right_of_marker = marker.first;
        const auto &marker_char = marker.second;

        const bool marker_is_site_boundary = marker_char % 2 == 1; // Test marker is odd.

        //case: entering or exiting a variant site
        if (marker_is_site_boundary) {
            auto new_search_states = process_boundary_marker(marker_char,
                                                             sa_right_of_marker,
                                                             current_search_state,
                                                             prg_info);
            markers_search_states.splice(markers_search_states.end(), new_search_states);
        }
            // case: the marker is an allele marker. We need to exit the variant site.
        else {
            auto new_search_state = process_allele_marker(marker_char,
                                                          sa_right_of_marker,
                                                          current_search_state,
                                                          prg_info);
            markers_search_states.emplace_back(new_search_state);
        }
    }

    return markers_search_states;
}


std::string gram::serialize_search_state(const SearchState &search_state) {
    std::stringstream ss;
    ss << "****** Search State ******" << std::endl;

    ss << "SA interval: ["
       << search_state.sa_interval.first
       << ", "
       << search_state.sa_interval.second
       << "]";
    ss << std::endl;

    if (not search_state.variant_site_path.empty()) {
        ss << "Variant site path [marker, allele id]: " << std::endl;
        for (const auto &variant_site: search_state.variant_site_path) {
            auto marker = variant_site.first;

            if (variant_site.second != 0) {
                const auto &allele_id = variant_site.second;
                ss << "[" << marker << ", " << allele_id << "]" << std::endl;
            }
        }
    }
    ss << "****** END Search State ******" << std::endl;
    return ss.str();
}


std::ostream &gram::operator<<(std::ostream &os, const SearchState &search_state) {
    os << serialize_search_state(search_state);
    return os;
}
