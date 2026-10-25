# Emesis Renderer

**Emesis** is a high-performance Minecraft: Java Edition renderer engineered specifically for Android launchers. It is designed to deliver strong frame rates and low frame-time variance on mobile SoCs while remaining lightweight and launcher-friendly.

## Key Features

- **Shaders support across versions** — Full shader compatibility for Minecraft versions **below 1.17 and 1.17+**, covering both the legacy and modern render pipelines.
- **Performance-first architecture** — Optimized rendering path focused on mobile constraints (bandwidth, thermal limits, and GPU efficiency).
- **Launcher integration** — Built to work cleanly with Android Minecraft launchers that support custom renderers (including Aerix and Pojav-based stacks).
- **Broad version coverage** — Targets both modern (1.17+) and legacy (1.8–1.16.5) Minecraft clients.

## Roadmap

Future optimization and performance improvements are actively in progress. Planned work includes:

- Further GPU and driver-level tuning for common mobile SoCs
- Reduced CPU overhead and improved batching
- Additional shader pipeline refinements
- Better thermal and power-aware performance scaling
- Expanded compatibility testing across devices and Minecraft versions

## Status

Early development. The core renderer implementation is not yet publicly available. This repository currently serves as the public project home.

## Related Projects

- [Aerix Launcher](https://github.com/entitybrian69-bit/Aerix-launcher) — Minecraft: Java Edition launcher for Android with advanced renderer selection
- [aetherium](https://github.com/entitybrian69-bit/aetherium) — Renderer and FPS-focused mod

## License

TBD

---

*Emesis is developed and maintained by entitybrian.*
