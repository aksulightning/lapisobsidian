# Browser hosting

`npm run build` in `client/` generates a static `dist/` tree. Serve it over HTTPS
in production. The Pages workflow publishes the browser modules at `client/web/`
and the project website at `website/`; root `index.html` opens the game.

The site contains no game server or TCP gateway. Enter the server owner's
`wss://hostname/lapisclient` URL. Configure `LAPIS_WS_ORIGINS` on that server to
include the exact Pages HTTPS origin (scheme and hostname, no project path).
See [TLS and origin configuration](lapisclient-protocol.md).

Existing GitHub Pages deployment is retained. Select GitHub Actions in repository
Settings → Pages to enable the existing deployment workflow. Pull requests only
validate/build the site; deployment happens on the `testing-client` branch.
