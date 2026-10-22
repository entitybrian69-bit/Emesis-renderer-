package com.entitybrian.emesis;

public final class EmesisNative {
    private static boolean loaded = false;
    private static String loadError = null;

    static {
        try {
            System.loadLibrary("emesis_renderer");
            loaded = true;
        } catch (Throwable t) {
            loaded = false;
            loadError = t.getMessage();
        }
    }

    private EmesisNative() {}

    public static boolean isLoaded() {
        return loaded;
    }

    public static String getLoadError() {
        return loadError;
    }

    public static native String getEngineVersion();

    public static native String simulateRoute(String mcVersion);
}
