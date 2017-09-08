#include <cctype>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "gtest/gtest.h"

#include "utils.hpp"
#include "prg.hpp"
#include "kmers.hpp"


TEST(GeneratePrecalc, GivenDataForSinglePrecalcEntry_CorrectDumpRowGenerated) {
    const Kmer kmer = {1, 2, 3, 4};
    const NonVariantKmers nonvar_kmers = {kmer};

    Site first_site = {
            VariantSite(5, {9, 8, 7}),
            VariantSite(7, {19, 18, 17})
    };
    Site second_site = {
            VariantSite(9, {29, 28, 27}),
            VariantSite(11, {39, 38, 37})
    };
    Sites sites = {first_site, second_site};
    const KmerSites kmer_sites = {
            {kmer, sites},
    };

    const SA_Intervals sa_intervals = {
            {123, 456},
            {789, 424}
    };

    const auto result = dump_kmer_index_entry(kmer,
                                              sa_intervals,
                                              nonvar_kmers,
                                              kmer_sites);
    const auto expected = "1 2 3 4|1|123 456 789 424||5 9 8 7 @7 19 18 17 @|9 29 28 27 @11 39 38 37 @|";
    EXPECT_EQ(result, expected);
}


TEST(GeneratePrecalc, GivenSites_DumpSitesCorrectly) {
    Site first_site = {
            VariantSite(5, {9, 8, 7}),
            VariantSite(7, {19, 18, 17})
    };
    Site second_site = {
            VariantSite(9, {29, 28, 27}),
            VariantSite(11, {39, 38, 37})
    };
    Sites sites = {first_site, second_site};

    const Kmer kmer = {1, 2, 3, 4};
    const KmerSites kmer_sites = {
            {kmer, sites},
    };
    const auto result = dump_sites(kmer, kmer_sites);
    const auto expected = "5 9 8 7 @7 19 18 17 @|9 29 28 27 @11 39 38 37 @|";
    EXPECT_EQ(result, expected);
}


TEST(GeneratePrecalc, GivenSaIntervals_DumpSaIntervalsStringCorrectly) {
    SA_Intervals sa_intervals = {
            SA_Interval(1, 2),
            SA_Interval(3, 4)
    };
    const auto result = dump_sa_intervals(sa_intervals);
    const auto expected = "1 2 3 4";
    EXPECT_EQ(result, expected);
}


TEST(GeneratePrecalc, GivenKmer_DumpKmerStringCorrectly) {
    const Kmer kmer = {1, 2, 3, 4};
    const auto result = dump_kmer(kmer);
    const auto expected = "1 2 3 4";
    EXPECT_EQ(result, expected);
}


TEST(GeneratePrecalc, GivenDnaString_DnaBasesEncodedCorrectly) {
    const auto dna_str = "AAACCCGGGTTTACGT";
    const auto result = encode_dna_bases(dna_str);
    const std::vector<uint8_t> expected = {
            1, 1, 1,
            2, 2, 2,
            3, 3, 3,
            4, 4, 4,
            1, 2, 3, 4,
    };
    EXPECT_EQ(result, expected);
}


TEST(ParsePrecalc, GivenEncodedKmerString_CorrectlyParsed) {
    const auto encoded_kmer_str = "3 4 2 1 1 3 1 1 2";
    const auto result = parse_encoded_kmer(encoded_kmer_str);
    const Kmer expected = {3, 4, 2, 1, 1, 3, 1, 1, 2};
    EXPECT_EQ(result, expected);
}


TEST(ParsePrecalc, GivenSaIntervalsString_CorrectlyParsed) {
    const auto full_sa_intervals_str = "352511 352512 352648 352649 352648 352649";
    const auto result = parse_sa_intervals(full_sa_intervals_str);

    SA_Intervals expected{
            {352511, 352512},
            {352648, 352649},
            {352648, 352649},
    };
    EXPECT_EQ(result, expected);
}


TEST(ParsePrecalc, GivenTwoSites_CorrectSiteStructGenerated) {
    Site expected = {
            VariantSite(5, {9, 8, 7}),
            VariantSite(7, {19, 18, 17})
    };

    const auto precalc_kmer_entry = "5 9 8 7 @7 19 18 17";
    const std::vector<std::string> &parts = split(precalc_kmer_entry, "|");
    const auto &result = parse_site(parts[0]);
    EXPECT_EQ(result, expected);
}


TEST(ParsePrecalc, GivenSitesTrailingAt_TrailingAtIgnored) {
    Site expected = {
            VariantSite(5, {9, 8, 7}),
            VariantSite(7, {19, 18, 17})
    };

    const auto precalc_kmer_entry = "5 9 8 7 @7 19 18 17 @";
    const std::vector<std::string> &parts = split(precalc_kmer_entry, "|");
    const auto &result = parse_site(parts[0]);
    EXPECT_EQ(result, expected);
}


