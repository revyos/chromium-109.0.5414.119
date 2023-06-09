# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


USE_PYTHON3 = True

def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def _CheckForTranslations(input_api, output_api):
  shared_keywords = ['i18n(']
  html_keywords = shared_keywords + ['$118n{']
  js_keywords = shared_keywords + ['I18nBehavior', 'loadTimeData.get']

  errors = []

  for f in input_api.AffectedFiles():
    local_path = f.LocalPath()
    # Allow translation in i18n_behavior.js.
    if local_path.endswith('i18n_behavior.js'):
      continue
    # Allow translation in the cr_components directory.
    if 'cr_components' in local_path:
      continue
    keywords = None
    if local_path.endswith('.js'):
      keywords = js_keywords
    elif local_path.endswith('.html'):
      keywords = html_keywords

    if not keywords:
      continue

    for lnum, line in f.ChangedContents():
      if any(line for keyword in keywords if keyword in line):
        errors.append("%s:%d\n%s" % (f.LocalPath(), lnum, line))

  if not errors:
    return []

  return [output_api.PresubmitError("\n".join(errors) + """

Don't embed translations directly in shared UI code. Instead, inject your
translation from the place using the shared code. For an example: see
<cr-dialog>#closeText (http://bit.ly/2eLEsqh).""")]


def _CheckSvgsOptimized(input_api, output_api):
  results = []
  try:
    import sys
    old_sys_path = sys.path[:]
    cwd = input_api.PresubmitLocalPath()
    sys.path += [input_api.os_path.join(cwd, '..', '..', '..', 'tools')]
    from resources import svgo_presubmit
    results += svgo_presubmit.CheckOptimized(input_api, output_api)
  finally:
    sys.path = old_sys_path
  return results


def _CheckWebDevStyle(input_api, output_api):
  results = []
  try:
    import sys
    old_sys_path = sys.path[:]
    cwd = input_api.PresubmitLocalPath()
    sys.path += [input_api.os_path.join(cwd, '..', '..', '..', 'tools')]
    from web_dev_style import presubmit_support
    results += presubmit_support.CheckStyle(input_api, output_api)
  finally:
    sys.path = old_sys_path
  return results

def _CheckNoDisallowedJS(input_api, output_api):
  # Ignore legacy files from the js/ subfolder along with tools/.
  EXCLUDE_PATH_PREFIXES = [
    'ui/webui/resources/js/assert.js',
    'ui/webui/resources/js/dom_automation_controller.js',
    'ui/webui/resources/js/cr',
    'ui/webui/resources/js/ios/',
    'ui/webui/resources/js/load_time_data.m.js',
    'ui/webui/resources/js/load_time_data_deprecated.js',
    'ui/webui/resources/js/promise_resolver.js',
    'ui/webui/resources/js/util.js',
    'ui/webui/resources/js/util_deprecated.js',
    'ui/webui/resources/tools/',
  ]

  normalized_excluded_prefixes = []
  for path in EXCLUDE_PATH_PREFIXES:
    normalized_excluded_prefixes.append(input_api.os_path.normpath(path))

  # Also exempt any externs or eslint files, which must be in JS.
  EXCLUDE_PATH_SUFFIXES = [
    '_externs.js',
    '.eslintrc.js',
  ]

  def allow_js(f):
    path = f.LocalPath()
    for prefix in normalized_excluded_prefixes:
      if path.startswith(prefix):
        return True
    for suffix in EXCLUDE_PATH_SUFFIXES:
      if path.endswith(suffix):
        return True
    return False

  from web_dev_style import presubmit_support
  return presubmit_support.DisallowNewJsFiles(input_api, output_api,
                                              lambda f: not allow_js(f))

def _CheckJsModulizer(input_api, output_api):
  affected = input_api.AffectedFiles()
  affected_files = [input_api.os_path.basename(f.LocalPath()) for f in affected]

  results = []
  if 'js_modulizer.py' in affected_files:
    presubmit_path = input_api.PresubmitLocalPath()
    sources = [input_api.os_path.join('tools', 'js_modulizer_test.py')]
    tests = [input_api.os_path.join(presubmit_path, s) for s in sources]
    results += input_api.canned_checks.RunUnitTests(
        input_api, output_api, tests, run_on_python2=False)
  return results


def _CheckGenerateGrd(input_api, output_api):
  affected = input_api.AffectedFiles()
  affected_files = [input_api.os_path.basename(f.LocalPath()) for f in affected]

  results = []
  if 'generate_grd.py' in affected_files:
    presubmit_path = input_api.PresubmitLocalPath()
    sources = [input_api.os_path.join('tools', 'generate_grd_test.py')]
    tests = [input_api.os_path.join(presubmit_path, s) for s in sources]
    results += input_api.canned_checks.RunUnitTests(
        input_api, output_api, tests, skip_shebang_check=True,
        run_on_python2=False)
  return results


def _CommonChecks(input_api, output_api):
  results = []
  results += _CheckForTranslations(input_api, output_api)
  results += _CheckSvgsOptimized(input_api, output_api)
  results += _CheckWebDevStyle(input_api, output_api)
  results += _CheckNoDisallowedJS(input_api, output_api)
  results += _CheckJsModulizer(input_api, output_api)
  results += _CheckGenerateGrd(input_api, output_api)
  results += input_api.canned_checks.CheckPatchFormatted(input_api, output_api,
                                                         check_js=True)
  return results
