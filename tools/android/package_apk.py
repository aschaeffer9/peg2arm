#!/usr/bin/env python3
"""Packages the Android build into a signed APK with the Android SDK's command-line tools (no Gradle).

  javac (SDL3's Java activity + android/java) -> jar -> d8 (classes.dex)
  aapt2 compile + link (android/AndroidManifest.xml, android/res) -> base APK
  + classes.dex + lib/arm64-v8a/libmain.so (debug info stripped) -> zipalign -> apksigner (a local debug key)

Usage:
  python tools/android/package_apk.py --so out/android/libmain.so --out out/android/peggle2.apk
      [--sdk %LOCALAPPDATA%/Android/Sdk] [--build-tools 36.0.0] [--platform android-36] [--jdk ~/.jdks/jbr-...]
      [--ndk C:/android/android-ndk-r28c] [--keystore out/android-debug.keystore]
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IS_WINDOWS = os.name == 'nt'


def fail(message):
    sys.exit(f'package_apk: {message}')


def tool(directory, name):
    """A tool in a folder: name.exe / name.bat / name on Windows, name elsewhere."""
    candidates = [name + '.exe', name + '.bat', name] if IS_WINDOWS else [name]
    for candidate in candidates:
        path = os.path.join(directory, candidate)
        if os.path.isfile(path):
            return path
    fail(f'no {name} in {directory}')


def run(args, env=None, cwd=None):
    print('>', ' '.join(str(a) for a in args), flush=True)
    if IS_WINDOWS and str(args[0]).lower().endswith('.bat'):
        args = ['cmd.exe', '/c'] + [str(a) for a in args]
    result = subprocess.run([str(a) for a in args], env=env, cwd=cwd)
    if result.returncode != 0:
        fail(f'{os.path.basename(str(args[0]))} failed with exit code {result.returncode}')


def newest(pattern):
    found = sorted(glob.glob(pattern))
    return found[-1] if found else None


def main():
    home = os.path.expanduser('~')
    ap = argparse.ArgumentParser()
    ap.add_argument('--so', required=True, help='libmain.so from the NDK build')
    ap.add_argument('--out', required=True, help='the signed APK to write')
    ap.add_argument('--sdk', default=os.path.join(os.environ.get('LOCALAPPDATA', home), 'Android', 'Sdk'))
    ap.add_argument('--build-tools', default='')
    ap.add_argument('--platform', default='android-36')
    ap.add_argument('--jdk', default='')
    ap.add_argument('--ndk', default='')
    ap.add_argument('--sdl-java', default=os.path.join(ROOT, 'sdk', 'thirdparty', 'sdl3', 'android-project', 'app',
                                                       'src', 'main', 'java'))
    ap.add_argument('--android', default=os.path.join(ROOT, 'android'))
    ap.add_argument('--keystore', default=os.path.join(ROOT, 'out', 'android-debug.keystore'))
    ap.add_argument('--min-sdk', default='31')
    ap.add_argument('--target-sdk', default='34')
    a = ap.parse_args()

    if not os.path.isfile(a.so):
        fail(f'no {a.so}')
    build_tools = (os.path.join(a.sdk, 'build-tools', a.build_tools) if a.build_tools
                   else newest(os.path.join(a.sdk, 'build-tools', '*')))
    if not build_tools or not os.path.isdir(build_tools):
        fail(f'no build-tools under {a.sdk}')
    android_jar = os.path.join(a.sdk, 'platforms', a.platform, 'android.jar')
    if not os.path.isfile(android_jar):
        fail(f'no {android_jar}')
    jdk = a.jdk or newest(os.path.join(home, '.jdks', '*'))
    if not jdk or not os.path.isdir(os.path.join(jdk, 'bin')):
        fail('no JDK (pass --jdk)')
    sdl_java = os.path.join(a.sdl_java, 'org', 'libsdl', 'app')
    if not os.path.isdir(sdl_java):
        fail(f'no SDL Java sources in {sdl_java}')
    print(f'build-tools {build_tools}\nplatform    {android_jar}\njdk         {jdk}', flush=True)

    env = dict(os.environ)
    env['JAVA_HOME'] = jdk
    env['PATH'] = os.path.join(jdk, 'bin') + os.pathsep + env.get('PATH', '')

    work = os.path.join(os.path.dirname(os.path.abspath(a.out)), 'apk_build')
    shutil.rmtree(work, ignore_errors=True)
    classes = os.path.join(work, 'classes')
    os.makedirs(classes)

    # 1. Java -> classes -> classes.jar -> classes.dex.
    sources = sorted(glob.glob(os.path.join(sdl_java, '*.java')) +
                     glob.glob(os.path.join(a.android, 'java', '**', '*.java'), recursive=True))
    sources_list = os.path.join(work, 'sources.txt')
    with open(sources_list, 'w', encoding='utf-8') as f:
        f.write('\n'.join('"' + s.replace('\\', '/') + '"' for s in sources) + '\n')
    run([tool(os.path.join(jdk, 'bin'), 'javac'), '-encoding', 'UTF-8', '-source', '8', '-target', '8',
         '-Xlint:-options', '-nowarn', '-bootclasspath', android_jar, '-d', classes, '@' + sources_list], env=env)
    classes_jar = os.path.join(work, 'classes.jar')
    run([tool(os.path.join(jdk, 'bin'), 'jar'), 'cf', classes_jar, '-C', classes, '.'], env=env)
    dex_dir = os.path.join(work, 'dex')
    os.makedirs(dex_dir)
    run([tool(build_tools, 'd8'), '--release', '--min-api', a.min_sdk, '--lib', android_jar,
         '--output', dex_dir, classes_jar], env=env)
    dex = os.path.join(dex_dir, 'classes.dex')
    if not os.path.isfile(dex):
        fail('d8 wrote no classes.dex')

    # 2. Resources and manifest -> base APK.
    res_zip = os.path.join(work, 'res.zip')
    run([tool(build_tools, 'aapt2'), 'compile', '--dir', os.path.join(a.android, 'res'), '-o', res_zip], env=env)
    base_apk = os.path.join(work, 'base.apk')
    run([tool(build_tools, 'aapt2'), 'link', '-o', base_apk, '-I', android_jar,
         '--manifest', os.path.join(a.android, 'AndroidManifest.xml'),
         '--min-sdk-version', a.min_sdk, '--target-sdk-version', a.target_sdk, res_zip], env=env)

    # 3. The native library, without its debug info (the unstripped one stays next to the build for symbolization).
    so_for_apk = os.path.join(work, 'libmain.so')
    strip = None
    if a.ndk:
        strip = newest(os.path.join(a.ndk, 'toolchains', 'llvm', 'prebuilt', '*', 'bin',
                                    'llvm-strip' + ('.exe' if IS_WINDOWS else '')))
    if strip:
        run([strip, '--strip-debug', '-o', so_for_apk, a.so], env=env)
    else:
        print('package_apk: no llvm-strip (pass --ndk): the library goes in with its debug info', flush=True)
        shutil.copyfile(a.so, so_for_apk)

    # 4. Base APK + dex + library.
    unaligned = os.path.join(work, 'unaligned.apk')
    with zipfile.ZipFile(base_apk) as src, zipfile.ZipFile(unaligned, 'w', zipfile.ZIP_DEFLATED) as dst:
        for info in src.infolist():
            dst.writestr(info, src.read(info.filename), compress_type=info.compress_type)
        dst.write(dex, 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
        dst.write(so_for_apk, 'lib/arm64-v8a/libmain.so', compress_type=zipfile.ZIP_DEFLATED)
    aligned = os.path.join(work, 'aligned.apk')
    run([tool(build_tools, 'zipalign'), '-p', '-f', '4', unaligned, aligned], env=env)

    # 5. Sign with a local debug key (made once).
    if not os.path.isfile(a.keystore):
        os.makedirs(os.path.dirname(os.path.abspath(a.keystore)), exist_ok=True)
        run([tool(os.path.join(jdk, 'bin'), 'keytool'), '-genkeypair', '-keystore', a.keystore, '-storepass', 'android',
             '-keypass', 'android', '-alias', 'peggle2debug', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
             '-dname', 'CN=Peggle 2 Android debug'], env=env)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    run([tool(build_tools, 'apksigner'), 'sign', '--ks', a.keystore, '--ks-pass', 'pass:android',
         '--key-pass', 'pass:android', '--out', a.out, aligned], env=env)
    run([tool(build_tools, 'apksigner'), 'verify', a.out], env=env)

    with zipfile.ZipFile(a.out) as z:
        names = z.namelist()
    for required in ('AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libmain.so'):
        if required not in names:
            fail(f'{required} missing from {a.out}')
    print(f'package_apk: {a.out} ({os.path.getsize(a.out) >> 20} MB; libmain.so '
          f'{os.path.getsize(so_for_apk) >> 20} MB in it)', flush=True)


if __name__ == '__main__':
    main()
