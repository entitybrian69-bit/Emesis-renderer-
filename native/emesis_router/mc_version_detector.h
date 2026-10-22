#ifndef EMESIS_MC_VERSION_DETECTOR_H
#define EMESIS_MC_VERSION_DETECTOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMESIS_ENGINE_LEGACY = 0, /* Minecraft <= 1.16.5 (OpenGL 2.1 / Compatibility pipeline) */
    EMESIS_ENGINE_MODERN = 1  /* Minecraft >  1.16.5 (1.17+, OpenGL 3.2+ Core pipeline)    */
} emesis_engine_t;

typedef struct {
    emesis_engine_t engine;
    char detected_version[64];
    char detection_source[96];
    int world_version;
} emesis_route_decision_t;

/**
 * Evaluates a raw Minecraft version string (e.g. "1.8.9", "1.12.2", "1.16.5",
 * "1.16.5-OptiFine_HD_U_G8", "1.17.1", "1.20.4", "1.21.4", "26.1", "20w51a",
 * "21w19a", "b1.7.3", "rd-132211") and returns the selected engine.
 * Returns -1 if the string cannot be recognized as a Minecraft version.
 */
int emesis_classify_mc_version_string(const char* version_str);

/**
 * Classifies a Minecraft Anvil/DataVersion integer (`world_version` in version.json
 * or `version:` in options.txt).
 * Minecraft 1.16.5 = 2586; last 1.16-era snapshot < 1.17 GL3.2 switch = 2697;
 * Minecraft 1.17 = 2724.
 */
emesis_engine_t emesis_classify_world_version(int world_version);

/**
 * Performs full multi-signal runtime detection:
 *   1. /sdcard/EmesisRenderer/config.json ("routingPolicy") & EMESIS_FORCE_CORE env
 *   2. Active JVM system properties via JNI_GetCreatedJavaVMs
 *   3. Client JAR version.json inspection (world_version / id)
 *   4. /proc/self/cmdline (--version, --fml.mcVersion, gameDir <ver>.json / options.txt)
 *   5. JAVA_HOME/release & Java runtime version fallback (Java 8 => <=1.16.5; Java 16+ => >1.16.5)
 */
void emesis_detect_runtime_route(emesis_route_decision_t* out);

#ifdef __cplusplus
}
#endif

#endif /* EMESIS_MC_VERSION_DETECTOR_H */
