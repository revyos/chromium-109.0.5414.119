# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# If this presubmit check fails or misbehaves, please complain to
# mnissler@chromium.org, bartfab@chromium.org or atwilson@chromium.org.

PRESUBMIT_VERSION = '2.0.0'
USE_PYTHON3 = True

import os
import sys
from xml.dom import minidom
from xml.parsers import expat

sys.path.append(os.path.abspath('.'))
from policy_templates import GetPolicyTemplates

_SRC_PATH = os.path.abspath('../../../')
sys.path.append(os.path.join(_SRC_PATH, 'third_party'))
import pyyaml


_CACHED_FILES = {}
_CACHED_POLICY_CHANGE_LIST = []

_TEST_CASES_DEPOT_PATH = os.path.join(
      'chrome', 'test', 'data', 'policy', 'policy_test_cases.json')
_PRESUBMIT_PATH = os.path.join(
      'components', 'policy', 'resources', 'PRESUBMIT.py')
_TEMPLATES_PATH = os.path.join(
      'components', 'policy', 'resources',
      'templates')
_MESSAGES_PATH = os.path.join(_TEMPLATES_PATH, 'messages.yaml')
_POLICIES_DEFINITIONS_PATH = os.path.join(_TEMPLATES_PATH, 'policy_definitions')
_POLICIES_YAML_PATH = os.path.join(_TEMPLATES_PATH, 'policies.yaml')
_HISTOGRAMS_PATH = os.path.join(
      'tools', 'metrics', 'histograms', 'enums.xml')
_DEVICE_POLICY_PROTO_PATH = os.path.join(
      'components', 'policy', 'proto', 'chrome_device_policy.proto')
_DEVICE_POLICY_PROTO_MAP_PATH = os.path.join(
      _TEMPLATES_PATH, 'device_policy_proto_map.yaml')
_LEGACY_DEVICE_POLICY_PROTO_MAP_PATH = os.path.join(
      _TEMPLATES_PATH, 'legacy_device_policy_proto_map.yaml')


def _SkipPresubmitChecks(input_api, files_watchlist):
  '''Returns True if no file or file under the directories specified was
     affected in this change.
     Args:
       input_api
       files_watchlist: List of files or directories
  '''
  for file in files_watchlist:
    if any(os.path.commonpath([file, f.LocalPath()]) == file for f in
           input_api.change.AffectedFiles()):
      return False

  return True


def _LoadYamlFile(root, path):
  str_path = str(path)
  if str_path not in _CACHED_FILES:
    with open(os.path.join(root, path), encoding='utf-8') as f:
      _CACHED_FILES[str_path] = pyyaml.safe_load(f)
  return _CACHED_FILES[str_path]


def _GetPolicyChangeList(input_api):
  '''Returns a list of policies modified inthe changelist with their old schema
     next to their new schemas.
     Args:
       input_api
      Returns:
        object with the following schema:
        { 'name': 'string', 'old_policy': dict, 'new_policy': dict }
        The policies are the values loaded from their yaml files.
  '''
  if _CACHED_POLICY_CHANGE_LIST:
    return _CACHED_POLICY_CHANGE_LIST
  policies_dir = input_api.os_path.join(input_api.change.RepositoryRoot(),
                                        _POLICIES_DEFINITIONS_PATH)
  template_affected_files = [f for f in input_api.change.AffectedFiles()
    if os.path.commonpath([policies_dir,
      f.AbsoluteLocalPath()]) ==  policies_dir]

  for affected_file in template_affected_files:
    path = affected_file.AbsoluteLocalPath()
    filename = os.path.basename(path)
    filename_no_extension = os.path.splitext(filename)[0]
    if (filename == '.group.details.yaml' or
        filename == 'policy_atomic_groups.yaml'):
      continue
    old_policy = None
    new_policy = None
    if affected_file.Action() in ['M', 'D']:
      try:
        old_policy = pyyaml.safe_load('\n'.join(affected_file.OldContents()))
      except:
        old_policy = None
    if affected_file.Action() != 'D':
      new_policy = pyyaml.safe_load('\n'.join(affected_file.NewContents()))
    _CACHED_POLICY_CHANGE_LIST.append({
      'policy': filename_no_extension,
      'old_policy': old_policy,
      'new_policy': new_policy})
  return _CACHED_POLICY_CHANGE_LIST


