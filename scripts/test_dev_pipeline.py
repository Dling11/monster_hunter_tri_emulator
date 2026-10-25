#!/usr/bin/env python3
"""Small integration checks for hashing and the Docker-to-Windows export."""
import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parent


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PipelineChecks(unittest.TestCase):
    def test_parallel_hashes_equal_serial_hashes(self):
        dispatch = load('dispatch_for_test', 'prepare_native_dispatch.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('index.json', 'Translator.Cli.dll', 'Translator.Core.dll', 'one.cpp', 'two.cpp'):
                (root / name).write_text(name)
            metadata = root / 'translation.json'
            metadata.write_text('{}')
            args = SimpleNamespace(native_index=root / 'index.json', base_metadata=metadata,
                                   translator=root / 'Translator.Cli.dll', functions=root,
                                   resolved_profile=None, retro_cpp_dir=None)
            previous = os.environ.get('MH3_HASH_JOBS')
            try:
                os.environ['MH3_HASH_JOBS'] = '1'
                serial = dispatch.input_digests(args)
                os.environ['MH3_HASH_JOBS'] = '8'
                parallel = dispatch.input_digests(args)
            finally:
                if previous is None:
                    os.environ.pop('MH3_HASH_JOBS', None)
                else:
                    os.environ['MH3_HASH_JOBS'] = previous
            self.assertEqual(list(serial.items()), list(parallel.items()))
            self.assertEqual(parallel[str((root / 'one.cpp').resolve())], dispatch.digest(root / 'one.cpp'))

    def test_export_is_windows_compatible_and_rejects_source_changes(self):
        dev = load('container_for_test', 'dev-container.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dev.ROOT, dev.GENERATED, dev.EXPORT = root, root / 'generated', root / 'export'
            for relative in ('recomp.yml', 'symbols.txt', 'runtime/CMakeLists.txt',
                             'cmake/DockerTranslation.cmake', 'docker/translation.Dockerfile',
                             'runtime/src/controller.cpp', 'runtime/include/controller.h'):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture\n')
            dev.GENERATED.mkdir()
            (dev.GENERATED / 'translation.json').write_text(json.dumps({
                'quality': {'unsupportedInstructionCount': 0, 'invalidSsaFunctionCount': 0}}))
            (dev.GENERATED / 'data.S').write_text(
                '.section .rodata,"a",%progbits\n.incbin "/workspace/generated/data.bin"\n'
                '.section .note.GNU-stack,"",%progbits\n')
            (dev.GENERATED / 'data.bin').write_bytes(bytes(range(256)))
            # source_inputs' default root is defined at import time.
            source_inputs = dev.source_inputs
            dev.source_inputs = lambda: source_inputs(root)
            inputs = dev.source_inputs()
            dev.export_windows('D:/fixture', inputs)
            assembly = (dev.EXPORT / 'data.S').read_text()
            self.assertIn('.section .rdata,"dr"', assembly)
            self.assertIn('D:/fixture/generated/data.bin', assembly)
            self.assertNotIn('.note.GNU-stack', assembly)
            self.assertEqual((dev.EXPORT / 'data.bin').read_bytes(), bytes(range(256)))
            manifest = json.loads((dev.EXPORT / dev.MARKER).read_text())
            for entry in manifest['outputs']:
                self.assertEqual(entry['sha256'], dev.sha256(dev.EXPORT / entry['path']))
            (root / 'runtime/src/controller.cpp').write_text('edited')
            with self.assertRaisesRegex(RuntimeError, 'Source files changed'):
                dev.export_windows('D:/fixture', inputs)
            self.assertFalse((dev.EXPORT / dev.MARKER).exists())


if __name__ == '__main__':
    unittest.main()
