package com.entitybrian.emesis;

import android.content.ContentResolver;
import android.content.Context;
import android.content.UriPermission;
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
             + "_JAVA_OPTIONS=-Dorg.lwjgl.opengl.libname=" + routerPath;
    }

    public static List<String> syncAllLauncherConfigs(Context context) {
        String routerPath = getRouterPath(context);
        String content = buildPojavCustomEnv(routerPath);
        List<String> updated = new ArrayList<String>();
        updated.addAll(syncPersistedSafTrees(context, content));
        updated.addAll(writeDirectLegacyLauncherDirs(content));
        return updated;
    }

    public static List<String> syncPersistedSafTrees(Context context, String content) {
        List<String> synced = new ArrayList<String>();
        if (Build.VERSION.SDK_INT < 21 || context == null || content == null) return synced;
        try {
            ContentResolver cr = context.getContentResolver();
            List<UriPermission> perms = cr.getPersistedUriPermissions();
            if (perms == null) return synced;
            for (int i = 0; i < perms.size(); i++) {
                UriPermission perm = perms.get(i);
                if (perm != null && perm.isWritePermission() && perm.getUri() != null) {
                    String saved = writeCustomEnvToTreeUri(context, perm.getUri(), content);
                    if (saved != null) {
                        synced.add(saved);
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return synced;
    }

    public static List<String> writeDirectLegacyLauncherDirs(String content) {
        List<String> written = new ArrayList<String>();
        if (content == null) return written;
        byte[] bytes;
        try {
            bytes = content.getBytes("UTF-8");
        } catch (Throwable t) {
            return written;
        }
        for (int i = 0; i < LEGACY_LAUNCHER_DIRS.length; i++) {
            try {
                File dir = new File(LEGACY_LAUNCHER_DIRS[i]);
                if (dir.exists() && dir.isDirectory() && dir.canWrite()) {
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