def _CheckPolicyTemplatesSyntax(input_api, output_api, legacy_policy_template):

  local_path = input_api.PresubmitLocalPath()
  template_dir = input_api.os_path.join(input_api.change.RepositoryRoot(),
                                        'components', 'policy', 'resources',
                                        'templates')
  old_sys_path = sys.path
  try:
    tools_path = input_api.os_path.normpath(
        input_api.os_path.join(local_path, input_api.os_path.pardir, 'tools'))
    sys.path = [tools_path] + sys.path
    # Optimization: only load this when it's needed.
    import syntax_check_policy_template_json
    device_policy_proto_path = input_api.os_path.join(
        local_path, '..', 'proto', 'chrome_device_policy.proto')
    args = ["--device_policy_proto_path=" + device_policy_proto_path]

    root = input_api.change.RepositoryRoot()

    # Get the current version from the VERSION file so that we can check
    # which policies are un-released and thus can be changed at will.
    current_version = None
    try:
      version_path = input_api.os_path.join(root, 'chrome', 'VERSION')
      with open(version_path, "rb") as f:
        current_version = int(f.readline().split(b"=")[1])
        print('Checking policies against current version: ' +
              current_version)
    except:
      pass

    # Check if there is a tag that allows us to bypass compatibility checks.
    # This can be used in situations where there is a bug in the validation
    # code or if a policy change needs to urgently be submitted.
    skip_compatibility_check = ('BYPASS_POLICY_COMPATIBILITY_CHECK'
                                 in input_api.change.tags)

    checker = syntax_check_policy_template_json.PolicyTemplateChecker()
    errors, warnings = checker.Run(args, legacy_policy_template,
                                   _GetPolicyChangeList(input_api),
                                   current_version,
                                   skip_compatibility_check)

    # PRESUBMIT won't print warning if there is any error. Append warnings to
    # error for policy_templates.json so that they can always be printed
    # together.
    if errors:
      error_msgs = "\n".join(errors+warnings)
      return [output_api.PresubmitError('Syntax error(s) in file:',
                                        [template_dir],
                                        error_msgs)]
    elif warnings:
      warning_msgs = "\n".join(warnings)
      return [output_api.PresubmitPromptWarning('Syntax warning(s) in file:',
                                                [template_dir],
                                                warning_msgs)]
  finally:
    sys.path = old_sys_path
  return []


def CheckPolicyTestCases(input_api, output_api):
  '''Verifies that the all defined policies have a test case.
  This is ran when policy_test_cases.json, policies.yaml or this PRESUBMIT.py
  file are modified.
  '''
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_TEST_CASES_DEPOT_PATH, _POLICIES_YAML_PATH, _PRESUBMIT_PATH]):
    return results

  # Read list of policies in chrome/test/data/policy/policy_test_cases.json.
  root = input_api.change.RepositoryRoot()
  with open(os.path.join(root, _TEST_CASES_DEPOT_PATH), encoding='utf-8') as f:
    test_names = input_api.json.load(f).keys()
  tested_policies = frozenset(name.partition('.')[0]
                              for name in test_names
                              if name[:2] != '--')
  policies_yaml = _LoadYamlFile(root, _POLICIES_YAML_PATH)
  policies = policies_yaml['policies']
  policy_names = frozenset(name for name in policies.values() if name)

  # Finally check if any policies are missing.
  missing = policy_names - tested_policies
  extra = tested_policies - policy_names
  error_missing = ("Policy '%s' was added to policy_templates.json but not "
                   "to src/chrome/test/data/policy/policy_test_cases.json. "
                   "Please update both files.")
  error_extra = ("Policy '%s' is tested by "
                 "src/chrome/test/data/policy/policy_test_cases.json but is not"
                 " defined in policy_templates.json. Please update both files.")
  results = []
  for policy in missing:
    results.append(output_api.PresubmitError(error_missing % policy))
  for policy in extra:
    results.append(output_api.PresubmitError(error_extra % policy))

  results.extend(
      input_api.canned_checks.CheckChangeHasNoTabs(
          input_api,
          output_api,
          source_file_filter=lambda x: x.LocalPath() == _TEST_CASES_DEPOT_PATH))

  return results


