#!/usr/bin/env python3
"""Validate a built bundle before bundling it beside the opt-in server."""
import hashlib
import json
import pathlib
import shutil
import sys


def install(source, destination):
    source=pathlib.Path(source).resolve();destination=pathlib.Path(destination).resolve()
    manifest=json.loads((source/'release.json').read_text())
    if manifest.get('platform')!='web': raise ValueError('Expected a LapisCube web package')
    files=manifest['files']
    required={'index.html','web.js','web.css','LapisCube.js','LapisCube.wasm','default.zip','NOTICE.md','LICENSE.txt','licenses/Emscripten.txt'}
    if not required.issubset(files): raise ValueError('Incomplete web bundle')
    actual={p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    if actual!=set(files)|{'release.json'}: raise ValueError('Unexpected bundle files')
    for name,expected in files.items():
        path=pathlib.PurePosixPath(name)
        if path.is_absolute() or any(p.startswith('.') for p in path.parts): raise ValueError('Unsafe bundle path')
        file=source.joinpath(*path.parts)
        if any(p.is_symlink() for p in [file,*file.parents] if p!=source.parent): raise ValueError('Symlinks are not allowed')
        if hashlib.sha256(file.read_bytes()).hexdigest()!=expected: raise ValueError('Hash mismatch: '+name)
    if (source/'LapisCube.wasm').read_bytes()[:4]!=b'\0asm': raise ValueError('Invalid WebAssembly')
    if source==destination:return
    if destination in source.parents or source in destination.parents: raise ValueError('Overlapping bundle directories')
    if destination.exists():shutil.rmtree(destination)
    shutil.copytree(source,destination)


if __name__=='__main__': install(*sys.argv[1:])
