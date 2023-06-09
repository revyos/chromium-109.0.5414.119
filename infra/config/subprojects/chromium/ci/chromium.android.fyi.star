# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the chromium.android.fyi builder group."""

load("//lib/builder_config.star", "builder_config")
load("//lib/builders.star", "os", "reclient")
load("//lib/ci.star", "ci")
load("//lib/consoles.star", "consoles")

ci.defaults.set(
    builder_group = "chromium.android.fyi",
    cores = 8,
    executable = ci.DEFAULT_EXECUTABLE,
    execution_timeout = ci.DEFAULT_EXECUTION_TIMEOUT,
    os = os.LINUX_DEFAULT,
    pool = ci.DEFAULT_POOL,
    priority = ci.DEFAULT_FYI_PRIORITY,
    service_account = ci.DEFAULT_SERVICE_ACCOUNT,
    reclient_jobs = reclient.jobs.DEFAULT,
    reclient_instance = reclient.instance.DEFAULT_TRUSTED,
)

consoles.console_view(
    name = "chromium.android.fyi",
    ordering = {
        None: ["android", "memory", "weblayer", "webview"],
    },
)

ci.builder(
    name = "Android ASAN (dbg) (reclient)",
    console_view_entry = consoles.console_view_entry(
        category = "builder|arm",
        short_name = "san",
    ),
    # Higher build timeout since dbg ASAN builds can take a while on a clobber
    # build.
    execution_timeout = 4 * time.hour,
    schedule = "triggered",  # triggered manually via Scheduler UI
)

ci.builder(
    name = "android-pie-arm64-wpt-rel-non-cq",
    console_view_entry = consoles.console_view_entry(
        category = "wpt|webview",
        short_name = "p-arm64",
    ),
)

ci.builder(
    name = "android-chrome-pie-x86-wpt-fyi-rel",
    console_view_entry = consoles.console_view_entry(
        category = "wpt|chrome",
        short_name = "p-x86",
    ),
)

ci.builder(
    name = "android-webview-pie-x86-wpt-fyi-rel",
    console_view_entry = consoles.console_view_entry(
        category = "wpt|webview",
        short_name = "p-x86",
    ),
)

# TODO(crbug.com/1022533#c40): Remove this builder once there are no associated
# disabled tests.
ci.builder(
    name = "android-pie-x86-fyi-rel",
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x86|rel",
        short_name = "P",
    ),
    # Set to an empty list to avoid chromium-gitiles-trigger triggering new
    # builds. Also we don't set any `schedule` since this builder is for
    # reference only and should not run any new builds.
    triggered_by = [],
)

# TODO(crbug.com/1137474): This and android-12-x64-fyi-rel
# are being kept around so that build links in the related
# bugs are accessible
# Remove these once the bugs are closed
ci.builder(
    name = "android-11-x86-fyi-rel",
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x86|rel",
        short_name = "11",
    ),
    # Set to an empty list to avoid chromium-gitiles-trigger triggering new
    # builds. Also we don't set any `schedule` since this builder is for
    # reference only and should not run any new builds.
    triggered_by = [],
)

ci.builder(
    name = "android-12-x64-fyi-rel",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "android",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "x64_builder",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "12",
    ),
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    # Matching the execution time out of the android-12-x64-rel
    execution_timeout = 4 * time.hour,
    triggered_by = ["ci/android-12-x64-rel"],
)

ci.builder(
    name = "android-annotator-rel",
    console_view_entry = consoles.console_view_entry(
        category = "network|traffic|annotations",
        short_name = "and",
    ),
    notifies = ["annotator-rel"],
)

# TODO(crbug.com/1299910): Move to non-FYI once the tester works fine.
ci.thin_tester(
    name = "android-webview-12-x64-dbg-tests",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "android",
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "x64_builder_mb",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    console_view_entry = consoles.console_view_entry(
        category = "tester|webview",
        short_name = "12",
    ),
    triggered_by = ["Android x64 Builder (dbg)"],
)

# TODO(crbug.com/1299910): Move to non-FYI once the tester works fine.
ci.thin_tester(
    name = "android-12-x64-dbg-tests",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "android",
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "x64_builder_mb",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    console_view_entry = consoles.console_view_entry(
        category = "tester|phone",
        short_name = "12",
    ),
    triggered_by = ["Android x64 Builder (dbg)"],
)

ci.builder(
    name = "android-cronet-asan-x86-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "android",
            apply_configs = ["cronet_builder", "mb"],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 32,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "x86_builder",
        ),
        execution_mode = builder_config.execution_mode.COMPILE_AND_TEST,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "cronet|asan",
    ),
)