def CheckPolicyHistograms(input_api, output_api):
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_HISTOGRAMS_PATH, _POLICIES_YAML_PATH, _PRESUBMIT_PATH]):
    return results

  root = input_api.change.RepositoryRoot()

  with open(os.path.join(root, _HISTOGRAMS_PATH), encoding='utf-8') as f:
    tree = minidom.parseString(f.read())
  enums = (tree.getElementsByTagName('histogram-configuration')[0]
               .getElementsByTagName('enums')[0]
               .getElementsByTagName('enum'))
  policy_enum = [e for e in enums
                 if e.getAttribute('name') == 'EnterprisePolicies'][0]
  policy_enum_ids = frozenset(int(e.getAttribute('value'))
                              for e in policy_enum.getElementsByTagName('int'))
  policies_yaml = _LoadYamlFile(root, _POLICIES_YAML_PATH)
  policies = policies_yaml['policies']
  policy_ids = frozenset([id for id, name in policies.items() if name])

  missing_ids = policy_ids - policy_enum_ids
  extra_ids = policy_enum_ids - policy_ids

  error_missing = ("Policy '%s' (id %d) was added to "
                   "policy_templates.json but not to "
                   "src/tools/metrics/histograms/enums.xml. Please update "
                   "both files. To regenerate the policy part of enums.xml, "
                   "run:\n"
                   "python tools/metrics/histograms/update_policies.py")
  error_extra = ("Policy id %d was found in "
                 "src/tools/metrics/histograms/enums.xml, but no policy with "
                 "this id exists in policy_templates.json. To regenerate the "
                 "policy part of enums.xml, run:\n"
                 "python tools/metrics/histograms/update_policies.py")
  results = []
  for policy_id in missing_ids:
    results.append(
        output_api.PresubmitError(error_missing %
                                  (policies[policy_id], policy_id)))
  for policy_id in extra_ids:
    results.append(output_api.PresubmitError(error_extra % policy_id))
  return results


def CheckPolicyAtomicGroupsHistograms(input_api, output_api):
  '''Verifies that the all policy atomic groups have a histogram entry.
  This is ran when policies.yaml, tools/metrics/histograms/enums.xml or this
  PRESUBMIT.py file are modified.
  '''
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_HISTOGRAMS_PATH, _POLICIES_YAML_PATH, _PRESUBMIT_PATH]):
    return results

  root = input_api.change.RepositoryRoot()

  with open(os.path.join(root, _HISTOGRAMS_PATH), encoding='utf-8') as f:
    tree = minidom.parseString(f.read())
  enums = (tree.getElementsByTagName('histogram-configuration')[0]
               .getElementsByTagName('enums')[0]
               .getElementsByTagName('enum'))
  atomic_group_enum = [e for e in enums
                 if e.getAttribute('name') == 'PolicyAtomicGroups'][0]
  atomic_group_enum_ids = frozenset(int(e.getAttribute('value'))
                              for e in atomic_group_enum
                                .getElementsByTagName('int'))
  policies_yaml = _LoadYamlFile(root, _POLICIES_YAML_PATH)
  atomic_groups = policies_yaml['atomic_groups']
  atomic_group_ids = frozenset(
    [id for id, name in atomic_groups.items() if name])

  missing_ids = atomic_group_ids - atomic_group_enum_ids
  extra_ids = atomic_group_enum_ids - atomic_group_ids

  error_missing = ("Policy atomic group '%s' (id %d) was added to "
                   "policy_templates.json but not to "
                   "src/tools/metrics/histograms/enums.xml. Please update "
                   "both files. To regenerate the policy part of enums.xml, "
                   "run:\n"
                   "python tools/metrics/histograms/update_policies.py")
  error_extra = ("Policy atomic group id %d was found in "
                 "src/tools/metrics/histograms/enums.xml, but no policy with "
                 "this id exists in policy_templates.json. To regenerate the "
                 "policy part of enums.xml, run:\n"
                 "python tools/metrics/histograms/update_policies.py")
  results = []
  for atomic_group_id in missing_ids:
    results.append(output_api.PresubmitError(error_missing %
                              (atomic_groups[atomic_group_id],
                              atomic_group_id)))
  for atomic_group_id in extra_ids:
    results.append(output_api.PresubmitError(error_extra % atomic_group_id))
  return results


