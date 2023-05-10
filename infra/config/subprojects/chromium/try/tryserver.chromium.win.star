# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the tryserver.chromium.win builder group."""

load("//lib/branches.star", "branches")
load("//lib/builder_config.star", "builder_config")
load("//lib/builders.star", "goma", "os", "reclient")
load("//lib/try.star", "try_")
load("//lib/consoles.star", "consoles")

try_.defaults.set(
    builder_group = "tryserver.chromium.win",
    builderless = True,
    cores = 8,
    orchestrator_cores = 2,
    compilator_cores = 32,
    executable = try_.DEFAULT_EXECUTABLE,
    execution_timeout = try_.DEFAULT_EXECUTION_TIMEOUT,
    goma_backend = goma.backend.RBE_PROD,
    compilator_goma_jobs = goma.jobs.J300,
    os = os.WINDOWS_DEFAULT,
    pool = try_.DEFAULT_POOL,
    service_account = try_.DEFAULT_SERVICE_ACCOUNT,

    # TODO(crbug.com/1362440): remove this.
    omit_python2 = False,
)

consoles.list_view(
    name = "tryserver.chromium.win",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
)

try_.builder(
    name = "win-annotator-rel",
)

try_.builder(
    name = "win-asan",
    mirrors = [
        "ci/win-asan",
    ],
    goma_jobs = goma.jobs.J150,
    execution_timeout = 6 * time.hour,
)

try_.builder(
    name = "win-celab-try-rel",
    executable = "recipe:celab",
    properties = {
        "exclude": "chrome_only",
        "pool_name": "celab-chromium-try",
        "pool_size": 20,
        "tests": "*",
    },
)

try_.builder(
    name = "win-libfuzzer-asan-rel",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    builderless = False,
    executable = "recipe:chromium_libfuzzer_trybot",
    main_list_view = "try",
    os = os.WINDOWS_ANY,
    tryjob = try_.job(),
)

try_.orchestrator_builder(
    name = "win-rel",
    check_for_flakiness = True,
    compilator = "win-rel-compilator",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win10 Tests x64",
        "ci/GPU Win x64 Builder",
        "ci/Win10 x64 Release (NVIDIA)",
    ],
    try_settings = builder_config.try_settings(
        rts_config = builder_config.rts_config(
            condition = builder_config.rts_condition.QUICK_RUN_ONLY,
        ),
    ),
    use_clang_coverage = True,
    coverage_test_types = ["unit", "overall"],
    main_list_view = "try",
    # TODO(crbug.com/1381274): Make this a CQ blocker.
    #tryjob = try_.job(),
    experiments = {
        "chromium_rts.inverted_rts": 100,
    },
    # TODO (crbug.com/1372179): Use orchestrator pool once overloaded test pools
    # are addressed
    #use_orchestrator_pool = True,
)

try_.compilator_builder(
    name = "win-rel-compilator",
    check_for_flakiness = True,
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    main_list_view = "try",
    # TODO (crbug.com/1245171): Revert when root issue is fixed
    grace_period = 4 * time.minute,
)

try_.builder(
    name = "win_archive",
    mirrors = [
        "ci/win32-archive-rel",
    ],
)

try_.builder(
    name = "win_chromium_compile_dbg_ng",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    mirrors = [
        "ci/Win Builder (dbg)",
    ],
    try_settings = builder_config.try_settings(
        include_all_triggered_testers = True,
        is_compile_only = True,
    ),
    goma_jobs = goma.jobs.J150,
    main_list_view = "try",
    tryjob = try_.job(
        # TODO(crbug.com/1335555) Remove once cancelling doesn't wipe
        # out builder cache
        cancel_stale = False,
    ),
    builderless = False,
    cores = 16,
    ssd = True,
)

try_.builder(
    name = "win_chromium_compile_rel_ng",
    mirrors = [
        "ci/Win Builder",
    ],
    try_settings = builder_config.try_settings(
        include_all_triggered_testers = True,
        is_compile_only = True,
    ),
)

