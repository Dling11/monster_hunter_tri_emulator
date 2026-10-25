#!/usr/bin/env python3
"""Docker-only translation and validated export for the native Windows build."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/workspace')
GENERATED = ROOT / 'generated'
EXPORT = Path('/windows-generated')
DOL_SHA256 = 'db5c9939f677c33d15d446aa4ad19c28044d0b80c5b3cc75adc7f1340ecab162'
MARKER = 'docker-windows-ready.json'


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_inputs(root=ROOT):
    paths = {root / 'recomp.yml', root / 'symbols.txt', root / 'runtime/CMakeLists.txt',
             root / 'cmake/DockerTranslation.cmake', root / 'docker/translation.Dockerfile'}
    for folder, suffixes in [('runtime/src', {'.cpp', '.h', '.hpp'}),
                              ('runtime/include', {'.h', '.hpp'}),
                              ('runtime/native', {'.c', '.h'}),
                              ('toolchain', {'.cs', '.csproj', '.props', '.targets'}),
                              ('scripts', {'.py'})]:
        paths.update(p for p in (root / folder).rglob('*')
                     if p.is_file() and p.suffix in suffixes and
                     not {'bin', 'obj', '__pycache__'} & set(p.relative_to(root / folder).parts))
    return {p.relative_to(root).as_posix(): sha256(p) for p in sorted(paths)}


def validate_disc():
    data = ROOT / 'Assets/DATA'
    required = ['sys/main.dol', 'sys/boot.bin', 'sys/fst.bin', 'files/mh3.sel']
    for name in required:
        if not (data / name).is_file():
            raise RuntimeError(f'Missing {data / name}; run Prepare with -RomPath first.')
    with (data / 'sys/boot.bin').open('rb') as stream:
        game_id = stream.read(6)
    if game_id != b'RMHP08':
        raise RuntimeError(f'Unsupported disc ID {game_id!r}; this source requires RMHP08.')
    if sha256(data / 'sys/main.dol') != DOL_SHA256:
        raise RuntimeError('main.dol does not match the verified PAL executable. Translation stopped.')
    if not list((data / 'files/01').glob('*.rso')):
        raise RuntimeError('Extracted disc is missing the game modules in files/01.')
    print('Disc verified: RMHP08; main.dol SHA256 matches the source.', flush=True)


def prepare(rom):
    data = ROOT / 'Assets/DATA'
    if data.exists():
        validate_disc()
        print('Reusing the existing verified extraction.', flush=True)
        return
    if not rom or not Path(rom).is_file():
        raise RuntimeError('Provide -RomPath pointing to your PAL ISO/RVZ/WIA image.')
    data.parent.mkdir(exist_ok=True)
    subprocess.run(['nodtool', 'extract', '-q', rom, str(data)], check=True)
    validate_disc()


def windows_text(text, windows_root, assembly=False):
    text = text.replace('/workspace/', windows_root.rstrip('/') + '/')
    if assembly:
        text = text.replace('.section .rodata,"a",%progbits', '.section .rdata,"dr"')
        text = re.sub(r'^\.section \.note\.GNU-stack[^\n]*\n?', '', text, flags=re.M)
    return text


def export_windows(windows_root, inputs):
    if not re.fullmatch(r'[A-Za-z]:/[^"\n\r;]*', windows_root):
        raise RuntimeError('Expected an absolute Windows checkout path with forward slashes.')
    metadata = json.loads((GENERATED / 'translation.json').read_text())
    if any(metadata['quality'][key] for key in ('unsupportedInstructionCount', 'invalidSsaFunctionCount')):
        raise RuntimeError('The translation did not pass its release quality gate.')
    EXPORT.mkdir(exist_ok=True)
    marker = EXPORT / MARKER
    previous = json.loads(marker.read_text()) if marker.exists() else {}
    # A partially exported tree is never accepted by CMake.
    marker.unlink(missing_ok=True)
    skipped = {'translation-state.json', 'native_dispatch_freshness.json'}
    sources = [p for p in sorted(GENERATED.rglob('*'))
               if p.is_file() and not p.name.startswith('.') and p.name not in skipped]

    def copy_output(source):
        relative = source.relative_to(GENERATED)
        destination = EXPORT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix.lower() in {'.cmake', '.s', '.h', '.hpp', '.cpp', '.json'}:
            text = windows_text(source.read_text(), windows_root, source.suffix.lower() == '.s')
            payload = text.encode()
            digest = hashlib.sha256(payload).hexdigest()
            if not destination.exists() or sha256(destination) != digest:
                destination.write_bytes(payload)
        else:
            digest = sha256(source)
            if not destination.exists() or sha256(destination) != digest:
                shutil.copyfile(source, destination)
        return {'path': relative.as_posix(), 'sha256': digest}

    with ThreadPoolExecutor(max_workers=8) as pool:
        outputs = list(pool.map(copy_output, sources))
    current = {entry['path'] for entry in outputs}
    for entry in previous.get('outputs', []):
        relative = Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Invalid path in previous export manifest.')
        if relative.as_posix() not in current:
            (EXPORT / relative).unlink(missing_ok=True)
    if source_inputs() != inputs:
        raise RuntimeError('Source files changed during translation; run Translate again.')
    hash_lists = {}
    input_records = [{'path': path, 'sha256': digest} for path, digest in inputs.items()]
    for group, records in [('inputs', input_records), ('outputs', outputs)]:
        path = EXPORT / f'docker-{group}.sha256'
        path.write_text(''.join(f"{entry['sha256']}  {entry['path']}\n" for entry in records))
        hash_lists[group] = sha256(path)
    result = {'format': 1, 'windowsRoot': windows_root, 'discId': 'RMHP08',
              'dolSha256': DOL_SHA256,
              'inputs': input_records, 'outputs': outputs, 'hashLists': hash_lists}
    temporary = marker.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, indent=2) + '\n')
    temporary.replace(marker)
    print(f'Windows export ready: {len(outputs)} files; verified inputs and output hashes.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['doctor', 'prepare', 'translate'])
    parser.add_argument('--rom')
    parser.add_argument('--windows-root')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    if args.mode == 'doctor':
        subprocess.run(['dotnet', '--version'], check=True)
        subprocess.run(['python3', '--version'], check=True)
        subprocess.run(['nodtool', '--version'], check=True)
        translator = Path(os.environ['WIICOMPILED_ROOT']) / 'translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll'
        if not translator.is_file():
            raise RuntimeError('Prebuilt translator is missing from the image.')
        print('Docker translation tools are ready; no host Python or .NET SDK is required.')
    elif args.mode == 'prepare':
        prepare(args.rom)
    else:
        if not args.windows_root:
            raise RuntimeError('--windows-root is required for Translate.')
        validate_disc()
        inputs = source_inputs()
        # This checkout tracks stale Windows build artifacts. Use only the
        # translator rebuilt from source inside the image.
        command = ['python3', str(ROOT / 'scripts/translate.py')]
        if args.force:
            command.append('--force')
        subprocess.run(command, check=True)
        export_windows(args.windows_root, inputs)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'MH3 Docker: {error}', file=sys.stderr)
        sys.exit(1)

