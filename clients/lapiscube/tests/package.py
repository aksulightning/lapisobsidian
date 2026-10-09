#!/usr/bin/env python3
"""Packaging must reject terminal builds, wrong platforms and modified ZIPs."""
import contextlib
import hashlib
import importlib.util
import io
import json
import zipfile
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('packager', ROOT/'tools/package.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


spec = importlib.util.spec_from_file_location('checker', ROOT/'tools/check_package.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class BuildGate(unittest.TestCase):
    def test_archive_integrity(self):
        files = {name: b'fixture' for name in ('README.md','LICENSE','NOTICE.md','licenses/ClassiCube.txt',
            'licenses/ClassiCube-credits.txt','texpacks/default.zip','audio/default.zip','asset-sources/manifest.json','options.txt','connect.sh')}
        files['LapisCube'] = b'\x7fELFfixture'
        release = {'schema_version': 1, 'platform': 'linux',
                   'files': {name: hashlib.sha256(data).hexdigest() for name,data in files.items()}}
        with tempfile.TemporaryDirectory() as tmp:
            archive = pathlib.Path(tmp)/'test.zip'
            for tamper in (False, True):
                with zipfile.ZipFile(archive,'w') as pack:
                    for name,data in files.items():
                        info=zipfile.ZipInfo('LapisCube-linux/'+name);info.external_attr=0o100755 << 16
                        pack.writestr(info, data if not tamper or name!='options.txt' else b'modified')
                    pack.writestr('LapisCube-linux/release.json', json.dumps(release))
                if tamper:
                    with self.assertRaisesRegex(ValueError, 'Hash mismatch'):
                        checker.check(archive)
                else:
                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(checker.check(archive)['platform'], 'linux')

    def test_mode_and_format_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            engine = pathlib.Path(tmp)
            binary = engine/'LapisCube'
            binary.write_bytes(b'\x7fELFfixture')
            binary.chmod(0o755)
            stamp = engine/'.lapiscube-build-mode'
            for mode in (None, 'terminal-release-v2', 'windows-release-v2'):
                if mode:
                    stamp.write_text(mode)
                with self.assertRaises(SystemExit):
                    packager.validate_binary(engine, 'linux')
            stamp.write_text('native-release-v2')
            self.assertEqual(packager.validate_binary(engine, 'linux'), binary)
            binary.chmod(0o644)
            with self.assertRaises(SystemExit):
                packager.validate_binary(engine, 'linux')
            binary.chmod(0o755)
            binary.write_bytes(b'MZwrong-platform')
            with self.assertRaises(SystemExit):
                packager.validate_binary(engine, 'linux')
            stamp.write_text('windows-release-v2')
            (engine/'LapisCube.exe').write_bytes(b'MZfixture')
            self.assertEqual(packager.validate_binary(engine, 'windows'), engine/'LapisCube.exe')
            (engine/'LapisCube.exe').write_bytes(b'\x7fELFwrong-platform')
            with self.assertRaises(SystemExit):
                packager.validate_binary(engine, 'windows')


if __name__ == '__main__':
    unittest.main()
