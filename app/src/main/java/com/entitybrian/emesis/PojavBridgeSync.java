package com.entitybrian.emesis;

import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.UriPermission;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class PojavBridgeSync {

    private static final String[] LEGACY_LAUNCHER_DIRS = new String[] {
        "/sdcard/games/PojavLauncher",
        "/sdcard/Android/data/net.kdt.pojavlaunch/files",
        "/sdcard/Android/data/net.kdt.pojavlaunch.debug/files",
        "/sdcard/Android/data/net.kdt.pojavlaunch.canary/files",
        "/sdcard/Android/data/org.angelauramc.amethyst/files",
        "/sdcard/Android/data/org.angelauramc.amethyst.debug/files",
        "/sdcard/Android/data/git.artdeell.mojo/files",
        "/sdcard/Android/data/git.artdeell.mojo.debug/files"
    };

    private static final String[] LEGACY_CUSTOM_ENV_PKGS = new String[] {
        "net.kdt.pojavlaunch",
        "net.kdt.pojavlaunch.debug",
        "net.kdt.pojavlaunch.canary",
        "org.angelauramc.amethyst",
        "org.angelauramc.amethyst.debug",
        "io.stamethyst",
        "git.artdeell.mojo",
        "git.artdeell.mojo.debug",
        "git.artdeell.solcraft",
        "com.soldev.solcraft",
        "com.qcxr.qcxr"
    };

    private PojavBridgeSync() {
    }

    public static String getRouterPath(Context context) {
        String nativeDir = context.getApplicationInfo().nativeLibraryDir;
        return new File(nativeDir, "libemesis_renderer.so").getAbsolutePath();
    }

    public static String buildPojavCustomEnv(String routerPath) {
        return "POJAV_RENDERER=opengles2\n"
             + "POJAVEXEC_EGL=" + routerPath + "\n"
             + "LIBGL_ES=3\n"
             + "LIBGL_GL=31\n"
             + "LIBGL_MIPMAP=3\n"
             + "LIBGL_NORMALIZE=1\n"
             + "LIBGL_NOERROR=1\n"
             + "LIBGL_NOINTOVLHACK=1\n"
             + "LIBGL_USE_MC_COLOR=1\n"
             + "SDL_OPENGL_LIBRARY=" + routerPath + "\n"
             + "SDL_EGL_LIBRARY=" + routerPath + "\n"
             + "_JAVA_OPTIONS=-Dorg.lwjgl.opengl.libname=" + routerPath + " -Dorg.lwjgl.egl.libname=" + routerPath;
    }

    /**
     * Permanent path-free environment for native plugin launchers (Aerix Launcher, Zalith,
     * FoldCraftLauncher, Pojav Glow Worm). Because these launchers dynamically query Emesis's
     * live nativeLibraryDir via PackageManager on every game launch, avoiding hardcoded
     * /data/app/~~.../ paths in custom_env.txt ensures APK updates are 100% automatic forever.
     */
    public static String buildPluginSafeCustomEnv() {
        return "LIBGL_ES=3\n"
             + "LIBGL_GL=31\n"
             + "LIBGL_MIPMAP=3\n"
             + "LIBGL_NORMALIZE=1\n"
             + "LIBGL_NOERROR=1\n"
             + "LIBGL_NOINTOVLHACK=1\n"
             + "LIBGL_USE_MC_COLOR=1";
    }

    public static boolean isAerixPackage(Context context, String pkg) {
        if (context == null || pkg == null || pkg.length() == 0) return false;
        PackageManager pm = context.getPackageManager();
        try {
            PackageInfo pi = pm.getPackageInfo(pkg, 0);
            if (pi == null || pi.applicationInfo == null) return false;
            ApplicationInfo ai = pi.applicationInfo;
            CharSequence labelCs = pm.getApplicationLabel(ai);
            if (labelCs != null) {
                String label = labelCs.toString().toLowerCase(Locale.US);
                if (label.contains("aerix")) {
                    return true;
                }
            }
            try {
                if (pm.getServiceInfo(new ComponentName(pkg, "net.kdt.pojavlaunch.mobileglues.MgQueryService"), 0) != null) {
                    return true;
                }
            } catch (Throwable ignored) {
            }
            try {
                if (pm.getActivityInfo(new ComponentName(pkg, "net.kdt.pojavlaunch.mobileglues.MainActivity"), 0) != null) {
                    return true;
                }
            } catch (Throwable ignored) {
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    public static boolean isNativePluginBridgePackage(Context context, String pkg) {
        if (pkg == null || pkg.length() == 0) return false;
        if (pkg.startsWith("com.movtery.zalithlauncher")
                || pkg.startsWith("com.movtery.pojavzh")
                || pkg.startsWith("com.tungsten.fcl")
                || "com.tungsten.fcm".equals(pkg)
                || "org.fcl.enchantnet".equals(pkg)
                || "net.kdt.pojavlaunch.firefly".equals(pkg)
                || "net.kdt.pojavlaunch.beta".equals(pkg)) {
            return true;
        }
        return isAerixPackage(context, pkg);
    }

    public static String resolvePackageForTreeUri(Context context, Uri treeUri) {
        if (context == null || treeUri == null) return null;
        String auth = treeUri.getAuthority();
        if (auth == null || auth.length() == 0) return null;
        try {
            ProviderInfo pi = context.getPackageManager().resolveContentProvider(auth, 0);
            if (pi != null && pi.packageName != null && pi.packageName.length() > 0) {
                return pi.packageName;
            }
        } catch (Throwable ignored) {
        }
        if (auth.endsWith(".scoped.gamefolder.debug")) {
            return auth.substring(0, auth.length() - ".scoped.gamefolder.debug".length()) + ".debug";
        }
        if (auth.endsWith(".pub.scoped.gamefolder")) {
            return auth.substring(0, auth.length() - ".pub.scoped.gamefolder".length());
        }
        if (auth.endsWith(".scoped.gamefolder")) {
            return auth.substring(0, auth.length() - ".scoped.gamefolder".length());
        }
        return null;
    }

    public static boolean isNativePluginBridgeTree(Context context, Uri treeUri) {
        String pkg = resolvePackageForTreeUri(context, treeUri);
        return isNativePluginBridgePackage(context, pkg);
    }

    public static boolean hasNonPluginLegacyLauncherInstalled(Context context) {
        if (context == null) return false;
        PackageManager pm = context.getPackageManager();
        for (int i = 0; i < LEGACY_CUSTOM_ENV_PKGS.length; i++) {
            String pkg = LEGACY_CUSTOM_ENV_PKGS[i];
            try {
                if (pm.getPackageInfo(pkg, 0) != null && !isNativePluginBridgePackage(context, pkg)) {
                    return true;
                }
            } catch (Throwable ignored) {
            }
        }
        return false;
    }

    public static boolean hasOnlyNativePluginLaunchersInstalled(Context context) {
        if (context == null) return false;
        if (hasNonPluginLegacyLauncherInstalled(context)) return false;
        PackageManager pm = context.getPackageManager();
        String[] checkPkgs = new String[] {
            "net.kdt.pojavlaunch",
            "net.kdt.pojavlaunch.debug",
            "net.kdt.pojavlaunch.firefly",
            "net.kdt.pojavlaunch.beta",
            "com.movtery.pojavzh",
            "com.movtery.pojavzh.debug",
            "com.movtery.zalithlauncher",
            "com.movtery.zalithlauncher.debug",
            "com.movtery.zalithlauncher.v2",
            "com.movtery.zalithlauncher.v2.debug",
            "com.tungsten.fcl",
            "com.tungsten.fcm",
            "com.tungsten.fcl.ngg",
            "org.fcl.enchantnet"
        };
        for (int i = 0; i < checkPkgs.length; i++) {
            try {
                if (pm.getPackageInfo(checkPkgs[i], 0) != null
                        && isNativePluginBridgePackage(context, checkPkgs[i])) {
                    return true;
                }
            } catch (Throwable ignored) {
            }
        }
        return false;
    }

    public static String buildRecommendedEnvForDevice(Context context) {
        if (hasOnlyNativePluginLaunchersInstalled(context)) {
            return buildPluginSafeCustomEnv();
        }
        return buildPojavCustomEnv(getRouterPath(context));
    }

    public static boolean hasPersistedSafGrants(Context context) {
        if (Build.VERSION.SDK_INT < 21 || context == null) return false;
        try {
            List<UriPermission> perms = context.getContentResolver().getPersistedUriPermissions();
            if (perms == null) return false;
            for (int i = 0; i < perms.size(); i++) {
                UriPermission perm = perms.get(i);
                if (perm != null && perm.isWritePermission() && perm.getUri() != null) {
                    return true;
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    public static List<String> syncAllLauncherConfigs(Context context) {
        String routerPath = getRouterPath(context);
        String legacyContent = buildPojavCustomEnv(routerPath);
        List<String> updated = new ArrayList<String>();
        updated.addAll(syncPersistedSafTrees(context, legacyContent));
        updated.addAll(writeDirectLegacyLauncherDirs(context, legacyContent));
        return updated;
    }

    public static List<String> syncPersistedSafTrees(Context context, String fallbackLegacyContent) {
        List<String> synced = new ArrayList<String>();
        if (Build.VERSION.SDK_INT < 21 || context == null) return synced;
        try {
            ContentResolver cr = context.getContentResolver();
            List<UriPermission> perms = cr.getPersistedUriPermissions();
            if (perms == null) return synced;
            for (int i = 0; i < perms.size(); i++) {
                UriPermission perm = perms.get(i);
                if (perm != null && perm.isWritePermission() && perm.getUri() != null) {
                    Uri treeUri = perm.getUri();
                    String content = isNativePluginBridgeTree(context, treeUri)
                            ? buildPluginSafeCustomEnv()
                            : (fallbackLegacyContent != null
                                ? fallbackLegacyContent
                                : buildPojavCustomEnv(getRouterPath(context)));
                    String saved = writeCustomEnvToTreeUri(context, treeUri, content);
                    if (saved != null) {
                        synced.add(saved);
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return synced;
    }

    public static List<String> writeDirectLegacyLauncherDirs(Context context, String fallbackLegacyContent) {
        List<String> written = new ArrayList<String>();
        for (int i = 0; i < LEGACY_LAUNCHER_DIRS.length; i++) {
            try {
                String dirPath = LEGACY_LAUNCHER_DIRS[i];
                File dir = new File(dirPath);
                if (dir.exists() && dir.isDirectory() && dir.canWrite()) {
                    boolean pluginSafe = false;
                    if (dirPath.contains("/net.kdt.pojavlaunch/")) {
                        pluginSafe = isNativePluginBridgePackage(context, "net.kdt.pojavlaunch");
                    } else if (dirPath.contains("/net.kdt.pojavlaunch.debug/")) {
                        pluginSafe = isNativePluginBridgePackage(context, "net.kdt.pojavlaunch.debug");
                    }
                    String content = pluginSafe
                            ? buildPluginSafeCustomEnv()
                            : (fallbackLegacyContent != null
                                ? fallbackLegacyContent
                                : buildPojavCustomEnv(getRouterPath(context)));
                    byte[] bytes = content.getBytes("UTF-8");
                    File out = new File(dir, "custom_env.txt");
                    FileOutputStream fos = new FileOutputStream(out, false);
                    fos.write(bytes);
                    fos.flush();
                    fos.close();
                    if (out.exists() && out.length() == bytes.length) {
                        written.add(out.getAbsolutePath());
                    }
                }
            } catch (Throwable ignored) {
            }
        }
        return written;
    }

    public static String writeCustomEnvForLauncherTree(Context context, Uri treeUri, String fallbackLegacyContent) {
        if (context == null || treeUri == null) return null;
        String content = isNativePluginBridgeTree(context, treeUri)
                ? buildPluginSafeCustomEnv()
                : (fallbackLegacyContent != null
                    ? fallbackLegacyContent
                    : buildPojavCustomEnv(getRouterPath(context)));
        return writeCustomEnvToTreeUri(context, treeUri, content);
    }

    public static String writeCustomEnvToTreeUri(Context context, Uri treeUri, String content) {
        if (Build.VERSION.SDK_INT < 21 || context == null || treeUri == null || content == null) return null;
        ContentResolver cr = context.getContentResolver();
        Cursor cursor = null;
        try {
            String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri parentDocUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeDocId);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeDocId);

            Uri targetDocUri = null;
            cursor = cr.query(
                    childrenUri,
                    new String[] {
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME
                    },
                    null,
                    null,
                    null);
            if (cursor != null) {
                while (cursor.moveToNext()) {
                    String docId = cursor.getString(0);
                    String name = cursor.getString(1);
                    if ("custom_env.txt".equals(name) && docId != null) {
                        targetDocUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
                        break;
                    }
                }
            }
            if (cursor != null) {
                cursor.close();
                cursor = null;
            }

            if (targetDocUri == null) {
                targetDocUri = DocumentsContract.createDocument(cr, parentDocUri, "text/plain", "custom_env.txt");
            }
            if (targetDocUri == null) return null;

            byte[] bytes = content.getBytes("UTF-8");
            boolean wrote = false;
            try {
                ParcelFileDescriptor pfd = cr.openFileDescriptor(targetDocUri, "rwt");
                if (pfd == null) {
                    pfd = cr.openFileDescriptor(targetDocUri, "wt");
                }
                if (pfd != null) {
                    FileOutputStream fos = new FileOutputStream(pfd.getFileDescriptor());
                    try {
                        fos.getChannel().truncate(0);
                    } catch (Throwable ignored) {
                    }
                    fos.write(bytes);
                    fos.flush();
                    fos.close();
                    pfd.close();
                    wrote = true;
                }
            } catch (Throwable ignored) {
            }

            if (!wrote) {
                OutputStream os = cr.openOutputStream(targetDocUri, "wt");
                if (os == null) {
                    os = cr.openOutputStream(targetDocUri);
                }
                if (os == null) return null;
                os.write(bytes);
                os.flush();
                os.close();
            }

            String authority = treeUri.getAuthority() != null ? treeUri.getAuthority() : "SAF";
            return authority + " :: " + treeDocId + "/custom_env.txt";
        } catch (Throwable t) {
            return null;
        } finally {
            if (cursor != null) {
                try {
                    cursor.close();
                } catch (Throwable ignored) {
                }
            }
        }
    }
}