try_.builder(
    name = "win_chromium_x64_rel_ng",
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win 7 Tests x64 (1)",
    ],
)

try_.builder(
    name = "win_upload_clang",
    builderless = False,
    cores = 32,
    executable = "recipe:chromium_upload_clang",
    goma_backend = None,
    os = os.WINDOWS_ANY,
    execution_timeout = 6 * time.hour,
)

try_.builder(
    name = "win_x64_archive",
    mirrors = [
        "ci/win-archive-rel",
    ],
)

try_.builder(
    name = "win10_chromium_x64_dbg_ng",
    mirrors = [
        "ci/Win x64 Builder (dbg)",
        "ci/Win10 Tests x64 (dbg)",
    ],
    os = os.WINDOWS_10,
)

try_.builder(
    name = "win10-wpt-content-shell-fyi-rel",
    mirrors = [
        "ci/win10-wpt-content-shell-fyi-rel",
    ],
    os = os.WINDOWS_10,
)

try_.builder(
    name = "win11-x64-fyi-rel",
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win11 Tests x64",
    ],
    builderless = True,
    use_clang_coverage = True,
    coverage_test_types = ["unit", "overall"],
    os = os.WINDOWS_10,
)

try_.builder(
    name = "win10_chromium_inverse_fieldtrials_x64_fyi_rel_ng",
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win10 Tests x64",
        "ci/GPU Win x64 Builder",
        "ci/Win10 x64 Release (NVIDIA)",
    ],
    os = os.WINDOWS_10,
)

# TODO(crbug.com/1381274): Remove this after it's been replaced with win-rel.
try_.orchestrator_builder(
    name = "win10_chromium_x64_rel_ng",
    check_for_flakiness = True,
    compilator = "win10_chromium_x64_rel_ng-compilator",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win10 Tests x64",
        "ci/GPU Win x64 Builder",
        "ci/Win10 x64 Release (NVIDIA)",
    ],
    try_settings = builder_config.try_settings(
        rts_config = builder_config.rts_config(
            condition = builder_config.rts_condition.QUICK_RUN_ONLY,
        ),
    ),
    use_clang_coverage = True,
    coverage_test_types = ["unit", "overall"],
    main_list_view = "try",
    tryjob = try_.job(),
    experiments = {
        "chromium_rts.inverted_rts": 100,
    },
    # TODO (crbug.com/1372179): Use orchestrator pool once overloaded test pools
    # are addressed
    #use_orchestrator_pool = True,
)

try_.orchestrator_builder(
    name = "win10_chromium_x64_rel_ng-inverse-fyi",
    check_for_flakiness = True,
    compilator = "win10_chromium_x64_rel_ng-compilator",
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win10 Tests x64",
        "ci/GPU Win x64 Builder",
        "ci/Win10 x64 Release (NVIDIA)",
    ],
    try_settings = builder_config.try_settings(
        rts_config = builder_config.rts_config(
            condition = builder_config.rts_condition.QUICK_RUN_ONLY,
        ),
    ),
    use_clang_coverage = True,
    coverage_test_types = ["unit", "overall"],
    main_list_view = "try",
    experiments = {
        "chromium_rts.inverted_rts": 100,
        "chromium_rts.inverted_rts_bail_early": 100,
    },
    use_orchestrator_pool = True,
)

try_.compilator_builder(
    name = "win10_chromium_x64_rel_ng-compilator",
    check_for_flakiness = True,
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    main_list_view = "try",
    # TODO (crbug.com/1245171): Revert when root issue is fixed
    grace_period = 4 * time.minute,
)

