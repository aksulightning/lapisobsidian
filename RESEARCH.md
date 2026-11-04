# Research sources and implementation decisions

## Research: GitHub Actions Ubuntu runner labels

Date: 2026-11-04
Purpose: Document the requested move from `ubuntu-24.04` to `ubuntu-latest`.

Sources reviewed (GitHub; accessed 2026-11-04):

- [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners): reviewed supported Ubuntu labels and the definition of the `-latest` images.
- [Ubuntu 26 generally available and latest migration](https://github.blog/changelog/2026-09-17-ubuntu-26-generally-available-and-latest-migration/): reviewed the migration of `ubuntu-latest` from Ubuntu 24.04 to 26.04 between October 19 and November 19, 2026.

Verified finding: `ubuntu-latest` follows GitHub's default stable Ubuntu runner image; during the announced migration it can resolve to Ubuntu 24.04 or 26.04. Implementation: replace exact x64 `ubuntu-24.04` runner values with `ubuntu-latest`. Explicit ARM runner labels remain pinned. No external implementation code was copied. Runtime compatibility awaits CI results.

## Research: Browser tests on Ubuntu 26.04

Date: 2026-11-04. Reviewed [Playwright Python release notes](https://playwright.dev/python/docs/release-notes#version-161) (Ubuntu 26.04 support introduced in 1.61) and the [official Python 1.64.1 release](https://github.com/microsoft/playwright-python/releases/tag/v1.64.1) (current stable release), both accessed on this date. The old 1.51.0 pin failed Chromium installation on the new runner. Both browser workflows now pin 1.64.1 while retaining Chromium installation and all existing acceptance tests. No external implementation code was copied.

Browser rotation acceptance uses [Chrome DevTools Protocol device metrics emulation](https://chromedevtools.github.io/devtools-protocol/tot/Emulation/#method-setDeviceMetricsOverride) (accessed 2026-11-04). Reviewed viewport dimensions, mobile emulation, scale factor and screen orientation. New Chromium refuses native window resizing while fullscreen; the test now rotates the emulated viewport and asserts fullscreen remains active, preserving the rotation gate and held-control checks.
