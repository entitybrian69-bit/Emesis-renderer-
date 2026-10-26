#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY_BIN="${PY_BIN:-/tmp/toolchain/venv/bin/python}"
JAVA_BIN="${JAVA_BIN:-$($PY_BIN -c 'import jdk4py; print(jdk4py.JAVA)')}"
KEYTOOL_BIN="${KEYTOOL_BIN:-$($PY_BIN -c 'import jdk4py; print(jdk4py.JAVA.parent / "keytool")')}"
AAPT2_BIN="${AAPT2_BIN:-/tmp/toolchain/npm_tools/aaptjs3-2.0.2/bin/x64/linux/aapt2}"
ANDROID_JAR="${ANDROID_JAR:-/tmp/toolchain/android-platforms/android-34/android.jar}"
ECJ_JAR="${ECJ_JAR:-/tmp/toolchain/ecj.jar}"
D8_JAR="${D8_JAR:-/tmp/toolchain/d8.jar}"
APKSIGNER_JAR="${APKSIGNER_JAR:-/tmp/toolchain/npm_tools/postar-apktool-node-0.3.4/lib/apksigner.jar}"
JNILIBS_DIR="${EMESIS_JNILIBS_OUT:-/tmp/emesis_build/jniLibs}"
OUT_APK="${ROOT_DIR}/EmesisRenderer-Universal-v1.2.4.apk"

WORK_DIR="/tmp/emesis_apk_build"
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}/compiled_res" "${WORK_DIR}/gen" "${WORK_DIR}/classes" "${WORK_DIR}/dex"

echo "=== [0/6] Generating Android launcher icons & hero logo ==="
"${PY_BIN}" "${ROOT_DIR}/tools/generate_android_icons.py"

echo "=== [1/6] Compiling Android resources with aapt2 ==="
"${AAPT2_BIN}" compile --dir "${ROOT_DIR}/app/src/main/res" -o "${WORK_DIR}/compiled_res/res.zip"

echo "=== [2/6] Preparing standalone aapt2 manifest & linking base APK ==="
ROOT_ENV="${ROOT_DIR}" WORK_ENV="${WORK_DIR}" "${PY_BIN}" - << 'PY'
import os, pathlib

src = pathlib.Path(os.environ["ROOT_ENV"]) / "app/src/main/AndroidManifest.xml"
dst = pathlib.Path(os.environ["WORK_ENV"]) / "AndroidManifest.xml"
xml = src.read_text(encoding="utf-8")

needle = '<manifest xmlns:android="http://schemas.android.com/apk/res/android">'
replacement = (
    '<manifest xmlns:android="http://schemas.android.com/apk/res/android"\n'
    '    package="com.entitybrian.emesis.renderer"\n'
    '    android:versionCode="124"\n'
    '    android:versionName="1.2.4">\n\n'
    '    <uses-sdk android:minSdkVersion="21" android:targetSdkVersion="34" />'
)
assert needle in xml, "Expected manifest root tag not found"
dst.write_text(xml.replace(needle, replacement, 1), encoding="utf-8")
PY

"${AAPT2_BIN}" link \
    -o "${WORK_DIR}/base.apk" \
    -I "${ANDROID_JAR}" \
    --manifest "${WORK_DIR}/AndroidManifest.xml" \
    --custom-package com.entitybrian.emesis \
    --java "${WORK_DIR}/gen" \
    "${WORK_DIR}/compiled_res/res.zip"

echo "=== [3/6] Compiling Java sources ==="
JAVA_SOURCES=$(find "${WORK_DIR}/gen" "${ROOT_DIR}/app/src/main/java" -name "*.java" | sort)
"${JAVA_BIN}" -cp "${ECJ_JAR}" org.eclipse.jdt.internal.compiler.batch.Main \
    -source 1.7 -target 1.7 \
    -bootclasspath "${ANDROID_JAR}" \
    -d "${WORK_DIR}/classes" \
    ${JAVA_SOURCES}

echo "=== [4/6] Compiling DEX bytecode with Google D8 (min-api 21) ==="
CLASS_FILES=$(find "${WORK_DIR}/classes" -name "*.class" | sort)
test -f "${D8_JAR}"
"${JAVA_BIN}" -cp "${D8_JAR}" com.android.tools.r8.D8 \
    --release \
    --min-api 21 \
    --lib "${ANDROID_JAR}" \
    --output "${WORK_DIR}/dex" \
    ${CLASS_FILES}

echo "=== [5/6] Assembling Universal APK (armeabi-v7a, arm64-v8a, x86, x86_64) ==="
JNILIBS_ENV="${JNILIBS_DIR}" WORK_ENV="${WORK_DIR}" "${PY_BIN}" - << 'PY'
import os, pathlib, shutil, zipfile

work = pathlib.Path(os.environ["WORK_ENV"])
jnilibs = pathlib.Path(os.environ["JNILIBS_ENV"])
unaligned = work / "unaligned.apk"
shutil.copy2(work / "base.apk", unaligned)

abis = ["armeabi-v7a", "arm64-v8a", "x86", "x86_64"]
with zipfile.ZipFile(unaligned, "a", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    zf.write(work / "dex" / "classes.dex", "classes.dex")
    for abi in abis:
        abi_dir = jnilibs / abi
        if not abi_dir.exists():
            raise SystemExit(f"Missing ABI directory: {abi_dir}")
        for so in sorted(abi_dir.glob("*.so")):
            zf.write(so, f"lib/{abi}/{so.name}")
PY

"${PY_BIN}" "${ROOT_DIR}/tools/zipalign.py" "${WORK_DIR}/unaligned.apk" "${WORK_DIR}/aligned.apk"

echo "=== [6/6] Signing Universal APK (v1 + v2 + v3 schemes) ==="
KEYSTORE="${ROOT_DIR}/tools/emesis-release.jks"
if [ ! -f "${KEYSTORE}" ]; then
    "${KEYTOOL_BIN}" -genkeypair \
        -keystore "${KEYSTORE}" \
        -storepass "emesis2026" \
        -keypass "emesis2026" \
        -alias "entitybrian" \
        -keyalg RSA \
        -keysize 2048 \
        -sigalg SHA384withRSA \
        -validity 10000 \
        -dname "CN=Entitybrian, OU=Emesis Renderer, O=Entitybrian, L=Global, ST=Global, C=US"
fi

"${JAVA_BIN}" -jar "${APKSIGNER_JAR}" sign \
    --ks "${KEYSTORE}" \
    --ks-key-alias "entitybrian" \
    --ks-pass "pass:emesis2026" \
    --key-pass "pass:emesis2026" \
    --v1-signing-enabled true \
    --v2-signing-enabled true \
    --v3-signing-enabled true \
    --v4-signing-enabled false \
    --out "${OUT_APK}" \
    "${WORK_DIR}/aligned.apk"

rm -f "${OUT_APK}.idsig"

echo "=== Verifying Signed Universal APK ==="
"${JAVA_BIN}" -jar "${APKSIGNER_JAR}" verify --verbose --print-certs "${OUT_APK}"
ls -lh "${OUT_APK}"
