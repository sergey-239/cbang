from SCons.Script import *
import inspect


def run_tests(env):
    import shlex
    import subprocess
    import sys

    cmd = shlex.split(env.get('TEST_COMMAND'))
    print('Executing:', cmd)
    sys.exit(subprocess.call(cmd))


def generate(env):
    import os
    import distutils.spawn

    python = distutils.spawn.find_executable('python3')
    if not python: python = distutils.spawn.find_executable('python')
    if not python: python = distutils.spawn.find_executable('python2')
    if not python: python = 'python'
    if env['PLATFORM'] == 'win32': python = python.replace('\\', '\\\\')

    path = inspect.getfile(inspect.currentframe())
    home = os.path.dirname(os.path.abspath(path)) + '/../..'

    cmd = python + ' ' + home + '/tests/testHarness -C tests --diff-failed ' \
        '--view-failed --view-unfiltered --save-failed --build'

    if 'DOCKBOT_MASTER_PORT' in os.environ: cmd += ' --no-color'

    env.CBAddVariables(('TEST_COMMAND', '`test` target command line', cmd))

    if 'test' in COMMAND_LINE_TARGETS: env.CBAddConfigureCB(run_tests)


def exists(): return 1
