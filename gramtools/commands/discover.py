## @file
# Variant discovery against inferred personalised reference.
#  The use case is the following: a set of reads is mapped to a prg using gram::commands::quasimap.
# Then a haploid personalised reference is inferred using `infer.py`.
# Finally the same set of reads can be mapped to this personalised reference for variant discovery using this module.
#  The output vcf file gets rebased to express variants in the original prg (rather than personalised reference) coordinate space.

import typing
import bisect
import logging
import collections

import vcf
import cortex

from .. import paths

log = logging.getLogger('gramtools')


def parse_args(common_parser, subparsers):
    parser = subparsers.add_parser('discover',
                                   parents=[common_parser])

    parser.add_argument('--run-dir', '--run-directory',
                        help='Common directory for gramtools running commands. Will contain discover outputs.'
                             'The outputs are: the vcf files from the variant callers,'
                             '[their adjucated combination], both native and rebased '
                             '(=expressed in original reference coordinates).',
                        dest='run_dir',
                        type=str,
                        required=True)

    parser.add_argument('--reads',
                        help='Reads files for variant discovery against `infer`.\n'
                             'These should be the same as those used in `quasimap`.'
                             'Those are used by default.',
                        type=str,
                        required=False)


def run(args):
    log.info('Start process: discover')
    _paths = paths.generate_discover_paths(args)

    # call variants using `cortex`
    log.debug('Running cortex')
    cortex.calls(_paths['inferred_fasta'], _paths['reads_files'], _paths['cortex_vcf'])

    # Get the length of the inferred fasta reference; was computed at infer stage.
    with open(_paths['inferred_ref_size']) as f:
        chrom_sizes = [int(s) for s in f.read().split()]

    #  Convert coordinates in personalised reference space to coordinates in (original) prg space.
    log.debug('Rebasing vcf')
    rebased_vcf_records = _rebase_vcf(_paths,
                                      chrom_sizes)

    if rebased_vcf_records is None:
        log.debug("Rebased VCF does not contain records")
        log.info('End process: discover')
        return

    template_vcf_file_path = _paths['cortex_vcf']
    _dump_rebased_vcf(rebased_vcf_records, _paths['rebased_vcf'], template_vcf_file_path)

    log.info('End process: discover. '
             'Rebased vcf in {}'.format(_paths['rebased_vcf']))


## Rebase a vcf so that it uses same reference as base_vcf.
#
# Here is the use case. We have:
#  new_vcf                         new_reference_vcf
#   |                               |
#  new_reference                   old_reference
#
# And we want:
#  new_vcf
#   |
#  old_reference
#
# In the context of gramtools:
#  * `new_vcf` is the vcf produced by cortex, ie variant calls against personalised reference.
# * `new_reference_vcf` is the vcf describing the personalised reference with respect to the original prg.
#  * `old_reference` is the original prg. The first allele of each variant site in the prg is taken for making this.
# * `new_reference` is the personalised reference; it is the original prg with inferred variant alleles instead.
#
# Thus what rebasing achieves is to express the variant calls in terms of the original prg coordinates.
def _rebase_vcf(_paths,
                chrom_sizes,
                check_records = True):
    base_vcf_file_path = _paths["inferred_vcf"]
    personalised_vcf_file_path = _paths["cortex_vcf"]

    if check_records:
        original_refs = load_multi_fasta(_paths["original_reference"])
        inferred_refs = load_multi_fasta(_paths["inferred_fasta"])

    var_unplaced_records = []
    ori_unplaced_records = []
    try:
        # Load the variant calls with respect to the personalised reference.
        variant_call_records = _load_records(personalised_vcf_file_path)
    except StopIteration:
        log.warning("Cortex VCF does not contain novel variants")
        return None
    base_records = _load_records(base_vcf_file_path)


    # Given a variant call position w.r.t the personalised reference, we will need to know how this position maps to the original prg.
    flagged_personalised_regions = _flag_personalised_reference_regions(base_records, chrom_sizes)

    new_vcf_records = []
    for vcf_record in variant_call_records:
        chrom_key = vcf_record.CHROM

        # Check the variant call is properly placed
        if check_records:
            position = vcf_record.POS
            inferred_sequence = inferred_refs[chrom_key]
            if len(inferred_sequence) < position or \
                vcf_record.REF != inferred_sequence[position - 1 : position - 1 + len(vcf_record.REF)]:
                var_unplaced_records.append(str(vcf_record))
                continue

        regions_list = flagged_personalised_regions.get(chrom_key, None)
        assert regions_list is not None, "Error: ref ID {} in vcf record {} is not present in vcf from infer {}".\
            format(chrom_key, vcf_record, base_vcf_file_path)

        new_record = _rebase_vcf_record(vcf_record,regions_list)

        # Check the rebased record is properly placed
        # Note that an error here can be due to the original vcf being inconsistent with the original fasta (at build)
        # This thus ought to be checked then.
        if check_records:
            original_sequence = original_refs[chrom_key]
            position = new_record.POS
            ref_seq = original_sequence[position - 1 : position - 1 + len(vcf_record.REF)].upper()
            if new_record.REF.upper() != ref_seq:
                ori_unplaced_records.append(str(new_record))
                continue

        # It is possible that the var caller calls for reversing the decision made in `infer`
        # In which case we get REF == ALT, and there is no point in writing out the record.
        if not all([new_record.REF == alt for alt in new_record.ALT]):
            new_vcf_records.append(new_record)

    if len(var_unplaced_records) > 0:
        log.warning("The following new variant records were skipped: {} \n"\
                    "Reasons: POS is not a valid position in the inferred reference sequence,"\
                    "or REF does not coincide with inferred reference sequence.".format("\t".join(var_unplaced_records)))

    if len(ori_unplaced_records) > 0:
        log.warning("The following records when rebased, were discordant with original reference: {} \n" \
                    .format("\t".join(ori_unplaced_records)))
    return new_vcf_records


