#!/usr/bin/env python3
from pathlib import Path
import os,subprocess,shutil,zipfile,hashlib,json,re,sys
root=Path(__file__).resolve().parents[1];pkg=root/'packaging/android';work=root/'build-android/rowell-apk'
work.mkdir(parents=True,exist_ok=True);classes=work/'classes'
sdk=Path(os.environ['ANDROID_HOME']);tools=sdk/'build-tools/35.0.0';jar=sdk/'platforms/android-35/android.jar'
def tool(name):return tools/(name+('.bat' if name in ('d8','apksigner') else '.exe')) if os.name=='nt' else tools/name
def run(*args,**kwargs):subprocess.run(list(map(str,args)),check=True,**kwargs)
# JNI signatures must come from the same SDL source that built libSDL3.so.
# The Android upstream's checked-in Java targets a different SDL release.
cache=(root/'build-android/CMakeCache.txt').read_text()
abi_match=re.search(r'^(?:CMAKE_ANDROID_ARCH_ABI|ANDROID_ABI):[^=]+=(.+)$',cache,re.M)
if not abi_match:raise RuntimeError('CMake does not identify the compiled Android ABI.')
abi=abi_match.group(1).strip()
if abi not in ('armeabi-v7a','arm64-v8a'):raise RuntimeError('Unsupported Lite Android ABI: '+abi)
source_match=re.search(r'^SDL3_SOURCE_DIR:[^=]+=(.+)$',cache,re.M)
if not source_match:raise RuntimeError('CMake does not identify the SDL native source.')
sdl=Path(source_match.group(1).strip())
sdl_java=sdl/'android-project/app/src/main/java/org/libsdl/app'
header=(sdl/'include/SDL3/SDL_version.h').read_text()
activity=(sdl_java/'SDLActivity.java').read_text()
version=[]
for component in ('MAJOR','MINOR','MICRO'):
    native=re.search(r'#define\s+SDL_'+component+r'_VERSION\s+(\d+)',header)
    bridge=re.search(r'SDL_'+component+r'_VERSION\s*=\s*(\d+)',activity)
    if not native or not bridge or native.group(1)!=bridge.group(1):
        raise RuntimeError('SDL Java/native source versions differ: '+component)
    version.append(native.group(1))
sdl_version='.'.join(version)

def verify_jni():
    native_source=(sdl/'src/core/android/SDL_android.c').read_text()
    tables=re.findall(r'static JNINativeMethod (\w+)_tab\[\]\s*=\s*\{(.*?)\n\};',native_source,re.S)
    if not tables:raise RuntimeError('SDL JNI registration tables were not found.')
    count=0
    for class_name,table in tables:
        declarations=subprocess.check_output(['javap','-private','-s','-classpath',str(classes),'org.libsdl.app.'+class_name],text=True)
        compiled=set(re.findall(r'(?m)^\s+[^\n]*?\b(\w+)\([^;\n]*\);\s*\n\s+descriptor:\s*(\S+)',declarations))
        expected=re.findall(r'\{\s*"(\w+)"\s*,\s*"([^"]+)"',table)
        if not expected:raise RuntimeError('SDL JNI table is empty: '+class_name)
        for name,signature in expected:
            if (name,signature) not in compiled:
                raise RuntimeError('SDL Java/native JNI mismatch: '+class_name+'.'+name+signature)
            count+=1
    print('SDL '+sdl_version+': '+str(count)+' compiled JNI signatures match the native source.',flush=True)
    return count

if sys.argv[1:]==['--check-existing-java']:
    verify_jni()
    sys.exit(0)
check_java=sys.argv[1:]==['--check-java']
if sys.argv[1:] and not check_java:raise RuntimeError('Unknown packaging arguments.')
# Avoid carrying old SDL class files into the replacement dex.
if classes.exists():shutil.rmtree(classes)
classes.mkdir()
java=list(sorted(sdl_java.glob('*.java')))
if not java:raise RuntimeError('Matching SDL Java sources are missing.')
# Keep the matching 3.4.4 bridge and apply only the reviewed input-connection
# override/helper. The complete upstream Java tree uses a different SDL ABI.
if sdl_version!='3.4.4':raise RuntimeError('Review the Lite IME override before changing SDL version.')
overrides=pkg/'src/org/libsdl/app'
java=[p for p in java if p.name!='SDLInputConnection.java']
java += [overrides/'SDLInputConnection.java',overrides/'RowellImeText.java']
java+=sorted(p for p in (pkg/'src').rglob('*.java') if (pkg/'src/org/libsdl') not in p.parents)
sources=work/'sources.txt';sources.write_text('\n'.join('"'+p.as_posix()+'"' for p in java)+'\n',encoding='utf-8')
run('javac','-encoding','UTF-8','--release','17','-cp',jar,'-d',classes,'@'+str(sources))
jni_count=verify_jni()
if check_java:sys.exit(0)
# Use a response file: the generated Activity classes exceed cmd.exe's 8191
# character limit when invoking the Windows d8 launcher.
d8_args=work/'d8-args.txt'
d8_args.write_text('--output\n'+work.as_posix()+'\n--lib\n'+jar.as_posix()+'\n--min-api\n29\n'+
    '\n'.join(p.as_posix() for p in sorted(classes.rglob('*.class')))+'\n',encoding='utf-8')
