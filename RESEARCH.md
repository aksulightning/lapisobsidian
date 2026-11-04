# Research sources and implementation decisions

## Research: GitHub Actions Ubuntu runner labels

Date: 2026-11-04
Purpose: Document the requested move from `ubuntu-24.04` to `ubuntu-latest`.

Sources reviewed (GitHub; accessed 2026-11-04):

- [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners): reviewed supported Ubuntu labels and the definition of the `-latest` images.
- [Ubuntu 26 generally available and latest migration](https://github.blog/changelog/2026-09-17-ubuntu-26-generally-available-and-latest-migration/): reviewed the migration of `ubuntu-latest` from Ubuntu 24.04 to 26.04 between October 19 and November 19, 2026.

Verified finding: `ubuntu-latest` follows GitHub's default stable Ubuntu runner image; during the announced migration it can resolve to Ubuntu 24.04 or 26.04. Implementation: replace exact x64 `ubuntu-24.04` runner values with `ubuntu-latest`. Explicit ARM runner labels remain pinned. No external implementation code was copied. Runtime compatibility awaits CI results.