try_.orchestrator_builder(
    name = "win10_chromium_x64_rel_ng-reclient",
    builderless = True,
    check_for_flakiness = True,
    compilator = "win10_chromium_x64_rel_ng-reclient-compilator",
    mirrors = [
        "ci/Win x64 Builder",
        "ci/Win10 Tests x64",
        "ci/GPU Win x64 Builder",
        "ci/Win10 x64 Release (NVIDIA)",
    ],
    description_html = "Experimental shadow builder to test reclient migration. <br/>The bot is shadowing <a href=\"https://ci.chromium.org/p/chromium/builders/try/win10_chromium_x64_rel_ng\">win10_chromium_x64_rel_ng</a>.",
    try_settings = builder_config.try_settings(
        rts_config = builder_config.rts_config(
            condition = builder_config.rts_condition.QUICK_RUN_ONLY,
        ),
        is_compile_only = True,
    ),
    use_clang_coverage = True,
    coverage_test_types = ["unit", "overall"],
    tryjob = try_.job(
        experiment_percentage = 3,
    ),
)

try_.compilator_builder(
    name = "win10_chromium_x64_rel_ng-reclient-compilator",
    builderless = True,
    check_for_flakiness = True,
    # TODO (crbug.com/1245171): Revert when root issue is fixed
    grace_period = 4 * time.minute,
    reclient_instance = reclient.instance.DEFAULT_UNTRUSTED,
    reclient_jobs = reclient.jobs.HIGH_JOBS_FOR_CQ,
)

try_.builder(
    name = "win7-rel",
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    mirrors = [
        "ci/Win Builder",
        "ci/Win7 Tests (1)",
    ],
    cores = 16,
    execution_timeout = 4 * time.hour + 30 * time.minute,
    goma_jobs = goma.jobs.J300,
    main_list_view = "try",
    ssd = True,
    tryjob = try_.job(
        location_filters = [
            "sandbox/win/.+",
            "sandbox/policy/win/.+",
        ],
    ),
)

try_.builder(
    name = "win-fieldtrial-rel",
    os = os.WINDOWS_DEFAULT,
    mirrors = ["ci/win-fieldtrial-rel"],
)

try_.builder(
    name = "win-perfetto-rel",
    mirrors = [
        "ci/win-perfetto-rel",
    ],
)

try_.gpu.optional_tests_builder(
    name = "win_optional_gpu_tests_rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "angle_internal",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
        ),
        build_gs_bucket = "chromium-gpu-fyi-archive",
    ),
    try_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    branch_selector = branches.DESKTOP_EXTENDED_STABLE_MILESTONE,
    main_list_view = "try",
    os = os.WINDOWS_DEFAULT,
    tryjob = try_.job(
        location_filters = [
            cq.location_filter(path_regexp = "chrome/browser/vr/.+"),
            cq.location_filter(path_regexp = "content/browser/xr/.+"),
            cq.location_filter(path_regexp = "content/test/gpu/.+"),
            cq.location_filter(path_regexp = "device/vr/.+"),
            cq.location_filter(path_regexp = "gpu/.+"),
            cq.location_filter(path_regexp = "media/audio/.+"),
            cq.location_filter(path_regexp = "media/base/.+"),
            cq.location_filter(path_regexp = "media/capture/.+"),
            cq.location_filter(path_regexp = "media/filters/.+"),
            cq.location_filter(path_regexp = "media/gpu/.+"),
            cq.location_filter(path_regexp = "media/mojo/.+"),
            cq.location_filter(path_regexp = "media/renderers/.+"),
            cq.location_filter(path_regexp = "media/video/.+"),
            cq.location_filter(path_regexp = "testing/buildbot/tryserver.chromium.win.json"),
            cq.location_filter(path_regexp = "testing/trigger_scripts/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/modules/vr/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/modules/mediastream/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/modules/webcodecs/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/modules/webgl/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/modules/xr/.+"),
            cq.location_filter(path_regexp = "third_party/blink/renderer/platform/graphics/gpu/.+"),
            cq.location_filter(path_regexp = "tools/clang/scripts/update.py"),
            cq.location_filter(path_regexp = "tools/mb/mb_config_expectations/tryserver.chromium.win.json"),
            cq.location_filter(path_regexp = "ui/gl/.+"),
        ],
    ),
)