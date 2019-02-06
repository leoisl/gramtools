#include <cstdint>
#include <vector>
#include <string>

#include "prg/masks.hpp"


using namespace gram;


sdsl::bit_vector gram::generate_prg_markers_mask(const sdsl::int_vector<> &encoded_prg) {
    sdsl::bit_vector variants_markers_mask(encoded_prg.size(), 0);
    for (uint64_t i = 0; i < encoded_prg.size(); i++)
        variants_markers_mask[i] = encoded_prg[i] > 4;
    return variants_markers_mask;
}

sdsl::bit_vector gram::generate_bwt_markers_mask(const FM_Index &fm_index) {
    sdsl::bit_vector bwt_markers_mask(fm_index.bwt.size(), 0);
    for (uint64_t i = 0; i < fm_index.bwt.size(); i++)
        bwt_markers_mask[i] = fm_index.bwt[i] > 4;
    return bwt_markers_mask;
}


sdsl::int_vector<> gram::load_allele_mask(const Parameters &parameters) {
    sdsl::int_vector<> allele_mask;
    sdsl::load_from_file(allele_mask, parameters.allele_mask_fpath);
    return allele_mask;
}

sdsl::int_vector<> gram::generate_allele_mask(const sdsl::int_vector<> &encoded_prg) {
    sdsl::int_vector<> allele_mask(encoded_prg.size(), 0, 32);
    uint32_t current_allele_id = 1;
    bool within_variant_site = false;

    for (uint64_t i = 0; i < encoded_prg.size(); ++i) {
        const auto &prg_char = encoded_prg[i];
        auto at_variant_site_boundary = prg_char > 4
                                        and prg_char % 2 != 0;
        auto entering_variant_site = at_variant_site_boundary
                                     and not within_variant_site;
        if (entering_variant_site){
            within_variant_site = true;
            current_allele_id = 1;
            continue;
        }

        auto exiting_variant_site = at_variant_site_boundary
                                     and within_variant_site;
        if (exiting_variant_site){
            within_variant_site = false;
            continue;
        }

        auto within_allele = prg_char <= 4 and within_variant_site;
        if (within_allele) {
            allele_mask[i] = current_allele_id;
            continue;
        }

        auto at_allele_marker = prg_char > 4 and prg_char % 2 == 0;
        if (at_allele_marker) {
            current_allele_id++;
            continue;
        }
    }
    sdsl::util::bit_compress(allele_mask);
    return allele_mask;
}


sdsl::int_vector<> gram::load_sites_mask(const Parameters &parameters) {
    sdsl::int_vector<> sites_mask;
    sdsl::load_from_file(sites_mask, parameters.sites_mask_fpath);
    return sites_mask;
}

sdsl::int_vector<> gram::generate_sites_mask(const sdsl::int_vector<> &encoded_prg) {
    sdsl::int_vector<> sites_mask(encoded_prg.size(), 0, 32);
    Marker current_site_marker = 0;
    bool within_variant_site = false;

    for (uint64_t i = 0; i < encoded_prg.size(); ++i) {
        const auto &prg_char = encoded_prg[i];
        auto at_variant_site_boundary = prg_char > 4
                                        and prg_char % 2 != 0;
        auto entering_variant_site = at_variant_site_boundary
                                     and not within_variant_site;
        if (entering_variant_site){
            within_variant_site = true;
            current_site_marker = prg_char;
            continue;
        }

        auto exiting_variant_site = at_variant_site_boundary
                                    and within_variant_site;
        if (exiting_variant_site){
            within_variant_site = false;
            continue;
        }

        auto within_allele = prg_char <= 4 and within_variant_site;
        if (within_allele) {
            sites_mask[i] = current_site_marker;
            continue;
        }
    }
    sdsl::util::bit_compress(sites_mask);
    return sites_mask;
}
