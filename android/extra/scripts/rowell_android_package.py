#!/usr/bin/env python3
from pathlib import Path
import os,subprocess,shutil,zipfile,hashlib,json,re,sys
root=Path('/workspace');pkg=root/'packaging/android';work=root/'build-android/rowell-apk'
work.mkdir(parents=True,exist_ok=True);classes=work/'classes'
sdk=Path(os.environ['ANDROID_HOME']);tools=sdk/'build-tools/35.0.0';jar=sdk/'platforms/android-35/android.jar'
def run(*args,**kwargs):subprocess.run(list(map(str,args)),check=True,**kwargs)
# JNI signatures must come from the same SDL source that built libSDL3.so.
# The Android upstream's checked-in Java targets a different SDL release.
cache=(root/'build-android/CMakeCache.txt').read_text()
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
if sys.argv[1:]:raise RuntimeError('Unknown packaging arguments.')
# Avoid carrying old SDL class files into the replacement dex.
if classes.exists():shutil.rmtree(classes)
classes.mkdir()
java=list(sorted(sdl_java.glob('*.java')))
if not java:raise RuntimeError('Matching SDL Java sources are missing.')
java+=sorted(p for p in (pkg/'src').rglob('*.java') if (pkg/'src/org/libsdl') not in p.parents)
sources=work/'sources.txt';sources.write_text('\n'.join(str(p) for p in java)+'\n')
run('javac','--release','17','-cp',jar,'-d',classes,'@'+str(sources))
jni_count=verify_jni()
run(tools/'d8','--output',work,'--lib',jar,'--min-api','29',*sorted(classes.rglob('*.class')))
run(tools/'aapt2','compile','--dir',pkg/'res','-o',work/'resources.zip')
base=work/'base.apk'
run(tools/'aapt2','link','-o',base,'-I',jar,'--manifest',pkg/'AndroidManifest.xml',work/'resources.zip','--auto-add-overlay')
libdir=work/'native';libdir.mkdir(exist_ok=True)
required=['libmain.so','libpng16.so','libSDL3.so']
libs=[]
for name in required:
    candidates=list((root/'build-android').rglob(name))
    candidates=[p for p in candidates if work not in p.parents and p.is_file()]
    if not candidates:raise RuntimeError('Missing native library: '+name)
    dest=libdir/name;shutil.copyfile(candidates[0],dest)
    run(Path(os.environ['ANDROID_NDK_HOME'])/'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip','--strip-unneeded',dest)
    libs.append(dest)
with zipfile.ZipFile(base,'a',compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(work/'classes.dex','classes.dex')
    for lib in libs:apk.write(lib,'lib/arm64-v8a/'+lib.name)
aligned=work/'aligned.apk';run(tools/'zipalign','-P','16','-f','4',base,aligned)
key=root/'.dev/rowell-dev.keystore'
if not key.is_file():
    run('keytool','-genkeypair','-keystore',key,'-alias','rowell-dev','-keyalg','RSA','-keysize','2048','-validity','10000',
        '-storepass','android','-keypass','android','-dname','CN=MH Tri Rowell Development')
output=root/'playtest/MH3-Rowell-Dev-1.4.1-arm64.apk';output.parent.mkdir(exist_ok=True)
run(tools/'apksigner','sign','--ks',key,'--ks-key-alias','rowell-dev','--ks-pass','pass:android','--key-pass','pass:android',
    '--v1-signing-enabled','true','--v2-signing-enabled','true','--v3-signing-enabled','true','--out',output,aligned)
run(tools/'apksigner','verify','--verbose',output)
run(tools/'zipalign','-c','-P','16','4',output)
manifest=subprocess.check_output([str(tools/'aapt2'),'dump','badging',str(output)],text=True)
if "package: name='com.rowell.mh3dev'" not in manifest:raise RuntimeError('APK package identity is not isolated.')
print(manifest[:1600],flush=True)
(output.with_suffix('.json')).write_text(json.dumps({'package':'com.rowell.mh3dev','dataFolder':'MH3_Rowell_Dev',
    'versionName':re.search(r"versionName='([^']+)'",manifest).group(1),
    'versionCode':int(re.search(r"versionCode='(\d+)'",manifest).group(1)),
    'minSdk':int(re.search(r"(?:minSdkVersion|sdkVersion):'(\d+)'",manifest).group(1)),
    'abi':'arm64-v8a','appName':re.search(r"^application-label:'([^']+)'",manifest,re.M).group(1),
    'sdlVersion':sdl_version,'verifiedJniMethods':jni_count,
    'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'touchSourceSHA256':hashlib.sha256((root/'runtime/src/touch_controls.cpp').read_bytes()).hexdigest()},indent=2))
print('Custom APK:',output,flush=True)
