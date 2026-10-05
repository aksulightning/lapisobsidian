"""Check static references before publishing, including project-subpath safety."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit
import re

root = Path(__file__).resolve().parents[2] / 'website'

class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.references = []
        self.h1 = 0

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            assert attrs['id'] not in self.ids, f"Duplicate ID: {attrs['id']}"
            self.ids.add(attrs['id'])
        self.h1 += tag == 'h1'
        if tag == 'img':
            assert 'alt' in attrs, 'Image needs alt text'
        for attr in ('href', 'src'):
            if attrs.get(attr):
                self.references.append(attrs[attr])

page = Page()
page.feed((root / 'index.html').read_text())
assert page.h1 == 1, 'Expected one main heading'
for reference in page.references:
    url = urlsplit(reference)
    if url.scheme or url.netloc:
        assert url.scheme == 'https', f'Expected HTTPS: {reference}'
        continue
    assert not url.path.startswith('/'), f'Root-relative path breaks project Pages: {reference}'
    if url.path:
        assert (root / url.path).is_file(), f'Missing file: {reference}'
    elif url.fragment:
        assert url.fragment in page.ids, f'Missing anchor: {reference}'
for reference in re.findall(r'url\(["\']?([^\)"\']+)', (root / 'style.css').read_text()):
    assert (root / reference).is_file(), f'Missing CSS asset: {reference}'
assert (root / '.nojekyll').exists()
print(f'Website checks passed: {len(page.references)} references, {len(page.ids)} unique IDs.')
