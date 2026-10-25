#!/usr/bin/env python3
"""Build the modified Android port without regenerating the author's game patches."""
from pathlib import Path
import hashlib,json,os,subprocess,shutil,zipfile

root=Path('/workspace'); cache=Path('/cache'); out=root/'playtest'
sdk=Path(os.environ['ANDROID_HOME']); ndk=Path(os.environ['ANDROID_NDK_HOME'])
def run(*args,**kwargs):
    print('+',*map(str,args),flush=True)
    subprocess.run(list(map(str,args)),check=True,**kwargs)

out.mkdir(exist_ok=True);(root/'.dev').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
dol=root/'Assets/DATA/sys/main.dol'
if hashlib.sha256(dol.read_bytes()).hexdigest()!='db5c9939f677c33d15d446aa4ad19c28044d0b80c5b3cc75adc7f1340ecab162':
    raise RuntimeError('The verified European disc extraction is required.')
if not (root/'generated/translation.json').is_file():raise RuntimeError('Run git sparse-checkout add generated first.')
shutil.copyfile('/game-map/MAP.txt',root/'build/MAP.txt')
run('dotnet','build','-c','Release','toolchain/translator/src/Translator.Cli','--artifacts-path',cache/'dotnet',cwd=root)
translator=cache/'dotnet/bin/Translator.Cli/release/Translator.Cli.dll'
if not translator.is_file():raise RuntimeError('Translator artifact was not produced where expected.')
# Two committed generated functions contain the author's performance patches.
# Keep their source contents and make their metadata hashes consistent.
meta_path=root/'generated/translation.json';meta=json.loads(meta_path.read_text())
for entry in meta['functions']:
    if entry['relativePath'] in ('func_800A8E5C.cpp','func_800CD5E8.cpp'):
        data=(root/'generated/functions'/entry['relativePath']).read_bytes()
        entry['size']=len(data);entry['sha256']=hashlib.sha256(data).hexdigest()
build_meta=root/'.dev/rowell-translation.json'
build_meta.write_text(json.dumps(meta,separators=(',',':')))
os.environ['DOTNET_BIN']=shutil.which('dotnet')
# Pass the actual Android translator to the freshness gate, rather than an old
# checked-in DLL. Generated game-function files are preserved by this operation.
build=root/'build-android'
run('cmake','-S',root/'runtime','-B',build,'-G','Ninja',
    '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
    '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-29','-DANDROID_STL=c++_static',
    '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=Release',
    '-DMH3_AURORA_ROOT='+str(root/'aurora-main'),'-DMH3_BUILD_HOST_TESTS=OFF',
    '-DMH3_TRANSLATOR_DLL='+str(translator),'-DMH3_TRANSLATION_METADATA='+str(build_meta),
    '-DMH3_BUILD_VITA_TEXTURE_PACKER=OFF','-DAURORA_SDL3_PROVIDER=vendor','-DAURORA_SDL3_LINKAGE=shared',
    '-DOPENSSL_ROOT_DIR=/opt/openssl-android','-DOPENSSL_INCLUDE_DIR=/opt/openssl-android/include',
    '-DOPENSSL_SSL_LIBRARY=/opt/openssl-android/lib/libssl.a','-DOPENSSL_CRYPTO_LIBRARY=/opt/openssl-android/lib/libcrypto.a',
    '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384')
run('cmake','--build',build,'--target','MH3','--parallel',os.environ.get('MH3_BUILD_JOBS','2'))
# Recheck the build graph before packaging, including any source updates made
# while a long first build was running. An unchanged build does no compilation.
run('cmake','--build',build,'--target','MH3','--parallel',os.environ.get('MH3_BUILD_JOBS','2'))
run('python3',root/'scripts/rowell_android_package.py')
