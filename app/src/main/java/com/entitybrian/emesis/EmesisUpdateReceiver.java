package com.entitybrian.emesis;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;
import java.util.List;

/**
 * Automatically updates custom_env.txt across all linked Pojav/Amethyst/Mojo SAF folders
 * the instant Android finishes updating the Emesis Renderer APK (even if the user taps
 * "Done" in PackageInstaller without opening the Emesis app), preventing stale
 * randomized /data/app/~~.../lib/<abi>/libemesis_renderer.so paths on Android 11+.
 *
 * If no SAF grant has been linked yet (e.g. manual copy/paste) or an OEM ROM blocked
 * waking the launcher's DocumentsProvider in the background, posts a 1-tap sync reminder.
 */
public class EmesisUpdateReceiver extends BroadcastReceiver {

    private static final String TAG = "EmesisRenderer";
    private static final String CHANNEL_ID = "emesis_apk_sync";
    private static final int NOTIF_ID = 1301;

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
                    if (synced.isEmpty()) {
                        postSyncReminderNotification(appCtx);
                    }
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

    private static void postSyncReminderNotification(Context ctx) {
        try {
            NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm == null) return;

            Intent openIntent = new Intent(ctx, MainActivity.class);
            openIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 23) {
                piFlags |= PendingIntent.FLAG_IMMUTABLE;
            }
            PendingIntent pi = PendingIntent.getActivity(ctx, 0, openIntent, piFlags);

            Notification.Builder builder;
            if (Build.VERSION.SDK_INT >= 26) {
                NotificationChannel ch = new NotificationChannel(
                        CHANNEL_ID,
                        "Emesis Launcher Sync",
                        NotificationManager.IMPORTANCE_HIGH);
                nm.createNotificationChannel(ch);
                builder = new Notification.Builder(ctx, CHANNEL_ID);
            } else {
                builder = new Notification.Builder(ctx);
            }

            builder.setSmallIcon(R.mipmap.ic_launcher)
                   .setContentTitle("Emesis Renderer Updated — Tap to Sync Pojav")
                   .setContentText("Android rotated the plugin path. Tap once & press 'Install to Pojav' to link auto-updates.")
                   .setContentIntent(pi)
                   .setAutoCancel(true);

            nm.notify(NOTIF_ID, builder.build());
        } catch (Throwable ignored) {
        }
    }
}
