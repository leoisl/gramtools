#ifndef GRAMTOOLS_MAP_HPP
#define GRAMTOOLS_MAP_HPP

uint64_t map_festa(Parameters &params, MasksParser &masks,
                   KmerIdx &kmer_idx, KmerIdx &kmer_idx_rev,
                   KmerSites &kmer_sites, KmersRef &kmers_in_ref, CSA &csa);


bool convert_festa_to_int_seq(GenomicRead *festa_read, std::vector<uint8_t> &readin_integer_seq);


void process_festa_sequence(GenomicRead *festa_read, std::vector<uint8_t> &readin_integer_seq, Parameters &params,
                            MasksParser &masks, int &count_reads, KmerIdx &kmer_idx, KmerIdx &kmer_idx_rev,
                            KmerSites &kmer_sites, KmersRef &kmers_in_ref, uint64_t &count_mapped, CSA &csa);

void output_allele_coverage(Parameters &params, MasksParser &masks);

#endif //GRAMTOOLS_MAP_HPP
