# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

USE_PYTHON3 = True
TEST_PATTERNS = [r'.+_test.py$']


def CheckChangeOnUpload(input_api, output_api):
    return _CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
    return _CommonChecks(input_api, output_api)


def _CommonChecks(input_api, output_api):
    results = []
    results += input_api.canned_checks.CheckPatchFormatted(input_api,
                                                           output_api,
                                                           check_js=True)
    try:
        import sys
        old_sys_path = sys.path[:]
        cwd = input_api.PresubmitLocalPath()

        sys.path += [input_api.os_path.join(cwd, '..', '..', 'tools')]
        import web_dev_style.presubmit_support
        results += web_dev_style.presubmit_support.CheckStyle(
            input_api, output_api)

        sys.path += [input_api.os_path.join(cwd, '..', 'chromeos')]
        import styles.presubmit_support
        results += styles.presubmit_support._CheckSemanticColors(
            input_api, output_api)

        # Run all unit tests under ui/file_manager/base folder.
        results += input_api.canned_checks.RunUnitTestsInDirectory(
            input_api,
            output_api,
            'base',
            files_to_check=TEST_PATTERNS,
            run_on_python2=False,
            skip_shebang_check=True)

        sys.path += [input_api.os_path.join(cwd)]
        import base.presubmit_support
        results += base.presubmit_support._CheckGM3Counterpart(
            input_api, output_api)
    finally:
        sys.path = old_sys_path
    return results
