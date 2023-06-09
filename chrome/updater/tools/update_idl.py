# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A tool for updating IDL COM headers/TLB after updating IDL template.

This tool must be run from a Windows machine at the source root directory.

Example:
    python3 chrome/updater/tools/update_idl.py
"""

import os
import platform
import subprocess

_IDL_GN_TARGET = 'chrome/updater/app/server/win:updater_idl_idl_idl_action'


class IDLUpdateError(Exception):
    """Module exception class."""


class IDLUpdater:
    """A class to update IDL COM headers/TLB files based on config."""

    def __init__(self, target_cpu: str, is_chrome_branded: bool):
        self.target_cpu = target_cpu
        self.is_chrome_branded = str(is_chrome_branded).lower()
        self.output_dir = r'out\idl_update'

    def update(self) -> None:
        print('Updating IDL files for', self.target_cpu,
              'CPU, chrome_branded:', self.is_chrome_branded, '...')
        self._make_output_dir()
        self._gen_gn_args()
        self._autoninja_and_update()

    def _make_output_dir(self) -> None:
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)

    def _gen_gn_args(self) -> None:
        # `subprocess` may interpret the complex config values passed via
        # `--args` differently than intended. Generate the default gn.args first
        # and then update it by writing directly.

        # gen args with default values.
        gn_args_path = os.path.join(self.output_dir, 'args.gn')
        print('Generating', gn_args_path, 'with default values.')
        subprocess.run(['gn.bat', 'gen', self.output_dir], check=True)

        # Manually update args.gn
        print('Write', gn_args_path, 'with desired config.')
        with open(gn_args_path, 'wt') as f:
            f.write((f'target_cpu="{self.target_cpu}"\n'
                     f'use_goma=true\n'
                     f'is_chrome_branded={self.is_chrome_branded}\n'
                     f'is_debug=true\n'
                     f'enable_nacl=false\n'
                     f'blink_symbol_level=0\n'
                     f'v8_symbol_level=0\n').format())
        print('Done.')

    def _autoninja_and_update(self) -> None:
        print('Check if update is needed by building the target...')
        proc = subprocess.run(
            ['autoninja.bat', '-C', self.output_dir, _IDL_GN_TARGET],
            capture_output=True,
            check=False)
        if proc.returncode == 0:
            print('No update is needed.\n')
            return

        cmd = self._extract_update_command(proc.stdout.decode('utf-8'))
        print('Updating IDL COM headers/TLB by [', cmd, ']...')
        subprocess.run(cmd, shell=True, capture_output=True, check=True)
        print('Done.\n')

    def _extract_update_command(self, stdout: str) -> str:
        lines = stdout.splitlines()
        if (len(lines) < 3
                or 'ninja: build stopped: subcommand failed.' not in lines[-1]
                or 'copy /y' not in lines[-2]
                or 'To rebaseline:' not in lines[-3]):
            print('-' * 80)
            print('STDOUT:')
            print(stdout)
            print('-' * 80)

            raise IDLUpdateError(
                'Unexpected autoninja error, or update this tool if the output '
                'format is changed.')

        return lines[-2].strip()


def check_running_environment() -> None:
    if 'Windows' not in platform.system():
        raise IDLUpdateError('This tool must run from Windows platform.')

    proc = subprocess.run(['git.bat', 'rev-parse', '--show-toplevel'],
                          capture_output=True,
                          check=True)

    if proc.returncode != 0:
        raise IDLUpdateError(
            'Failed to run git for finding source root directory.')

    source_root = os.path.abspath(proc.stdout.decode('utf-8').strip()).lower()
    if not os.path.exists(source_root):
        raise IDLUpdateError('Unexpected failure to get source root directory')

    cwd = os.getcwd().lower()
    if cwd != source_root:
        raise IDLUpdateError(f'This tool must run from project root folder. '
                             f'CWD: [{cwd}] vs ACTUAL:[{source_root}]')


def main():
    check_running_environment()

    for target_cpu in ['arm64', 'x64', 'x86']:
        for is_chrome_branded in [True, False]:
            IDLUpdater(target_cpu, is_chrome_branded).update()


if __name__ == '__main__':
    main()
