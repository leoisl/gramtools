## @file
#  Build/load a population reference genome and set it up for quasimapping.
# Either a vcf/reference is passed and a prg generated from it, or an existing prg is passed.
# Once the prg is stored the back-end `build` routine is called, producing the encoded prg, its fm-index, and other supporting data structures.
import os
import time
import json
import shutil
import logging
import subprocess
import collections

import cluster_vcf_records
import vcf

from .. import version
from .. import common
from .. import paths

log = logging.getLogger('gramtools')


def parse_args(common_parser, subparsers):
    parser = subparsers.add_parser('build',
                                   parents=[common_parser])
    parser.add_argument('--gram-dir', '--gram-directory',
                        help='',
                        dest='gram_dir',
                        type=str,
                        required=True)

    parser.add_argument('--vcf',
                        help='File containing variant information to capture in the prg.',
                        nargs="+",
                        action="append",
                        type=str)
    parser.add_argument('--reference',
                        help='Reference the vcf file refers to, used to build non-variant parts of the prg.',
                        type=str,
                        required=False)
    parser.add_argument('--prg',
                        help='A prg can be passed in directly instead of a vcf/reference combination.',
                        type=str,
                        required=False)

    parser.add_argument('--kmer-size',
                        help='Kmer size for indexing the prg. Defaults to 5.',
                        type=int,
                        default=5,
                        required=False)

    parser.add_argument('--max-read-length',
                        help='',
                        type=int,
                        default=150,
                        required=False)

    # The current default behaviour is to extract only relevant kmers from prg.
    parser.add_argument('--all-kmers',
                        help='Whether or not all kmers of given size should be indexed.\n'
                             'When this flag is not used, only kmers overlapping variant sites in prg will be indexed.',
                        action='store_true',
                        required=False)

    parser.add_argument('--max-threads',
                        help='',
                        type=int,
                        default=1,
                        required=False)


def run(args):
    log.info('Start process: build')

    start_time = str(time.time()).split('.')[0]

    command_paths = paths.generate_build_paths(args)

    if type(command_paths['vcf']) is list:  # Meaning: more than one vcf filepath provided at command line.
        # Update the vcf path to a combined vcf from all those provided.
        command_paths = _handle_multi_vcf(command_paths)

    report = collections.OrderedDict()

    if hasattr(args, 'prg') and args.prg is not None:
        report = _skip_prg_construction(command_paths, report, args)
    else:
        report = _execute_command_generate_prg(command_paths, report, args)
        paths.perl_script_file_cleanup(command_paths)

    report = _execute_gramtools_cpp_build(command_paths, report, args)

    log.debug('Computing sha256 hash of project paths')
    command_hash_paths = common.hash_command_paths(command_paths)

    log.debug('Saving command report:\n%s', command_paths['build_report'])
    _report = collections.OrderedDict([
        ('kmer_size', args.kmer_size),
        ('max_read_length', args.max_read_length)
    ])
    report.update(_report)

    report_file_path = command_paths['build_report']
    _save_report(start_time,
                 report,
                 command_paths,
                 command_hash_paths,
                 report_file_path)

    log.info('End process: build')

    if report.get('return_value_is_0') is False:
        exit(1)


## Checks prg file exists and copies it to gram directory.
def _skip_prg_construction(build_paths, report, args):
    if report.get('return_value_is_0') is False:
        report['prg_build_report'] = {
            'return_value_is_0': False
        }
        return report

    if not os.path.isfile(args.prg):
        log.error("PRG argument provided and file not found:\n%s", args.prg)
        success = False
    else:
        log.debug("PRG file provided, skipping construction")
        log.debug("Copying PRG file into gram directory")
        shutil.copyfile(args.prg, build_paths['prg'])
        success = True

    report['return_value_is_0'] = success
    report['prg_build_report'] = {
        'return_value_is_0': success
    }
    return report


## Calls perl utility that converts a vcf and fasta reference into a linear prg.
def _execute_command_generate_prg(build_paths, report, _):
    if report.get('return_value_is_0') is False:
        report['prg_build_report'] = {
            'return_value_is_0': False
        }
        return report

    command = [
        'perl', common.prg_build_exec_fpath,
        '--outfile', build_paths['perl_generated'],
        '--vcf', build_paths['vcf'],
        '--ref', build_paths['original_reference'],
    ]
    command_str = ' '.join(command)
    log.debug('Executing command:\n\n%s\n', command_str)
    timer_start = time.time()

    current_working_directory = os.getcwd()
    process_handle = subprocess.Popen(command,
                                      cwd=current_working_directory,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE)
    process_result = common.handle_process_result(process_handle)
    timer_end = time.time()
    log.debug('Finished executing command: %.3f seconds',
              timer_end - timer_start)

    command_result, entire_stdout = process_result

    report['return_value_is_0'] = command_result
    report['prg_build_report'] = collections.OrderedDict([
        ('command', command_str),
        ('return_value_is_0', command_result),
        ('stdout', entire_stdout),
    ])
    return report