def _dump_rebased_vcf(records, rebase_file_path, template_vcf_file_path):
    template_handle = open(template_vcf_file_path, 'r')
    template_vcf = vcf.Reader(template_handle)

    rebase_handle = open(rebase_file_path, 'w')
    writer = vcf.Writer(rebase_handle, template_vcf)
    for record in records:
        writer.write_record(record)
    writer.close()


## Class that allows mapping between a vcf record in one REF coordinate to a vcf record in another REF coordinate system.
#  Call the two references coordinates ref 2 and ref 1.
#  A `_Region` either marks a region in ref 2 within a variant region in ref 1, or a region in ref 2 within an invariant region in ref 1.
class _Region:
    def __init__(self, base_POS, inf_POS, length, vcf_record_REF=None, vcf_record_ALT=None):
        ## Start coordinate in base reference.
        self.base_POS = base_POS
        ## Start coordinate in inferred reference.
        self.inf_POS = inf_POS
        ## Holds the length of the region
        self.length = length

        ## If in a variant site, the only parts of the vcf_record that we need to track are the REF and ALT sequences.
        self.vcf_record_REF = vcf_record_REF

        self.vcf_record_ALT = vcf_record_ALT

    @property
    def is_site(self):
        return self.vcf_record_REF is not None

    def __eq__(self, other):
        return self.__dict__ == other.__dict__

    def __repr__(self):
        return str(self.__dict__)

    def __lt__(self, other):
        if isinstance(other, _Region):
            return self.inf_POS < other.inf_POS
        else:
            return self.inf_POS < other

    def __len__(self):
        return self.end - self.start + 1


_Regions = typing.List[_Region]
_Regions_Map = typing.Dict[str, _Regions]


