package com.entitybrian.emesis;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import java.util.List;

/**
 * Automatically updates custom_env.txt across all linked Pojav/Amethyst/Mojo SAF folders
 * the instant Android finishes updating the Emesis Renderer APK (even if the user taps
 * "Done" in PackageInstaller without opening the Emesis app), preventing stale
 * randomized /data/app/~~.../lib/<abi>/libemesis_renderer.so paths on Android 11+.
 */
public class EmesisUpdateReceiver extends BroadcastReceiver {

    private static final String TAG = "EmesisRenderer";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (context == null || intent == null) return;
        final String action = intent.getAction();
        if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            return;
        }

        final Context appCtx = context.getApplicationContext() != null
                ? context.getApplicationContext()
                : context;
        final PendingResult pending = goAsync();
        Thread syncThread = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    List<String> synced = PojavBridgeSync.syncAllLauncherConfigs(appCtx);
                    Log.i(TAG, "MY_PACKAGE_REPLACED synced custom_env.txt to "
                            + synced.size() + " launcher folder(s): "
                            + PojavBridgeSync.getRouterPath(appCtx));
                } catch (Throwable t) {
                    Log.w(TAG, "MY_PACKAGE_REPLACED auto-sync warning: " + t.getMessage());
                } finally {
                    if (pending != null) {
                        try {
                            pending.finish();
                        } catch (Throwable ignored) {
                        }
                    }
                }
            }
        }, "EmesisApkUpdateSync");
        syncThread.start();
    }
}
