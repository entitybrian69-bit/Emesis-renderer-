<div align="center">
  <img src="assets/emesis_logo.png" alt="Emesis Renderer — Made by Entitybrian" width="280" />

  # Emesis Renderer
  ### **Made by Entitybrian**

  **Universal All-in-One Android Renderer Plugin APK for Minecraft: Java Edition**

  [![Download Universal APK](https://img.shields.io/badge/Download-EmesisRenderer--Universal--v1.0.0.apk-00FF88?style=for-the-badge&logo=android&logoColor=080C0F)](https://github.com/entitybrian69-bit/Emesis-renderer-/raw/arena/fac24748-emesis-renderer/EmesisRenderer-Universal-v1.0.0.apk)
</div>

---

## Direct Download

- **Direct Universal APK Download (32-bit & 64-bit Android):**
  [**`EmesisRenderer-Universal-v1.0.0.apk`**](https://github.com/entitybrian69-bit/Emesis-renderer-/raw/arena/fac24748-emesis-renderer/EmesisRenderer-Universal-v1.0.0.apk)
- **GitHub File Viewer Page:**
  [https://github.com/entitybrian69-bit/Emesis-renderer-/blob/arena/fac24748-emesis-renderer/EmesisRenderer-Universal-v1.0.0.apk](https://github.com/entitybrian69-bit/Emesis-renderer-/blob/arena/fac24748-emesis-renderer/EmesisRenderer-Universal-v1.0.0.apk)

---

## Overview

**Emesis Renderer** is a unified, zero-configuration Android launcher renderer plugin APK created by **Entitybrian**. Instead of switching between separate renderer plugins for older and modern Minecraft releases, **Emesis Renderer** bundles two isolated native OpenGL-over-GLES rendering pipelines behind a single intelligent runtime version router (`libemesis_renderer.so`):

| Minecraft Version Range | Active Native Pipeline | Graphics Profile |
| :--- | :--- | :--- |
| **Minecraft `> 1.16.5`** *(1.17 – 1.21.x, 26.x+, snapshots `>= 21w10a`)* | **Emesis Modern Core** (`libemesis_modern.so`) | Desktop OpenGL 3.2 – 4.6 Core Profile over OpenGL ES 3.x |
| **Minecraft `<= 1.16.5`** *(Classic, Beta, 1.7.10, 1.8.9, 1.12.2, 1.16.5)* | **Emesis Compatibility Core** (`libemesis_legacy.so`) | Desktop OpenGL 2.1 / 3.x Compatibility + GLSL/SPIR-V Shader Compiler with 32-bit GLES 2.0/3.x fallback |

---

## Universal Device Architecture Support (Including 32-bit Android)

A single **`EmesisRenderer-Universal-v1.0.0.apk`** supports Android 5.0+ (`minSdkVersion 21`) through Android 15/16+ (`targetSdkVersion 34`, 16KB page-aligned ELF segments) across **all four Android ABIs**:

- `armeabi-v7a` — **32-bit ARM** smartphones & tablets
- `arm64-v8a` — **64-bit ARM** smartphones & tablets
- `x86` — **32-bit Intel/AMD** Chromebooks, tablets & Android emulators
- `x86_64` — **64-bit Intel/AMD** Chromebooks, handheld PCs & Android emulators

---

## Supported Minecraft: Java Edition Launchers

Emesis Renderer implements the native Android renderer plugin contract (`fclPlugin`, `zalithRendererPlugin`, `pgwRendererPlugin`, `boatEnv`, `pojavEnv`, `extractNativeLibs="true"`) and is automatically discovered by:

- **FoldCraftLauncher (FCL)** & FCL forks
- **ZalithLauncher 2** & **ZalithLauncher 1** (and forks)
- **PojavLauncher Glow Worm (PGW)** & plugin-enabled Pojav forks

### Installation & Usage

1. Download and install [**`EmesisRenderer-Universal-v1.0.0.apk`**](https://github.com/entitybrian69-bit/Emesis-renderer-/raw/arena/fac24748-emesis-renderer/EmesisRenderer-Universal-v1.0.0.apk) on your Android device.
2. Open your launcher's **Renderer Settings** and select **`Emesis Renderer (All Versions — Made by Entitybrian)`**.
3. Launch **any** Minecraft version (`<= 1.16.5` or `> 1.16.5`) — Emesis Renderer detects the version automatically at startup and activates the matching rendering core.
4. *(Optional)* Open the **Emesis Renderer** app from your app drawer to inspect device ABI status, test version routing, or set a manual routing policy in `/sdcard/EmesisRenderer/config.json`.
