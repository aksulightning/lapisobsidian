#!/usr/bin/env python3
"""Turn staged WASM, JavaScript, assets and license notices into one HTML file."""
import base64
import html
import io
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILENAME = 'LapisCube-singleplayer.html'


def bundle(staged, destination):
    runtime = ['LapisCube.js', 'LapisCube.wasm', 'LapisServer.js',
               'LapisServer.wasm', 'server-worker.js', 'default.zip']
    runtime += [p.relative_to(staged).as_posix() for p in sorted((staged/'sounds').glob('*.wav'))]
    files = {name: (staged/name).read_bytes() for name in runtime}
    worker = files['server-worker.js'].decode()
    loader = "importScripts('LapisServer.js');"
    if worker.count(loader) != 1:
        raise ValueError('Server worker loader changed')
    files['server-worker.js'] = worker.replace(loader, '').encode()

    # Keep the complete provenance and notices available offline, including
    # original assets, hashes, upstream licenses and corresponding-source URL.
    notices = io.BytesIO()
    with zipfile.ZipFile(notices, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(staged.rglob('*')):
            name = path.relative_to(staged).as_posix()
            if path.is_file() and (path.suffix in ('.md', '.txt', '.json') or
                                   name.startswith(('licenses/', 'asset-sources/'))):
                archive.writestr(name, path.read_bytes())
    files['notices.zip'] = notices.getvalue()
    payload = json.dumps({name: base64.b64encode(data).decode('ascii') for name,data in files.items()}, separators=(',', ':'))

    notice_paths = [staged/name for name in ('README.md', 'NOTICE.md', 'credits.md',
                    'SOURCE-server.txt', 'LICENSE-server.txt', 'LICENSE.txt')]
    notice_paths += sorted(p for p in (staged/'licenses').rglob('*') if p.is_file())
    notice_paths += sorted(p for p in (staged/'asset-sources/licenses').rglob('*') if p.is_file())
    sections = ''.join('<details><summary>'+html.escape(p.relative_to(staged).as_posix())+
                       '</summary><pre>'+html.escape(p.read_text())+'</pre></details>' for p in notice_paths)
    dialog = ('<dialog id="embedded-credits"><button id="close-credits">Close</button>'
              '<h2>Open source &amp; asset credits</h2>'
              '<p>All licenses and asset provenance are included in this file.</p>'
              '<a id="download-notices" download="LapisCube-notices.zip">Download notices &amp; provenance</a>'+
              sections+'</dialog>')
    css = (staged/'web.css').read_text()+'\n#embedded-credits{max-width:min(850px,95vw);max-height:90vh;overflow:auto;border:1px solid #628492;border-radius:8px;background:#173440;color:#e6f0ed} #embedded-credits::backdrop{background:#000a} #embedded-credits pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px} #close-credits{float:right}\n'
    page = (staged/'index.html').read_text()
    icon = base64.b64encode((staged/'icon.svg').read_bytes()).decode('ascii')
    page = page.replace('<link rel="manifest" href="app.webmanifest">',
                        '<link rel="icon" href="data:image/svg+xml;base64,'+icon+'">')
    page = page.replace('<link rel="stylesheet" href="web.css">', '<style>'+css+'</style>')
    page = page.replace('<a href="NOTICE.md" target="_blank" rel="noopener">Open source &amp; asset credits</a>',
                        '<button id="show-credits" type="button">Open source &amp; asset credits</button>')
    scripts = '<script id="lapis-bundle" type="application/json">'+payload+'</script>\n'
    # Escape only closing HTML sequences; JS comparison operators must survive.
    for path in (ROOT/'standalone/embedded.js', staged/'singleplayer.js', staged/'web.js'):
        scripts += '<script>'+path.read_text().replace('</', '<\\/')+'</script>\n'
    old = '<script src="singleplayer.js"></script>\n  <script src="web.js"></script>'
    if old not in page:
        raise ValueError('Singleplayer page script layout changed')
    page = page.replace(old, dialog+'\n'+scripts)
    destination.mkdir(parents=True, exist_ok=True)
    output = destination/FILENAME
    output.write_text(page)
    print(f'{output} ({output.stat().st_size:,} bytes; all runtime resources embedded)')
    return output
