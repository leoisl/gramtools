## @file
# Executes `gram quasimap` backend for variant aware backward searching.
import os
import time
import json
import logging
import subprocess
import collections

from .. import version
from .. import common
from .. import paths


log = logging.getLogger('gramtools')


def parse_args(common_parser, subparsers):
    parser = subparsers.add_parser('quasimap',
                                   parents=[common_parser])
    parser.add_argument('--gram-dir','--gram-directory',
                        help='Directory containing outputs from gramtools `build`',
                        dest='gram_dir',
                        type=str,
                        required=True)

    parser.add_argument('--run-dir','--run-directory',
                        help='Common directory for gramtools running commands. Will contain quasimap outputs.'
                             'These are JSON formatted files describing read coverage over variant sites in the prg.',
                        type=str,
                        dest='run_dir',
                        required=True)

    parser.add_argument('--reads',
                        help='One or more read files.\n'
                             'Valid formats: fastq, sam/bam/cram, fasta, txt; compressed or uncompressed; fuzzy extensions (eg fq, fsq for fastq).\n'
                             'Read files can be given after one or several \'--reads\' argument:'
                             'Eg \'--reads rf_1.fq rf_2.fq.gz --reads rf_3.bam \'',
                        nargs='+',
                        action="append",
                        type=str,
                        required=True)

    parser.add_argument('--max-threads',
                        help='Read quasimapping to the prg can be multi-threaded.'
                             'By default, uses just one thread.',
                        type=int,
                        default=1,
                        required=False)

    parser.add_argument('--seed',
                        help='Use this for fixing seed. Fixing seed will produce consistent coverage output across different runs.'
                             'By default, seed is randomly generated.',
                        type=int,
                        default=0,
                        required=False)

    parser.add_argument('--output-directory',
                        help='[Deprecated: use --run-dir instead].\n'
                             'Directory where outputs of quasimap will be stored.'
                             'Defaults to \'quasimap_outputs\' inside \'gram-dir\'.',
                        type=str,
                        required=False)


def _execute_command(quasimap_paths, report, args):
    if report.get('return_value_is_0') is False:
        report['gramtools_cpp_quasimap'] = {
            'return_value_is_0': False
        }
        return report

    command = [
        common.gramtools_exec_fpath,
        'quasimap',
        '--gram', quasimap_paths['gram_dir'],
        '--reads', ' '.join(quasimap_paths['reads_files']),
        '--kmer-size', str(args.kmer_size),
        '--run-directory', quasimap_paths['quasimap_dir'],
        '--max-threads', str(args.max_threads),
        '--seed', str(args.seed),
    ]

    command_str = ' '.join(command)
    log.debug('Executing command:\n\n%s\n', command_str)

    current_working_directory = os.getcwd()
    process_handle = subprocess.Popen(command_str,
                                      cwd=current_working_directory,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                      shell=True,
                                      env={'LD_LIBRARY_PATH': common.lib_paths})

    command_result, entire_stdout = common.handle_process_result(process_handle)
    log.info('Output run directory:\n%s', quasimap_paths['quasimap_dir'])

    report['return_value_is_0'] = command_result
    report['gramtools_cpp_quasimap'] = collections.OrderedDict([
        ('command', command_str),
        ('return_value_is_0', command_result),
        ('stdout', entire_stdout),
    ])
    return report


def _save_report(start_time,
                 reports,
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
    _report.update(reports)
    _report.update(collections.OrderedDict([
        ('current_working_directory', current_working_directory),
        ('paths', command_paths),
        ('path_hashes', command_hash_paths),
        ('version_report', report_dict),
    ]))

    with open(report_file_path, 'w') as fhandle:
        json.dump(_report, fhandle, indent=4)


def _load_build_report(project_paths):
    try:
        with open(project_paths['build_report']) as fhandle:
            return json.load(fhandle)
    except FileNotFoundError:
        log.error("Build report not found: %s. Try re-running gramtools `build`?", project_paths['build_report'])
        exit(1)


def _check_build_success(build_report):
    if not build_report['return_value_is_0']:
        log.error("Build was not completed successfully (see: build report)")
        exit(1)


def run(args):
    log.info('Start process: quasimap')

    if args.output_directory is not None:
        log.warning("DEPRECATED Option: '--output-directory'. Please use '--run-dir' in the future instead.")
        args.run_dir = args.output_directory

    start_time = str(time.time()).split('.')[0]
    _paths = paths.generate_quasimap_paths(args)

    build_report = _load_build_report(_paths)
    _check_build_success(build_report)

    kmer_size = build_report['kmer_size']
    setattr(args, 'kmer_size', kmer_size)

    report = collections.OrderedDict()
    report = _execute_command(_paths, report, args)

    ## Get the read statistics; report if most variant sites have no coverage.
    with open(_paths['read_stats']) as f:
        read_stats = json.load(f)

    num_sites_noCov, num_sites_total = read_stats["Read_depth"]["num_sites_noCov"], read_stats["Read_depth"]["num_sites_total"]
    if num_sites_noCov / num_sites_total > 0.5:
        log.warning("More than 50% of all variant sites have no coverage ({} out of {})."
                    "Possible reasons include: reads not quality-trimmed; low sequencing depth."\
                    .format(num_sites_noCov, num_sites_total))


    log.debug('Computing sha256 hash of project paths')
    command_hash_paths = common.hash_command_paths(_paths)

    log.debug('Saving command report:\n%s', _paths['report'])
    _save_report(start_time,
                 report,
                 _paths,
                 command_hash_paths,
                 _paths['report'])
    log.info('End process: quasimap')

    if report.get('return_value_is_0') is False:
        exit(1)
