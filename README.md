<div align="center">
  <img src="assets/emesis_logo.png" alt="Emesis Renderer — Made by Entitybrian" width="280" />

  # Emesis Renderer
  ### **Made by Entitybrian**

  **Universal All-in-One Android Renderer Plugin APK for All Pojav Variants & Minecraft: Java Edition**

  [![Download Universal APK](https://img.shields.io/badge/Download-EmesisRenderer--Universal--v1.2.5.apk-00FF88?style=for-the-badge&logo=android&logoColor=080C0F)](./EmesisRenderer-Universal-v1.2.5.apk)
</div>

---

## Latest Build

- **Universal APK (`v1.2.5` — 32-bit & 64-bit Android):**
  **`EmesisRenderer-Universal-v1.2.5.apk`**

---

## Overview

**Emesis Renderer** is a universal all-in-one Android launcher renderer plugin APK created by **Entitybrian**, engineered for full compatibility across **all variants of PojavLauncher** and its forks/continuations. It bundles two isolated native OpenGL-over-GLES rendering pipelines behind a single runtime version router (`libemesis_renderer.so`):

| Minecraft Version Range | Active Native Pipeline | Graphics Profile |
| :--- | :--- | :--- |
| **Minecraft `> 1.16.5`** *(1.17 – 1.21.x, 26.x+, snapshots `>= 21w10a`)* | **Emesis Modern Core** (`libemesis_modern.so`) | Desktop OpenGL 3.2 – 4.6 Core Profile over OpenGL ES 3.x (automatic GLES 3.2 / 3.1 / 3.0 context promotion) |
| **Minecraft `<= 1.16.5`** *(Classic, Beta, 1.6.4, 1.7.10, 1.8.9, 1.12.2, 1.16.5)* | **Emesis Compatibility Core** (`libemesis_legacy.so`) | High-Throughput GLES 3.2/3.1/3.0 + SPIR-V ESSL Shader Accelerator (persistent `.ec1`/`.ep1` shader & GPU binary cache, 16-target MRT FBO state-diffing, persistent GPU `GL_QUADS` IBO, hardware PCF shadows & HDR float filtering, with 32-bit GLES 2.0 fallback) |

---

## High-Efficiency Shader Acceleration for All Versions Below 1.17 (`<= 1.16.5`)

Emesis Compatibility Core (`libemesis_legacy.so`) is purpose-engineered for stutter-free compilation and maximum GPU/CPU frame rates across **Sodium**, **Iris**, **ShadersMod**, **OptiFine**, **Angelica**, and **Oculus** on Minecraft `1.6.4`, `1.7.10`, `1.8.9`, `1.12.2`, `1.14.4`, `1.15.2`, and `1.16.5`:

- **Two-Tier Persistent Shader Translation Cache (`.ec1`) + GPU Program Binary Cache (`.ep1`):**
  - Thread-safe 128-bit keyed RAM cache + atomic Scoped-Storage-aware disk cache (`POJAV_GAME_DIR`, `MC_DIR`, game `cwd`, `HOME`, `TMPDIR`) eliminates repeated GLSL-to-SPIR-V-to-ESSL recompilation (**~1,500x faster** RAM hits, **>140x faster** disk hits).
  - Core ES 3.0 + `OES_get_program_binary` `.ep1` GPU binary caching (`GL_PROGRAM_BINARY_RETRIEVABLE_HINT`) with in-RAM `.ep1` deduplication reloads compiled GPU machine ISA directly via `glProgramBinary` across fallback program tables and world reloads, plus post-link shader compiler memory reclamation (`glDetachShader` + `glReleaseShaderCompiler`) and persistent `.emesis_fpe.psa` Fixed-Function archives.
- **Full-Spectrum `< 1.17` GLSL Dialect Translation (`#version 110` – `#version 460 compatibility`) & Custom Uniform Synthesis:**
  - Translates Desktop GLSL `110`/`120`/`130`/`140`/`150`/`330+ compatibility` (`gl_Vertex`, `gl_MultiTexCoord*`, `gl_ModelViewProjectionMatrix`, `gl_TexCoord[]`, `gl_Fog`, `ftransform()`, `GL_EXT_gpu_shader4` `texelFetch2D`/`textureSize2D`/bitwise ops, `GL_ARB_shader_texture_lod` `texture2DLod`/`texture2DGradARB`/`shadow2DProj`, uniform default initializers, and mid-file `#extension` directives) into native ESSL `320 es` / `310 es` / `300 es` via Khronos `glslang` + `SPIRV-Cross` while preserving fragment MRT `layout(location = N)` outputs, stripping internal `_gl4es_*` uniforms from reflection, and automatically synthesizing `shaders.properties` custom uniforms (`exposureLevel`, `dayStrength`/`nightStrength`, `daytime`, `resolution`, `viewSize`/`pixelSize`, `sunDir`/`lightDir`, `taaOffset`/`taaJitter`) when omitted by older `< 1.17` shader loaders.
  - Native `#version 300 es` / `310 es` / `320 es` shaders take a zero-overhead fast path without destructive ES 2.0 int-to-float coercion.
  - O(1) `khash` uniform name lookup (`glGetUniformLocation`), deferred per-draw dirty uniform flushing with generation-gated zero-ping-pong `GL_ALPHA_TEST` cutout sync, heap-backed arbitrary-length uniform initializer parsing, and `glBindFragDataLocation` support.
- **Tile-Based Mobile GPU (Adreno / Mali / PowerVR / Xclipse) Draw & MRT Optimizations:**
  - **16-Target MRT, Per-Attachment Indexed Blending (`GL_OES`/`EXT_draw_buffers_indexed`), & Mip-Level FBO State-Diffing:** Supports per-buffer `glEnablei`/`glDisablei`/`glColorMaski`/`glBlendFunci`/`glBlendFuncSeparatei`/`glBlendEquationi`/`glBlendEquationSeparatei`, eliminates redundant `glFramebufferTexture2D` and `glDrawBuffers` driver calls across composite/deferred G-buffer passes while properly rebinding bloom/auto-exposure mipmap levels (`glGenerateMipmap` + `level > 0`), accelerates `glCopyImageSubData` via scissor-safe cached-FBO `glBlitFramebuffer` on TBDR GPUs, and skips synchronous `glFinish()` pipeline stalls on ES 3.x.
  - **Persistent GPU `GL_QUADS` Index Buffers (`uint16` & `uint32`):** Converts Minecraft chunk, particle, and fullscreen composite `GL_QUADS` draws on the GPU without per-draw CPU index regeneration or client-memory streaming.
  - **Hardware PCF Shadow Maps & HDR Linear Float Buffers:** Enables native hardware `GL_COMPARE_REF_TO_TEXTURE` PCF `sampler2DShadow` filtering, `RGBA16F`/`R11F_G11F_B10F`/`RGBA32F` linear float render-target filtering (`GL_OES_texture_float_linear`), and automatic GLES `3.2 -> 3.1 -> 3.0 -> 2.0` context promotion.

---

## Universal Device Architecture Support (Including 32-bit Android)

A single **`EmesisRenderer-Universal-v1.2.5.apk`** supports Android 5.0+ (`minSdkVersion 21`) through Android 15/16+ (`targetSdkVersion 34`, 16KB page-aligned ELF segments) across **all four Android ABIs**:

- `armeabi-v7a` — **32-bit ARM** smartphones & tablets
- `arm64-v8a` — **64-bit ARM** smartphones & tablets
- `x86` — **32-bit Intel/AMD** Chromebooks, tablets & Android emulators
- `x86_64` — **64-bit Intel/AMD** Chromebooks, handheld PCs & Android emulators

---

## Compatible Across All Pojav Variants

Emesis Renderer implements **every** renderer integration contract across the Pojav ecosystem in a single APK:

1. **RendererPlugin V2 (`fclPlugin_V2`) — Native Plugin + In-Launcher Settings UI**
   - **ZalithLauncher 2** (`com.movtery.zalithlauncher.v2`)
   - **FoldCraftLauncher (FCL)** & modern V2-enabled forks (`com.tungsten.fcl`, `org.fcl.enchantnet`)
2. **RendererPlugin V1 (`pgwRendererPlugin` / `zalithRendererPlugin` / `fclPlugin`) — Zero-Config Auto Discovery**
   - **Pojav Glow Worm (PGW / PojavZH)** (`com.movtery.pojavzh`)
   - **PojavLauncher Beta / Zink** & **Pojav Firefly / Horizon** (`net.kdt.pojavlaunch.beta`, `net.kdt.pojavlaunch.firefly`)
   - **ZalithLauncher 1** (`com.movtery.zalithlauncher`)
   - **Boat-based Pojav/FCL hybrids** (`boatEnv` + `pojavEnv` + `DLOPEN` + full-path `eglName`)
3. **Vanilla Pojav `custom_env.txt` Native Bridge (Built-In One-Tap Generator in App)**
   - **Official PojavLauncher** (`net.kdt.pojavlaunch`, debug & nightly builds)
   - **Amethyst** (`org.angelauramc.amethyst`)
   - **MojoLauncher** & **SolCraftLauncher** (`git.artdeell.mojo`, `git.artdeell.solcraft`)
   - **QuestCraft** (`com.qcxr.qcxr`)

### Installation & Usage

- **On Plugin-Enabled Pojav Variants (Zalith 1/2, Pojav Glow Worm, Pojav Beta-Zink, Pojav Firefly, FCL):**
  1. Install **`EmesisRenderer-Universal-v1.2.5.apk`**.
  2. Open your launcher's **Renderer Settings** and select **`Emesis Renderer v1.2.5 (All Versions — Made by Entitybrian)`**.
  3. Launch any Minecraft version (`<= 1.16.5` or `> 1.16.5`).

- **On Vanilla Official PojavLauncher / Debug / Custom Forks / Amethyst / MojoLauncher / QuestCraft (Android 5 – Android 16 Scoped Storage):**
  1. Install **`EmesisRenderer-Universal-v1.2.5.apk`** and open **Emesis Renderer** from your app drawer (or tap **Open** right from the Android installer dialog).
  2. In the **All Pojav Variants Compatibility** card, tap **Install to Pojav** and tap **Use this folder -> Allow** when PojavLauncher's game folder opens (permission is saved so future updates auto-sync automatically).
  3. In Official PojavLauncher's Video Settings, keep Renderer on default **Holy GL4ES (OpenGL ES 2)** (do **not** select raw OpenGL ES 3), then launch any Minecraft version (`<= 1.16.5` or `> 1.16.5`) — Emesis Renderer automatically intercepts EGL + LWJGL/SDL OpenGL and routes to the matching native core.
