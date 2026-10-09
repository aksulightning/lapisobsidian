#!/usr/bin/env python3
"""Verify the complete contents of a LapisCube development ZIP before launch."""
import hashlib
import json
import pathlib
import sys
import zipfile


def check(path):
    with zipfile.ZipFile(path) as pack:
        entries = pack.infolist()
        names = [item.filename for item in entries if not item.is_dir()]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate archive paths')
        for name in names:
            p = pathlib.PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name:
                raise ValueError('Unsafe archive path')
        manifests = [name for name in names if name.count('/') == 1 and name.endswith('/release.json')]
        if len(manifests) != 1:
            raise ValueError('Expected one release manifest')
        root = manifests[0].split('/')[0]+'/'
        release = json.loads(pack.read(manifests[0]))
        if release['schema_version'] != 1 or release['platform'] not in ('linux', 'windows'):
            raise ValueError('Unsupported release manifest')
        expected = {root+name for name in release['files']}
        if set(names) != expected | {manifests[0]}:
            raise ValueError('Unlisted or missing archive contents')
        for name, digest in release['files'].items():
            if hashlib.sha256(pack.read(root+name)).hexdigest() != digest:
                raise ValueError('Hash mismatch: '+name)
        required = {'README.md','LICENSE','NOTICE.md','licenses/ClassiCube.txt','licenses/ClassiCube-credits.txt',
                    'texpacks/default.zip','audio/default.zip','asset-sources/manifest.json','options.txt'}
        binary, launcher = ('LapisCube','connect.sh') if release['platform']=='linux' else ('LapisCube.exe','connect.bat')
        if not required | {binary, launcher} <= set(release['files']):
            raise ValueError('Missing runtime resources or license notices')
        header = pack.read(root+binary)[:4]
        if release['platform']=='linux':
            if header != b'\x7fELF' or not pack.getinfo(root+launcher).external_attr >> 16 & 0o111:
                raise ValueError('Invalid Linux executable/launcher')
            if not pack.getinfo(root+binary).external_attr >> 16 & 0o111:
                raise ValueError('Linux executable permission lost')
        elif header[:2] != b'MZ':
            raise ValueError('Invalid Windows executable')
    print(f"package: {release['platform']}, {len(expected)} file hashes, runtime resources and licenses verified")
    return release


if __name__ == '__main__':
    check(sys.argv[1])
