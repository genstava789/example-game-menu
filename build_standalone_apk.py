import os
import sys
import subprocess
import shutil
import zipfile

# Configuration paths
SDK_DIR = r"C:\Android\android-sdk"
BUILD_TOOLS_DIR = os.path.join(SDK_DIR, "build-tools", "34.0.0")
PLATFORM_JAR = os.path.join(SDK_DIR, "platforms", "android-34", "android.jar")
AAPT2 = os.path.join(BUILD_TOOLS_DIR, "aapt2.exe")
D8 = os.path.join(BUILD_TOOLS_DIR, "d8.bat")
ZIPALIGN = os.path.join(BUILD_TOOLS_DIR, "zipalign.exe")
APKSIGNER = os.path.join(BUILD_TOOLS_DIR, "apksigner.bat")

PROJECT_DIR = os.path.abspath(r"C:\Users\administrator\Downloads\Template Mod Menu 64bit")
SHORT_PROJECT_DIR = r"C:\Users\administrator\Downloads\TEMPLA~1"
APP_DIR = os.path.join(PROJECT_DIR, "app")
SRC_DIR = os.path.join(APP_DIR, "src", "main")
RES_DIR = os.path.join(SRC_DIR, "res")
MANIFEST = os.path.join(SRC_DIR, "AndroidManifest.xml")
JAVA_SRC_DIR = os.path.join(SRC_DIR, "java")
LIBS_DIR = os.path.join(SRC_DIR, "libs")

BUILD_DIR = os.path.join(PROJECT_DIR, "build_standalone")