class IndexKmers : public ::testing::Test {

protected:
    std::string prg_fpath;

    virtual void SetUp() {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        const auto uuid_str = boost::lexical_cast<std::string>(uuid);
        prg_fpath = "./prg_" + uuid_str;
    }

    virtual void TearDown() {
        std::remove(prg_fpath.c_str());
    }

    FM_Index fm_index_from_raw_prg(const std::string &prg_raw) {
        std::vector<uint64_t> prg = encode_prg(prg_raw);
        dump_encoded_prg(prg, prg_fpath);
        FM_Index fm_index;
        // TODO: constructing from memory with sdsl::construct_im appends 0 which corrupts
        sdsl::construct(fm_index, prg_fpath, 8);
        return fm_index;
    }

};


TEST_F(IndexKmers, KmerCrossesVariantRegion_KmerNotInNonVariantRegionSet) {
    const std::string prg_raw = "aca5g6t5gcatt";
    auto kmer = encode_dna_bases("atgca");
    const FM_Index &fm_index = fm_index_from_raw_prg(prg_raw);
    const DNA_Rank &rank_all = calculate_ranks(fm_index);
    const uint64_t max_alphabet = max_alphabet_num(prg_raw);
    const std::vector<int> allele_mask = generate_allele_mask(prg_raw);

    Kmers kmers = {
            {kmer}
    };

    KmerSA_Intervals sa_intervals_map;
    KmerSites sites_map;
    NonVariantKmers nonvar_kmers;

    index_kmers(kmers,
                sa_intervals_map,
                sites_map,
                nonvar_kmers,
                max_alphabet,
                allele_mask,
                rank_all,
                fm_index);

    auto &result = nonvar_kmers;
    NonVariantKmers expected = {};
    EXPECT_EQ(result, expected);
}


TEST_F(IndexKmers, KmerInNonVariantRegion_KmerIncludedInNonVarKmerSet) {
    const std::string prg_raw = "aca5g6t5gcatt";
    auto kmer = encode_dna_bases("atgca");
    const FM_Index &fm_index = fm_index_from_raw_prg(prg_raw);
    const DNA_Rank &rank_all = calculate_ranks(fm_index);
    const uint64_t max_alphabet = max_alphabet_num(prg_raw);
    const std::vector<int> allele_mask = generate_allele_mask(prg_raw);

    Kmers kmers = {
            {kmer}
    };

    KmerSA_Intervals sa_intervals_map;
    KmerSites sites_map;
    NonVariantKmers nonvar_kmers;

    index_kmers(kmers,
                sa_intervals_map,
                sites_map,
                nonvar_kmers,
                max_alphabet,
                allele_mask,
                rank_all,
                fm_index);

    auto &result = nonvar_kmers;
    NonVariantKmers expected = {};
    EXPECT_EQ(result, expected);
}


TEST_F(IndexKmers, KmerCrossesSecondAllele_VariantRegionRecordedInSites) {
    const std::string prg_raw = "aca5g6t5gcatt";
    auto kmer = encode_dna_bases("atgca");
    const FM_Index &fm_index = fm_index_from_raw_prg(prg_raw);
    const DNA_Rank &rank_all = calculate_ranks(fm_index);
    const uint64_t max_alphabet = max_alphabet_num(prg_raw);
    const std::vector<int> allele_mask = generate_allele_mask(prg_raw);

    Kmers kmers = {
            {kmer}
    };

    KmerSA_Intervals sa_intervals_map;
    KmerSites sites_map;
    NonVariantKmers nonvar_kmers;

    index_kmers(kmers,
                sa_intervals_map,
                sites_map,
                nonvar_kmers,
                max_alphabet,
                allele_mask,
                rank_all,
                fm_index);

    auto &result = sites_map[kmer];
    Sites expected = {
            { VariantSite(5, {2}) }
    };
    EXPECT_EQ(result, expected);
}


TEST_F(IndexKmers, KmerCrossesFirstAllele_VariantRegionRecordedInSites) {
    const std::string prg_raw = "aca5g6t5gcatt";
    auto kmer = encode_dna_bases("aggca");
    const FM_Index &fm_index = fm_index_from_raw_prg(prg_raw);
    const DNA_Rank &rank_all = calculate_ranks(fm_index);
    const uint64_t max_alphabet = max_alphabet_num(prg_raw);
    const std::vector<int> allele_mask = generate_allele_mask(prg_raw);

    Kmers kmers = {
            {kmer}
    };

    KmerSA_Intervals sa_intervals_map;
    KmerSites sites_map;
    NonVariantKmers nonvar_kmers;

    index_kmers(kmers,
                sa_intervals_map,
                sites_map,
                nonvar_kmers,
                max_alphabet,
                allele_mask,
                rank_all,
                fm_index);

    auto &result = sites_map[kmer];
    Sites expected = {
            { VariantSite(5, {1}) }
    };
    EXPECT_EQ(result, expected);
}