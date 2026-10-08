# Client license compliance

Root `LICENSE` supplies the existing GNU GPL version 3 terms, and `NOTICE.md`
preserves server/upstream notices. New client, transport, scripts and shaders
follow that code license. The application is **not CC0**. Runtime production has
no third-party npm dependencies; optional Playwright test tooling is Apache-2.0
and is not shipped. See [code provenance](client-third-party-code.md).

The 13 selected external PNGs are unmodified CC0 works by
ARoachIFoundOnMyPillow; primary-source license and exact-byte provenance were
verified as recorded in [asset sources](client-asset-sources.md). The 33 original
SVG tiles and one original audio-parameter file have an explicit CC0 dedication
in `client/public/assets/LICENSE.md`. CC0 imposes no attribution condition; creator
records are nevertheless preserved for traceability. The code license does not
assert ownership of third-party assets or relicense them.

The client ships no Mojang/Microsoft texture, sound, logo, splash or official
resource pack. It does not extract JAR contents or access proprietary asset
servers. Protocol namespaces, core-pack identifiers and numeric registries remain
unchanged because the actual server depends on them; they do not load proprietary
content. No reference-client code or assets with noncommercial restrictions are
included. Existing technical source notices are preserved.

## Adding assets

1. Locate the original creator's exact file/release and license statement. A
   collection README, repository code license or visual resemblance is insufficient.
2. Accept only explicit CC0, or original artwork/sound dedicated separately to CC0.
   Reject unclear provenance. Check authorship/rights and inspect the artwork/audio;
   avoid proprietary extractions, edits or confusing copied branding.
3. Import only used files. Preserve primary-source evidence, creator, source URL,
   immutable mirror/source commit, exact paths, modifications and intended use.
4. Add the mapping and manifest record, including SHA-256. For a new source,
   deliberately review and extend the audit allowlist, never relax it wholesale.
5. Update the source documentation and embed whitelist. Build, run the audit,
   check fallbacks, and review the final rendered/played assets.

`npm --prefix client run audit` rejects unrecorded files, missing/unsupported
licenses or origins, changed hashes, missing registry assets, prohibited endpoint
references and unexpected HTML branding. Build checks the served route list.
The script supplements the recorded primary-source and visual review; text
matching and hashes cannot prove copyright ownership. Regenerating a manifest
is not a replacement for that review. Tests include negative provenance cases.
