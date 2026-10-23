#!/usr/bin/env bash
set -euo pipefail

ZIG_BIN="${ZIG_BIN:-$(/tmp/toolchain/venv/bin/python -c 'import ziglang, pathlib; print(pathlib.Path(ziglang.__file__).parent / "zig")')}"
NDK_HEADERS="${NDK_HEADERS:-/tmp/toolchain/ndk-headers}"
NDK_PLATFORMS="${NDK_PLATFORMS:-/tmp/toolchain/ndk-full/platforms/android-21}"
WRAP_DIR="/tmp/emesis_zig_wrappers"

mkdir -p "$WRAP_DIR"

if [ -f "${NDK_HEADERS}/android/legacy_stdlib_inlines.h" ] && ! grep -q "EMESIS_API21_STRTOD_L" "${NDK_HEADERS}/android/legacy_stdlib_inlines.h"; then
    cat >> "${NDK_HEADERS}/android/legacy_stdlib_inlines.h" <<'EOF'

/* EMESIS_API21_STRTOD_L: Android < 26 static inline shims for libc++ */
#if defined(__ANDROID_API__) && (__ANDROID_API__ < 26)
#include <stdlib.h>
#include <xlocale.h>
__BEGIN_DECLS
static __inline double strtod_l(const char* __s, char** __end_ptr, locale_t __l) {
    (void)__l;
    return strtod(__s, __end_ptr);
}
static __inline float strtof_l(const char* __s, char** __end_ptr, locale_t __l) {
    (void)__l;
    return strtof(__s, __end_ptr);
}
static __inline long strtol_l(const char* __s, char** __end_ptr, int __base, locale_t __l) {
    (void)__l;
    return strtol(__s, __end_ptr, __base);
}
__END_DECLS
#endif
EOF
fi

cat > "$WRAP_DIR/bionic_api21_compat.h" <<'EOF'
#ifndef EMESIS_BIONIC_API21_COMPAT_H
#define EMESIS_BIONIC_API21_COMPAT_H
#if defined(__ANDROID__) && (__ANDROID_API__ < 26)
#include <stdlib.h>
#include <xlocale.h>
#ifdef __cplusplus
extern "C" {
#endif
static __inline double strtod_l(const char* __s, char** __end_ptr, locale_t __l) {
    (void)__l;
    return strtod(__s, __end_ptr);
}
static __inline float strtof_l(const char* __s, char** __end_ptr, locale_t __l) {
    (void)__l;
    return strtof(__s, __end_ptr);
}
static __inline long strtol_l(const char* __s, char** __end_ptr, int __base, locale_t __l) {
    (void)__l;
    return strtol(__s, __end_ptr, __base);
}
#ifdef __cplusplus
}
#endif
#endif
#endif
EOF

make_abi_wrappers() {
    local abi="$1"
    local zig_target="$2"
    local asm_triple="$3"
    local sys_libdir="$4"
    local extra_flags="$5"

    cat > "$WRAP_DIR/libc_${abi}.txt" <<EOF
include_dir=${NDK_HEADERS}
sys_include_dir=${NDK_HEADERS}/${asm_triple}
crt_dir=${sys_libdir}
msvc_lib_dir=
kernel32_lib_dir=
gcc_dir=${sys_libdir}
darwin_sdk_dir=
EOF

    cat > "$WRAP_DIR/${abi}-cc" <<EOF
#!/usr/bin/env bash
export ZIG_LIBC="${WRAP_DIR}/libc_${abi}.txt"
exec "${ZIG_BIN}" cc -target ${zig_target} \\
    -isystem "${NDK_HEADERS}" \\
    -isystem "${NDK_HEADERS}/${asm_triple}" \\
    -L"${sys_libdir}" \\
    -fno-sanitize=all -Wno-date-time ${extra_flags} "\$@"
EOF

    cat > "$WRAP_DIR/${abi}-cxx" <<EOF
#!/usr/bin/env bash
export ZIG_LIBC="${WRAP_DIR}/libc_${abi}.txt"
extra_tail=""
case "\$*" in
    *SPIRV/doc.cpp*) extra_tail="-O0" ;;
esac
exec "${ZIG_BIN}" c++ -target ${zig_target} \\
    -isystem "${NDK_HEADERS}" \\
    -isystem "${NDK_HEADERS}/${asm_triple}" \\
    -L"${sys_libdir}" \\
    -fno-sanitize=all -Wno-date-time ${extra_flags} "\$@" \${extra_tail}
EOF

    cat > "$WRAP_DIR/${abi}-ar" <<EOF
#!/usr/bin/env bash
exec "${ZIG_BIN}" ar "\$@"
EOF

    cat > "$WRAP_DIR/${abi}-ranlib" <<EOF
#!/usr/bin/env bash
exec "${ZIG_BIN}" ranlib "\$@"
EOF

    chmod +x "$WRAP_DIR/${abi}-cc" "$WRAP_DIR/${abi}-cxx" "$WRAP_DIR/${abi}-ar" "$WRAP_DIR/${abi}-ranlib"
}

make_abi_wrappers "armeabi-v7a" "arm-linux-androideabi.21" "arm-linux-androideabi" "${NDK_PLATFORMS}/arch-arm/usr/lib" "-mcpu=generic+v7a -mfloat-abi=softfp"
make_abi_wrappers "arm64-v8a"   "aarch64-linux-android.21" "aarch64-linux-android" "${NDK_PLATFORMS}/arch-arm64/usr/lib" ""
make_abi_wrappers "x86"         "x86-linux-android.21"     "i686-linux-android"    "${NDK_PLATFORMS}/arch-x86/usr/lib" "-msse2 -mfpmath=sse"
make_abi_wrappers "x86_64"      "x86_64-linux-android.21"  "x86_64-linux-android"  "${NDK_PLATFORMS}/arch-x86_64/usr/lib64" "-msse4.2 -mpopcnt"

echo "Zig Android API-21 wrappers generated in ${WRAP_DIR}"