run(tool('d8'),'@'+str(d8_args))
run(tool('aapt2'),'compile','--dir',pkg/'res','-o',work/'resources.zip')
base=work/'base.apk'
if not (pkg/'assets/licenses/Inter-OFL.txt').is_file():raise RuntimeError('The bundled Inter font license is missing.')
run(tool('aapt2'),'link','-o',base,'-I',jar,'--manifest',pkg/'AndroidManifest.xml',work/'resources.zip','--auto-add-overlay','-A',pkg/'assets')
libdir=work/'native';libdir.mkdir(exist_ok=True)
required=['libmain.so','libpng16.so','libSDL3.so']
libs=[]
for name in required:
    candidates=list((root/'build-android').rglob(name))
    candidates=[p for p in candidates if work not in p.parents and p.is_file()]
    if not candidates:raise RuntimeError('Missing native library: '+name)
    dest=libdir/name;shutil.copyfile(candidates[0],dest)
    elf=dest.read_bytes()[:20]
    expected_class,expected_machine=(1,40) if abi=='armeabi-v7a' else (2,183)
    if len(elf)<20 or elf[:4]!=b'\x7fELF' or elf[4]!=expected_class or elf[5]!=1 or int.from_bytes(elf[18:20],'little')!=expected_machine:
        raise RuntimeError('Native library does not match '+abi+': '+name)
    ndk_host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
    run(Path(os.environ['ANDROID_NDK_HOME'])/('toolchains/llvm/prebuilt/'+ndk_host+'/bin/llvm-strip'),'--strip-unneeded',dest)
    libs.append(dest)
with zipfile.ZipFile(base,'a',compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(work/'classes.dex','classes.dex')
    for lib in libs:apk.write(lib,'lib/'+abi+'/'+lib.name)
aligned=work/'aligned.apk';run(tool('zipalign'),'-P','16','-f','4',base,aligned)
key=root/'.dev/rowell-lite.keystore'
if not key.is_file():
    run('keytool','-genkeypair','-keystore',key,'-alias','rowell-lite','-keyalg','RSA','-keysize','2048','-validity','10000',
        '-storepass','android','-keypass','android','-dname','CN=MH Tri Rowell Lite Experimental')
output=root/('playtest/MH3-Low-Spec-0.1-'+abi+'.apk');output.parent.mkdir(exist_ok=True)
run(tool('apksigner'),'sign','--ks',key,'--ks-key-alias','rowell-lite','--ks-pass','pass:android','--key-pass','pass:android',
    '--v1-signing-enabled','true','--v2-signing-enabled','true','--v3-signing-enabled','true','--out',output,aligned)
run(tool('apksigner'),'verify','--verbose',output)
run(tool('zipalign'),'-c','-P','16','4',output)
manifest=subprocess.check_output([str(tool('aapt2')),'dump','badging',str(output)],text=True)
if "package: name='com.rowell.mh3lite'" not in manifest:raise RuntimeError('APK package identity is not isolated.')
print(manifest[:1600],flush=True)
(output.with_suffix('.json')).write_text(json.dumps({'package':'com.rowell.mh3lite','dataFolder':'MH3_Low_Spec',
    'versionName':re.search(r"versionName='([^']+)'",manifest).group(1),
    'versionCode':int(re.search(r"versionCode='(\d+)'",manifest).group(1)),
    'minSdk':int(re.search(r"(?:minSdkVersion|sdkVersion):'(\d+)'",manifest).group(1)),
    'abi':abi,'appName':re.search(r"^application-label:'([^']+)'",manifest,re.M).group(1),
    'sdlVersion':sdl_version,'verifiedJniMethods':jni_count,
    'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'touchSourceSHA256':hashlib.sha256((root/'runtime/src/touch_controls.cpp').read_bytes()).hexdigest()},indent=2))
print('Custom APK:',output,flush=True)