def run_cmd(cmd, cwd=PROJECT_DIR, check=True):
    print(f"\n[EXEC] {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    res = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True, text=True)
    if res.stdout:
        print(res.stdout.strip())
    if res.stderr:
        print(res.stderr.strip())
    if check and res.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {res.returncode}")
    return res

def main():
    print("="*60)
    print("  BUILDING STANDALONE MOD MENU APK (ARM64)")
    print("="*60)
    
    if os.path.exists(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
    os.makedirs(BUILD_DIR, exist_ok=True)
    
    gen_dir = os.path.join(BUILD_DIR, "gen")
    classes_dir = os.path.join(BUILD_DIR, "classes")
    compiled_res = os.path.join(BUILD_DIR, "compiled_res.zip")
    base_apk = os.path.join(BUILD_DIR, "base.apk")
    unaligned_apk = os.path.join(BUILD_DIR, "unaligned.apk")
    aligned_apk = os.path.join(BUILD_DIR, "aligned.apk")
    final_apk = os.path.join(PROJECT_DIR, "ModMenu_Standalone_arm64.apk")
    keystore = os.path.join(BUILD_DIR, "debug.keystore")
    
    os.makedirs(gen_dir, exist_ok=True)
    os.makedirs(classes_dir, exist_ok=True)
    
    # Step 1: Compile native libs with ndk-build
    print("\n[*] Step 1: Compiling native libraries with ndk-build...")
    ndk_build = os.path.join(SDK_DIR, "ndk", "27.3.13750724", "ndk-build.cmd")
    run_cmd(f'"{ndk_build}" NDK_PROJECT_PATH="{SHORT_PROJECT_DIR}\\app\\src\\main" APP_BUILD_SCRIPT="{SHORT_PROJECT_DIR}\\app\\src\\main\\jni\\Android.mk" NDK_APPLICATION_MK="{SHORT_PROJECT_DIR}\\app\\src\\main\\jni\\Application.mk" APP_ABI="arm64-v8a"')
    run_cmd(f'"{ndk_build}" NDK_PROJECT_PATH="{SHORT_PROJECT_DIR}\\app\\src\\main" APP_BUILD_SCRIPT="{SHORT_PROJECT_DIR}\\app\\src\\main\\jni\\Android.mk" NDK_APPLICATION_MK="{SHORT_PROJECT_DIR}\\app\\src\\main\\jni\\Application.mk" APP_ABI="armeabi-v7a"')
        
    # Step 2: Compile resources with AAPT2
    print("\n[*] Step 2: Compiling resources with aapt2...")
    run_cmd([AAPT2, "compile", "--dir", RES_DIR, "-o", compiled_res])
    
    # Step 3: Link resources with AAPT2 to produce base.apk and R.java
    print("\n[*] Step 3: Linking resources with aapt2...")
    run_cmd([
        AAPT2, "link",
        "-o", base_apk,
        "-I", PLATFORM_JAR,
        "--manifest", MANIFEST,
        "--min-sdk-version", "21",
        "--target-sdk-version", "34",
        "--version-code", "1",
        "--version-name", "1.0",
        "--java", gen_dir,
        compiled_res,
        "--auto-add-overlay"
    ])
    
    # Step 4: Compile Java sources with javac
    print("\n[*] Step 4: Compiling Java sources with javac...")
    java_files = []
    for root, _, files in os.walk(JAVA_SRC_DIR):
        for f in files:
            if f.endswith(".java"):
                java_files.append(os.path.join(root, f))
    for root, _, files in os.walk(gen_dir):
        for f in files:
            if f.endswith(".java"):
                java_files.append(os.path.join(root, f))
                
    javac_cmd = [
        "javac",
        "-target", "1.8",
        "-source", "1.8",
        "-d", classes_dir,
        "-cp", PLATFORM_JAR
    ] + java_files
    run_cmd(javac_cmd)
    
    # Step 5: Convert .class files to classes.dex with D8
    print("\n[*] Step 5: Generating classes.dex with D8...")
    class_files = []
    for root, _, files in os.walk(classes_dir):
        for f in files:
            if f.endswith(".class"):
                class_files.append(os.path.join(root, f))
    run_cmd([D8, "--lib", PLATFORM_JAR, "--output", BUILD_DIR] + class_files)
    
    # Step 6: Package APK with classes.dex and native libraries
    print("\n[*] Step 6: Packaging APK...")
    shutil.copy2(base_apk, unaligned_apk)
    
    with zipfile.ZipFile(unaligned_apk, 'a') as zf:
        dex_path = os.path.join(BUILD_DIR, "classes.dex")
        zf.write(dex_path, "classes.dex")
        
        arm64_so = os.path.join(LIBS_DIR, "arm64-v8a", "libModMenu.so")
        armv7_so = os.path.join(LIBS_DIR, "armeabi-v7a", "libModMenu.so")
        if os.path.isfile(arm64_so):
            zf.write(arm64_so, "lib/arm64-v8a/libModMenu.so")
            print(f"[+] Added lib/arm64-v8a/libModMenu.so ({os.path.getsize(arm64_so)} bytes)")
        if os.path.isfile(armv7_so):
            zf.write(armv7_so, "lib/armeabi-v7a/libModMenu.so")
            print(f"[+] Added lib/armeabi-v7a/libModMenu.so ({os.path.getsize(armv7_so)} bytes)")
        
    # Step 7: Zipalign APK
    print("\n[*] Step 7: Aligning APK with zipalign...")
    run_cmd([ZIPALIGN, "-p", "-f", "-v", "4", unaligned_apk, aligned_apk])
    
    # Step 8: Generate debug keystore and sign with apksigner
    print("\n[*] Step 8: Signing APK with apksigner...")
    if not os.path.isfile(keystore):
        keytool_cmd = [
            "keytool", "-genkey", "-v",
            "-keystore", keystore,
            "-alias", "androiddebugkey",
            "-storepass", "android",
            "-keypass", "android",
            "-keyalg", "RSA",
            "-keysize", "2048",
            "-validity", "10000",
            "-dname", "CN=Android Debug,O=Android,C=US"
        ]
        run_cmd(keytool_cmd)
        
    run_cmd([
        APKSIGNER, "sign",
        "--ks", keystore,
        "--ks-pass", "pass:android",
        "--key-pass", "pass:android",
        "--ks-key-alias", "androiddebugkey",
        "--out", final_apk,
        aligned_apk
    ])
    
    # Step 9: Verify signature
    print("\n[*] Step 9: Verifying signature...")
    run_cmd([APKSIGNER, "verify", "-v", final_apk])
    
    print("\n" + "="*60)
    print(f"  BUILD SUCCESSFUL!")
    print(f"  Output APK: {final_apk}")
    print(f"  Size: {os.path.getsize(final_apk):,} bytes")
    print("="*60 + "\n")

if __name__ == "__main__":
    main()