# TODO(crbug/1171839): Remove this from syntax_check_policy_template_json.py
# as this check is now duplicated.
def CheckMessages(input_api, output_api):
  '''Verifies that the all the messages from messages.yaml have the following
  format: {[key: string]: {text: string, desc: string}}.
  This is ran when messages.yaml or this PRESUBMIT.py
  file are modified.
  '''
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_MESSAGES_PATH, _PRESUBMIT_PATH]):
    return results

  root = input_api.change.RepositoryRoot()
  messages = _LoadYamlFile(root, _MESSAGES_PATH)

  for message in messages:
    # |key| must be a string, |value| a dict.
    if not isinstance(message, str):
      results.append(
        output_api.PresubmitError(
          f'Each message key must be a string, invalid key {message}'))
      continue

    if not isinstance(messages[message], dict):
      results.append(
        output_api.PresubmitError(
          f'Each message must be a dictionary, invalid message {message}'))
      continue

    if ('desc' not in messages[message] or
        not isinstance(messages[message]['desc'], str)):
      results.append(
        output_api.PresubmitError(
          f"'desc' string key missing in message {message}"))

    if ('text' not in messages[message] or
        not isinstance(messages[message]['text'], str)):
      results.append(
        output_api.PresubmitError(
          f"'text' string key missing in message {message}"))

    # There should not be any unknown keys in |value|.
    for vkey in messages[message]:
      if vkey not in ('desc', 'text'):
        results.append(output_api.PresubmitError(
          f'In message {message}: Unknown key: {vkey}'))
  return results


def CheckMissingPlaceholders(input_api, output_api):
  '''Verifies that the all the messages from messages.yaml, caption and
  descriptions from files under templates/policy_definitions do not have
  malformed placeholders.
  This is ran when messages.yaml, files under templates/policy_definitions or
  this PRESUBMIT.py file are modified.
  '''
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_MESSAGES_PATH, _POLICIES_DEFINITIONS_PATH, _PRESUBMIT_PATH]):
    return results

  root = input_api.change.RepositoryRoot()
  new_policies = [change['new_policy']
    for change in _GetPolicyChangeList(input_api)]
  messages = _LoadYamlFile(root, _MESSAGES_PATH)
  items = new_policies + list(messages.values())
  for item in items:
    for key in ['desc', 'text']:
      if item is None:
        continue
      if not key in item:
        continue
      try:
        node = minidom.parseString('<msg>%s</msg>' % item[key]).childNodes[0]
      except expat.ExpatError as e:
        error = (
            'Error when checking for missing placeholders: %s in:\n'
            '!<Policy Start>!\n%s\n<Policy End>!' %
            (e, item[key]))
        results.append(output_api.PresubmitError(error))
        continue

      for child in node.childNodes:
        if child.nodeType == minidom.Node.TEXT_NODE and '$' in child.data:
          warning = ("Character '$' found outside of a placeholder in '%s'. "
                     "Should it be in a placeholder ?") % item[key]
          results.append(output_api.PresubmitPromptWarning(warning))
  return results

