# Lapis Obsidian Client website

The project landing page is in `website/` on `testing-client`. It is plain
HTML/CSS/JavaScript, with no build dependencies, external fonts, analytics,
backend, or third-party asset requests. Images are unmodified captures of the
working client from the verified browser run at commit `890da398dd15da4bc7172cc8962c6a57539147e5`.
They depict the project's original replacement textures.

The page includes setup commands, keyboard/touch controls, documentation and CI
artifact links, compatibility information, and known limitations. It does not
attempt to run the TCP-dependent game from a public webpage or expose the local
bridge to a remote Origin.

## GitHub Pages

The `Project website` workflow validates and packages `website/` when relevant
files change on `testing-client`. Publishing uses GitHub's official Pages actions.

One-time repository setup (requires repository settings permission):

1. Open **Settings → Pages → Build and deployment**.
2. Set **Source** to **GitHub Actions**.
3. If the `github-pages` environment restricts deployment branches, allow
   `testing-client` under **Settings → Environments → github-pages**.
4. Open **Actions → Project website**, choose the latest `testing-client` run,
   and **Re-run all jobs**. Future website pushes deploy automatically.

The expected project URL is `https://aksulightning.github.io/lapisobsidian/`.
Use the deployment job's returned URL as the authoritative publishing result.
Until Pages is enabled, validation and artifact upload succeed and deployment
is explicitly skipped with a configuration warning. This does not mean the
website is live. Pull requests validate/package only; they do not deploy.

## Edit and check

Edit `website/index.html`, `website/style.css`, and `website/main.js` directly.
Keep asset URLs relative so the site works under the repository URL prefix.

```sh
node --check website/main.js
python3 .github/scripts/check-website.py
# Optional local preview on a developer machine:
python3 -m http.server 8080 --bind 127.0.0.1 --directory website
```

The page works without JavaScript except for control-tab switching and the copy
button. The commands remain selectable. Tabs include keyboard navigation and
ARIA state; disclosures use native HTML. Layouts adapt to phones/tablets and
respect reduced-motion preferences. The website can also be served by any
static host without changing the game client.
