## @file
# The entry point for the `gramtools` software.
import logging
import argparse
import collections

from . import version
from .commands import build
from .commands import simulate
from .commands import quasimap
from .commands import infer
from .commands import discover


def _setup_logging(level):
    log = logging.getLogger('gramtools')
    handler = logging.StreamHandler()
    formatter = logging.Formatter(
        '%(asctime)s %(name)-12s %(levelname)-8s %(message)s')
    handler.setFormatter(formatter)
    log.addHandler(handler)
    log.setLevel(level)
    return log


root_parser = argparse.ArgumentParser(prog='gramtools')
commands = collections.OrderedDict([
    ('build', build),
    ('simulate', simulate),
    ('quasimap', quasimap),
    ('infer', infer),
    ('discover', discover),
])


def _parse_args():
    root_parser.add_argument('--version', help='', action='store_true')
    metavar = '{{{commands}}}'.format(commands=', '.join(commands.keys()))
    subparsers = root_parser.add_subparsers(title='subcommands',
                                            dest='subparser_name',
                                            metavar=metavar)

    common_parser = subparsers.add_parser('common', add_help=False)
    common_parser.add_argument('--debug', help='', action='store_true')

    for command in commands.values():
        command.parse_args(common_parser, subparsers)

    arguments = root_parser.parse_args()
    return arguments


def _get_log(args):
    if hasattr(args, 'debug') and args.debug:
        level = logging.DEBUG
    else:
        level = logging.INFO
    _setup_logging(level)
    log = logging.getLogger('gramtools')
    return log


def run():
    args = _parse_args()
    _get_log(args)

    if args.version:
        report_json, _ = version.report()
        print(report_json)
        return

    try:
        command = commands[args.subparser_name]
    except KeyError:
        root_parser.print_help()
    else:
        command.run(args)


if __name__ == '__main__':
    run()