# TODO(crbug/1171839): Remove this from syntax_check_policy_template_json.py
# as this check is now duplicated.
def CheckDevicePolicyProtos(input_api, output_api):
  results = []
  if _SkipPresubmitChecks(
      input_api,
      [_DEVICE_POLICY_PROTO_PATH, _DEVICE_POLICY_PROTO_MAP_PATH,
       _LEGACY_DEVICE_POLICY_PROTO_MAP_PATH, _PRESUBMIT_PATH]):
    return results
  root = input_api.change.RepositoryRoot()

  proto_map = _LoadYamlFile(root, _DEVICE_POLICY_PROTO_MAP_PATH)
  legacy_proto_map = _LoadYamlFile(root, _LEGACY_DEVICE_POLICY_PROTO_MAP_PATH)
  with open(os.path.join(root, _DEVICE_POLICY_PROTO_PATH),
            'r', encoding='utf-8') as file:
    protos = file.read()
  results = []
  # Check that proto_map does not have duplicate values.
  proto_paths = set()
  for proto_path in proto_map.values():
    if proto_path in proto_paths:
      results.append(output_api.PresubmitError(
          f'Duplicate proto path {proto_path} in '
          f'{os.path.basename(_DEVICE_POLICY_PROTO_MAP_PATH)}. '
          'Did you set the right path for your device policy?'))
    proto_paths.add(proto_path)

  # Check that legacy_proto_map does not have duplicate values.
  for proto_path_list in legacy_proto_map.values():
    for proto_path in proto_path_list:
      if not proto_path:
        continue
      if proto_path in proto_paths:
        results.append(output_api.PresubmitError(
          f'Duplicate proto path {proto_path} in '
          'legacy_device_policy_proto_map.yaml.'
          'Did you set the right path for your device policy?'))
      proto_paths.add(proto_path)

  for policy, proto_path in proto_map.items():
    fields = proto_path.split(".")
    for field in fields:
      if field not in protos:
        results.append(output_api.PresubmitError(
         f"Policy '{policy}': Expected field '{field}' not found in "
         "chrome_device_policy.proto."))
  return results


def _CommonChecks(input_api, output_api):
  results = []
  root = input_api.change.RepositoryRoot()
  template_dir = input_api.os_path.join(input_api.change.RepositoryRoot(),
                                        'components', 'policy', 'resources',
                                        'templates')
  device_policy_proto_path = input_api.os_path.join(
      root, 'components', 'policy', 'proto', 'chrome_device_policy.proto')
  syntax_check_path = input_api.os_path.join(
      root, 'components', 'policy', 'tools',
      'syntax_check_policy_template_json.py')
  affected_files = input_api.change.AffectedFiles()

  template_changed = any(
    os.path.commonpath([template_dir, f.AbsoluteLocalPath()]) == template_dir
    for f in affected_files)
  device_policy_proto_changed = any(
    f.AbsoluteLocalPath() == device_policy_proto_path for f in affected_files)
  syntax_check_changed = any(f.AbsoluteLocalPath() == syntax_check_path
    for f in affected_files)

  if (template_changed or device_policy_proto_changed or syntax_check_changed):
    try:
      template_data = GetPolicyTemplates()
    except:
      results.append(
        output_api.PresubmitError('Unable to load the policy templates.'))
      return results

    # chrome_device_policy.proto is hand crafted. When it is changed, we need
    # to check if it still corresponds to policy_templates.json.
    if template_changed or device_policy_proto_changed or syntax_check_changed:
      results.extend(
        _CheckPolicyTemplatesSyntax(input_api, output_api, template_data))

  return results


def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)