## Marks the personalised reference with `Regions` flagging a relationship with the base reference.
#  There are two types of `Regions`: those inside variant sites in the prg, and those outside.
# Important requirements for vcf:
# * the records are sorted by POS for a given ref ID, and all records referring to the same ref ID are contiguous.
# * the chrom_sizes are ordered in same order as the ref IDs in the records.
# The first point is explicitly checked using assertions.
def _flag_personalised_reference_regions(base_records, chrom_sizes) -> _Regions_Map:
    all_personalised_ref_regions = {}  # Maps each CHROM to a list of _Regions
    chrom_positions = {}  # Maps each CHROM to a base_ref and an inferred_ref position
    num_chroms = -1
    prev_chrom_key = None
    prev_record = None

    for vcf_record in base_records:
        chrom_key = vcf_record.CHROM
        if chrom_key not in all_personalised_ref_regions:
            all_personalised_ref_regions[chrom_key] = []
            chrom_positions[chrom_key] = [1, 1]

            # We enter a new ref ID; let's make sure we capture the end of the previous ref ID
            if num_chroms >= 0:
                base_ref_pos, inf_ref_pos = chrom_positions[prev_chrom_key][0], chrom_positions[prev_chrom_key][1]
                if base_ref_pos <= chrom_size:
                    regions_list = all_personalised_ref_regions[prev_chrom_key]
                    non_var_region = _Region(base_POS=base_ref_pos, inf_POS=inf_ref_pos,
                                             length=chrom_size - base_ref_pos + 1)
                    regions_list.append(non_var_region)

            num_chroms += 1
            chrom_size = chrom_sizes[num_chroms]

        else:
            # Enforce ref ID contiguity
            assert chrom_key == prev_chrom_key, "Ref IDs not contiguous: {} and {} interspersed"\
                                                          .format(chrom_key, prev_chrom_key)
            # Enforce position sortedness
            assert vcf_record.POS > prev_record.POS, "Records not in increasing POS order: {} and {}"\
                                                            .format(prev_record, vcf_record)
        regions_list = all_personalised_ref_regions[chrom_key]
        base_ref_pos, inf_ref_pos = chrom_positions[chrom_key][0], chrom_positions[chrom_key][1]

        # Build non-variant region
        if vcf_record.POS > base_ref_pos:
            region_length = vcf_record.POS - base_ref_pos
            if len(regions_list) == 0 or regions_list[-1].is_site:
                non_var_region = _Region(base_POS=base_ref_pos, inf_POS=inf_ref_pos,
                                         length=region_length)
                regions_list.append(non_var_region)
            else: # We saw a site, but it was not used; so, we only need to merge this region with prev region.
                regions_list[-1].length += region_length
            base_ref_pos += region_length
            inf_ref_pos += region_length

        # Build variant region
        # Note the following 'guarantee' from `infer`: the first element of GT is the single most likely haploid genotype call.
        # We need to take that, as that is what gets used to produce the inferred reference fasta- which is the reference used for variant calling.
        picked_alleles = vcf_record.samples[0].gt_alleles

        if set(picked_alleles) == {None}:
            picked_allele = 0  # GT of './.'
        else:
            picked_allele = int(picked_alleles[0])  # Get the first one. Can be 0 (REF) !

        # Only make a var region if the inference procedure did not pick the REF.
        if picked_allele != 0:
            var_region = _Region(base_POS=base_ref_pos, inf_POS=inf_ref_pos,
                                 length=len(vcf_record.ALT[picked_allele - 1]), vcf_record_REF=vcf_record.REF,
                                 vcf_record_ALT=str(vcf_record.ALT[picked_allele - 1]))

            regions_list.append(var_region)
            base_ref_pos += len(vcf_record.REF)
            inf_ref_pos += var_region.length

        chrom_positions[chrom_key] = [base_ref_pos, inf_ref_pos]
        prev_chrom_key = chrom_key
        prev_record = vcf_record

    # End game: deal with the last non-var region if there is one.
    if len(all_personalised_ref_regions) == 0:
        raise ValueError("No records in provided vcf.")

    if base_ref_pos <= chrom_size:
        non_var_region = _Region(base_POS=base_ref_pos, inf_POS=inf_ref_pos,
                                 length=chrom_size - base_ref_pos + 1)
        regions_list.append(non_var_region)

    return all_personalised_ref_regions


## Take all vcf_record class attributes, modify those of interest, and make a new vcf record.
# For valid `new_attributes`, see https://pyvcf.readthedocs.io/en/latest/API.html#vcf-model-record
def _modify_vcf_record(vcf_record, **new_attributes):
    for attribute, value in new_attributes.items():
        if getattr(vcf_record, attribute, None):
            setattr(vcf_record, attribute, value)

    return vcf_record