## Executes `gram build` backend.
def _execute_gramtools_cpp_build(build_paths, report, args):
    if report.get('return_value_is_0') is False:
        report['gramtools_cpp_build'] = {
            'return_value_is_0': False
        }
        return report

    command = [
        common.gramtools_exec_fpath,
        'build',
        '--gram', build_paths['gram_dir'],
        '--kmer-size', str(args.kmer_size),
        '--max-read-size', str(args.max_read_length),
        '--max-threads', str(args.max_threads),
    ]

    if args.all_kmers:
        command.append('--all-kmers')

    if args.debug:
        command += ['--debug']
    command_str = ' '.join(command)

    log.debug('Executing command:\n\n%s\n', command_str)

    current_working_directory = os.getcwd()
    log.debug('Using current working directory:\n%s',
              current_working_directory)

    process_handle = subprocess.Popen(command_str,
                                      cwd=current_working_directory,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                      shell=True,
                                      env={'LD_LIBRARY_PATH': common.lib_paths})

    process_result = common.handle_process_result(process_handle)
    command_result, entire_stdout = process_result

    report['return_value_is_0'] = command_result
    report['gramtools_cpp_build'] = collections.OrderedDict([
        ('command', command_str),
        ('return_value_is_0', command_result),
        ('stdout', entire_stdout),
    ])
    return report


def _save_report(start_time,
                 report,
                 command_paths,
                 command_hash_paths,
                 report_file_path):
    end_time = str(time.time()).split('.')[0]
    _, report_dict = version.report()
    current_working_directory = os.getcwd()

    _report = collections.OrderedDict([
        ('start_time', start_time),
        ('end_time', end_time),
        ('total_runtime', int(end_time) - int(start_time)),
    ])
    _report.update(report)
    _report.update(collections.OrderedDict([
        ('current_working_directory', current_working_directory),
        ('paths', command_paths),
        ('path_hashes', command_hash_paths),
        ('version_report', report_dict),
    ]))

    with open(report_file_path, 'w') as fhandle:
        json.dump(_report, fhandle, indent=4)


## Combines multiple vcf files together using external python utility.
# Records where the REFs overlap are merged together and all possible haplotypes enumerated.
# New path to 'vcf' is path to the combined vcf
def _handle_multi_vcf(command_paths):
    vcf_files = command_paths['vcf']
    tmp_vcf_name = os.path.join(command_paths['gram_dir'], 'tmp.vcf')
    final_vcf_name = os.path.join(command_paths['gram_dir'], 'build.vcf')

    cluster = cluster_vcf_records.vcf_clusterer.VcfClusterer(vcf_files,
                                                             command_paths['original_reference'],
                                                             tmp_vcf_name, max_alleles_per_cluster=5000)
    cluster.run()

    ## Rewrite header so that it better reflects the clustered vcf contents.
    ## This is needed in order to correctly merge multiple vcf from multi-sample pipeline.
    ## The headers output by vcf_clusterer are barebones, because it may get differently specified vcf's; here, we assume they are uniform.
    ## TODO: uniformity will NOT be guaranteed when the initial vcf is not in the same format as that produced by `discover`
    vcf_template_name = vcf_files[0]

    # In multi-sample case, we do not want the initial build vcf to be used as template
    if os.path.basename(vcf_template_name) == os.path.basename(command_paths['perl_generated_vcf']):
        vcf_template_name = vcf_files[1]

    with open(vcf_template_name) as vcf_template_file:
        vcf_template = vcf.Reader(vcf_template_file)  # We will take the headers from the first provided vcf file.
        vcf_template.metadata['source'] = [
            "cluster_vcf_records module, version {}".format(cluster_vcf_records.__version__)]

        with open(final_vcf_name, "w") as build_out:
            # We will take the headers from the template, and the records from the clusterer output.
            vcf_writer = vcf.Writer(build_out, vcf_template)

            with open(tmp_vcf_name) as record_container:
                # And simply write out all records as is
                vcf_records = []
                record_reader = vcf.Reader(record_container)
                # We will ignore records which cannot be parsed by the vcf module.
                rec = "Not_none"
                while rec is not None:
                    rec = get_next_valid_record(record_reader)
                    if rec is not None:
                        vcf_records.append(rec)

            for record in vcf_records:
                vcf_writer.write_record(record)


    os.remove(tmp_vcf_name)
    command_paths['vcf'] = final_vcf_name
    return command_paths


def get_next_valid_record(vcf_reader):
    try:
        next_record = next(vcf_reader, None)
    except Exception:
        next_record = get_next_valid_record(vcf_reader)
    return next_record