## Change `vcf_record` to be expressed relative to a different reference.
#  @param vcf_record a representation of a vcf entry;
#  @param personalised_reference_regions a set of `_Regions` marking the personalised reference and its relationship to the prg reference.
# The key idea is to progress through the REF sequence of the vcf_record to rebase, mapping it to the variant/non-variant space of the personalised reference.
def _rebase_vcf_record(vcf_record, personalised_reference_regions: _Regions):

    # Get index of region containing the start of the `vcf_record`.
    region_index = _find_start_region_index(vcf_record, personalised_reference_regions)

    consumed_reference = 0  # Position in the inferred ref. sequence
    reference_length = len(vcf_record.REF)

    # We will iterate over regions until we have consumed all of the vcf_record REF sequence.
    journey_continues = True

    # we will build the rebased_REF as we traverse the regions. We will use the vcf_record ALT and pre-pend and/or post-pend to it if necessary.
    rebased_REF, rebased_ALT = "", str(vcf_record.ALT[0])

    # Let's rebase the position straight away
    first_region = personalised_reference_regions[region_index]

    # Case: hitting variant region. We rebase at the beginning of the variant region.
    if first_region.is_site:
        rebased_POS = first_region.base_POS

        # We also straight away pre-pend any preceding variation relative to the base REF
        if vcf_record.POS > first_region.inf_POS:
            record_inset = vcf_record.POS - first_region.inf_POS
            rebased_ALT = first_region.vcf_record_ALT[:record_inset] + rebased_ALT

    # Case: hitting non-variant region. We rebase at where the vcf_record starts, in base ref coordinates.
    else:
        rebased_POS = first_region.base_POS + (vcf_record.POS - first_region.inf_POS)

    while journey_continues:
        region = personalised_reference_regions[region_index]
        # This is the key step. We check how much of the vcf_record REF (inferred reference) can be consumed by the current region.
        # If the current region can consume at least what is left of the vcf_record REF, loop ends.
        # NOTE that region.length is 'overloaded': if a non-var region, it is the fixed interval between var regions.
        # If a var region, it is the inferred_vcf record's ALT length (REF and ALT lengths can differ).
        consumable = region.length - (
                    vcf_record.POS + consumed_reference - region.inf_POS)  # The amount of the inferred reference that can be consumed by the region

        if consumable >= (reference_length - consumed_reference):
            journey_continues = False
            to_consume = reference_length - consumed_reference
        else:
            to_consume = consumable

        if region.is_site:
            rebased_REF += region.vcf_record_REF  # Copy the whole base REF for the variant site

        else:
            # We can use the vcf_record's REF, as that is also the base REF sequence- because we are in a non-variant site.
            rebased_REF += vcf_record.REF[consumed_reference:consumed_reference + to_consume]

        consumed_reference += to_consume  # Log the progression

        region_index += 1

    # Sanity check: we have consumed the whole of vcf_record.REF.
    assert consumed_reference == len(vcf_record.REF)

    # End game: Deal with the last region. We need to post-pend any succeeding sequence in the ALT record if we finish in a variant site.
    if region.is_site:
        cur_pos = vcf_record.POS + consumed_reference
        # The inset will be < 0 if there is a part of the (inferred vcf record's) ALT which has not been
        inset = cur_pos - (region.inf_POS + region.length)
        if inset < 0:
            rebased_ALT += region.vcf_record_ALT[inset:]

    vcf_record = _modify_vcf_record(vcf_record, POS=rebased_POS, REF=rebased_REF, ALT=[rebased_ALT])

    return vcf_record


## Return the marked `_Region` containing the position referred to by `vcf_record`.
def _find_start_region_index(vcf_record, marked_regions):
    record_start_POS = vcf_record.POS

    # Binary search on a sorted array. The '<' comparator is overloaded in `_Region` definition to allow the search.
    # The produced index is:
    # * If a region with start POS == record_start_POS is found, returns that index (specifically, the first occurrence)
    # * If such a region is not found, returns the index of the region with start POS **strictly** greater than record_start_POS
    # IN THAT CASE, we need to return the region index one smaller; so that the region's interval includes record_start_POS.
    region_index = bisect.bisect_left(marked_regions, record_start_POS)

    #  Case: `record_start_index` is larger than any _Region start.
    if region_index > len(marked_regions) - 1:
        region_index = len(marked_regions) - 1

    selected_region = marked_regions[region_index]

    # Case: record_start_POS was not found exactly.
    if selected_region.inf_POS > record_start_POS:
        region_index -= 1

    return region_index


## Produce a list of vcf records parsed from `vcf_file_path`.
def _load_records(vcf_file_path):
    file_handle = open(vcf_file_path, 'r')
    vcf_reader = vcf.Reader(file_handle)
    all_vcf_records = list(vcf_reader)
    return all_vcf_records


## Generator to always yield the first allele (REF) of a variant site.
def _alwaysRef():
    while True:
        yield 0

def load_multi_fasta(fasta_fname):
    # Load the record fully in memory
    with open(fasta_fname) as r:
        records = collections.OrderedDict()
        record = ""
        new_recomb_name = ""

        for line in r:
            if line[0] == ">":
                if new_recomb_name != "":
                    records[new_recomb_name] = record
                new_recomb_name = line[1:].strip().split()[0]
                record = ""

            else:
                record += line.replace("\n","")

        records[new_recomb_name] = record

    return records

