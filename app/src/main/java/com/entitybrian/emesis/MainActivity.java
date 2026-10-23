package com.entitybrian.emesis;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {

    private static final int REQ_OPEN_POJAV_TREE = 1101;

    private TextView simOutputText;
    private TextView policyStatusText;
    private TextView bridgeStatusText;
    private String activeCustomEnvContent;

    private static final String[][] KNOWN_POJAV_VARIANTS = new String[][] {
        {"net.kdt.pojavlaunch",                 "PojavLauncher (Official)",          "custom_env"},
        {"net.kdt.pojavlaunch.debug",           "PojavLauncher / Custom Fork (Debug)", "custom_env"},
        {"net.kdt.pojavlaunch.canary",          "PojavLauncher (Canary)",            "custom_env"},
        {"net.kdt.pojavlaunch.firefly",         "Pojav Firefly / PGW",               "plugin"},
        {"net.kdt.pojavlaunch.beta",            "PojavLauncher Beta / Zink",         "plugin"},
        {"com.movtery.pojavzh",                 "Pojav Glow Worm (PojavZH)",         "plugin"},
        {"com.movtery.pojavzh.debug",           "Pojav Glow Worm (Debug)",           "plugin"},
        {"com.movtery.zalithlauncher",          "ZalithLauncher 1",                  "plugin"},
        {"com.movtery.zalithlauncher.debug",    "ZalithLauncher 1 (Debug)",          "plugin"},
        {"com.movtery.zalithlauncher.v2",       "ZalithLauncher 2 (Plugin V2)",      "plugin"},
        {"com.movtery.zalithlauncher.v2.debug", "ZalithLauncher 2 (Debug)",          "plugin"},
        {"org.angelauramc.amethyst",            "Amethyst (Pojav Continuation)",     "custom_env"},
        {"org.angelauramc.amethyst.debug",      "Amethyst (Debug)",                  "custom_env"},
        {"io.stamethyst",                       "Amethyst Variant",                  "custom_env"},
        {"git.artdeell.mojo",                   "MojoLauncher",                      "custom_env"},
        {"git.artdeell.mojo.debug",             "MojoLauncher (Debug)",              "custom_env"},
        {"git.artdeell.solcraft",               "SolCraftLauncher",                  "custom_env"},
        {"com.soldev.solcraft",                 "SolCraftLauncher",                  "custom_env"},
        {"com.tungsten.fcl",                    "FoldCraftLauncher (Plugin V1/V2)",  "plugin"},
        {"com.tungsten.fcm",                    "FoldCraftLauncher Variant",         "plugin"},
        {"com.tungsten.fcl.ngg",                "FoldCraftLauncher Custom",          "plugin"},
        {"org.fcl.enchantnet",                  "EnchantNet Launcher",               "plugin"},
        {"com.qcxr.qcxr",                       "QuestCraft (VR Pojav)",             "custom_env"}
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#080C0F"));
        scroll.setFillViewport(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        root.setPadding(pad, dp(28), pad, dp(36));

        // 1. Hero Logo Card
        LinearLayout heroCard = createCard("#0D151C", "#1E3A2F");
        heroCard.setGravity(Gravity.CENTER_HORIZONTAL);

        ImageView logoView = new ImageView(this);
        logoView.setImageResource(R.drawable.emesis_logo_hero);
        logoView.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams logoLp = new LinearLayout.LayoutParams(dp(190), dp(190));
        logoLp.bottomMargin = dp(12);
        heroCard.addView(logoView, logoLp);

        TextView titleView = new TextView(this);
        titleView.setText("EMESIS RENDERER v1.1.5");
        titleView.setTextColor(Color.parseColor("#F3F7FA"));
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 23);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setGravity(Gravity.CENTER);
        heroCard.addView(titleView);

        TextView authorView = new TextView(this);
        authorView.setText("Made by Entitybrian • <1.17 Shader Accelerator");
        authorView.setTextColor(Color.parseColor("#00FF88"));
        authorView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        titleView.setIncludeFontPadding(false);
        authorView.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        authorView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams authorLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        authorLp.topMargin = dp(4);
        heroCard.addView(authorView, authorLp);

        TextView subView = new TextView(this);
        subView.setText("Universal All-Pojav Variants Renderer Plugin APK • 32-bit & 64-bit");
        subView.setTextColor(Color.parseColor("#94A3B8"));
        subView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        subView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        subLp.topMargin = dp(8);
        heroCard.addView(subView, subLp);

        root.addView(heroCard, cardParams());

        // Optional 1-tap cleanup banner if legacy hidden v1.0.x package (com.entitybrian.emesis) is still installed
        if (isLegacyV10Installed()) {
            LinearLayout legacyCard = createCard("#1A140B", "#D97706");
            legacyCard.addView(sectionHeader("LEGACY v1.0 CLEANUP AVAILABLE"));
            legacyCard.addView(infoLine("Old Emesis v1.0 Package Detected",
                    "An older v1.0 test package (com.entitybrian.emesis) is still installed on this phone. Tap below to remove the old duplicate so only v1.1.2+ remains."));
            Button removeOldBtn = createButton("Uninstall Old v1.0 Package");
            removeOldBtn.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    try {
                        Intent del = new Intent(Intent.ACTION_DELETE, Uri.parse("package:com.entitybrian.emesis"));
                        startActivity(del);
                    } catch (Throwable ignored) {
                    }
                }
            });
            legacyCard.addView(removeOldBtn, marginTop(8));
            root.addView(legacyCard, cardParams());
        }

        // 2. Device & Native Plugin Verification Card
        LinearLayout statusCard = createCard("#101820", "#1F2937");
        statusCard.addView(sectionHeader("PLUGIN & DEVICE STATUS"));

        String abiList = getSupportedAbis();
        final String nativeDir = getApplicationInfo().nativeLibraryDir;
        final String routerPath = new File(nativeDir, "libemesis_renderer.so").getAbsolutePath();
        boolean hasRouter = new File(nativeDir, "libemesis_renderer.so").exists();
        boolean hasModern = new File(nativeDir, "libemesis_modern.so").exists();
        boolean hasLegacy = new File(nativeDir, "libemesis_legacy.so").exists();

        String engineVer = EmesisNative.isLoaded()
                ? EmesisNative.getEngineVersion()
                : ("JNI Error: " + EmesisNative.getLoadError());

        statusCard.addView(infoLine("Plugin Protocols",
                "Plugin V2 (fclPlugin_V2) • Plugin V1 (PGW / Zalith / FCL / Boat) • Scoped Storage Pojav SAF Installer"));
        statusCard.addView(infoLine("Engine Build", engineVer));
        statusCard.addView(infoLine("Device ABIs", abiList));
        statusCard.addView(infoLine("Bundled Targets",
                "armeabi-v7a (32-bit), arm64-v8a (64-bit), x86 (32-bit), x86_64 (64-bit)"));
        statusCard.addView(infoLine("Extracted Native Libs",
                "router=" + (hasRouter ? "OK" : "MISSING")
                + " • modern=" + (hasModern ? "OK" : "MISSING")
                + " • legacy=" + (hasLegacy ? "OK" : "MISSING")));
        root.addView(statusCard, cardParams());

        // 3. All Pojav Variants Compatibility & Scoped-Storage SAF Installer Card
        LinearLayout pojavCard = createCard("#101820", "#1F2937");
        pojavCard.addView(sectionHeader("ALL POJAV VARIANTS COMPATIBILITY"));

        pojavCard.addView(infoLine("Detected Launchers on This Device", scanInstalledLaunchers()));
        pojavCard.addView(infoLine("Plugin-Aware Pojav Variants (Zero Config)",
                "Pojav Glow Worm (PGW), Pojav Beta-Zink, Pojav Firefly, ZalithLauncher 1 & 2, and FoldCraftLauncher automatically list 'Emesis Renderer' in Renderer Settings."));
        pojavCard.addView(infoLine("Official PojavLauncher / Debug / Amethyst / Mojo Setup",
                "1) Tap 'Install to Pojav' below and tap 'Use this folder' -> 'Allow' when PojavLauncher's folder opens.\n"
                + "2) In Official PojavLauncher Video Settings, keep Renderer on default 'Holy GL4ES (OpenGL ES 2)' — do NOT select raw 'OpenGL ES 3 (DesktopGL)'. Emesis automatically promotes the context to GLES 3.2/3.1/3.0 + Shader Accelerator!"));

        activeCustomEnvContent = buildPojavCustomEnv(routerPath);

        TextView envPreview = new TextView(this);
        envPreview.setText(activeCustomEnvContent);
        envPreview.setTextColor(Color.parseColor("#CBD5E1"));
        envPreview.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        envPreview.setTypeface(Typeface.MONOSPACE);
        GradientDrawable codeBg = new GradientDrawable();
        codeBg.setColor(Color.parseColor("#080C0F"));
        codeBg.setCornerRadius(dp(8));
        codeBg.setStroke(dp(1), Color.parseColor("#334155"));
        envPreview.setBackground(codeBg);
        envPreview.setPadding(dp(10), dp(8), dp(10), dp(8));
        pojavCard.addView(envPreview, marginTop(8));

        LinearLayout bridgeBtnRow = new LinearLayout(this);
        bridgeBtnRow.setOrientation(LinearLayout.HORIZONTAL);

        Button installEnvBtn = createButton("Install to Pojav");
        Button pickFolderBtn = createButton("Pick Folder");
        Button copyEnvBtn = createButton("Copy");

        LinearLayout.LayoutParams bLp1 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.35f);
        bLp1.rightMargin = dp(6);
        LinearLayout.LayoutParams bLp2 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        bLp2.rightMargin = dp(6);
        LinearLayout.LayoutParams bLp3 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 0.70f);

        bridgeBtnRow.addView(installEnvBtn, bLp1);
        bridgeBtnRow.addView(pickFolderBtn, bLp2);
        bridgeBtnRow.addView(copyEnvBtn, bLp3);
        pojavCard.addView(bridgeBtnRow, marginTop(10));

        bridgeStatusText = new TextView(this);
        bridgeStatusText.setText("Active library path: " + routerPath);
        bridgeStatusText.setTextColor(Color.parseColor("#94A3B8"));
        bridgeStatusText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        pojavCard.addView(bridgeStatusText, marginTop(8));

        // Automatically keep linked Pojav SAF folders synced whenever this app opens (even after APK updates)
        List<String> autoSynced = syncPersistedSafTrees(activeCustomEnvContent);
        if (!autoSynced.isEmpty()) {
            StringBuilder sb = new StringBuilder("Auto-synced custom_env.txt to linked launcher folder(s):\n");
            for (int i = 0; i < autoSynced.size(); i++) {
                if (i > 0) sb.append("\n");
                sb.append("• ").append(autoSynced.get(i));
            }
            bridgeStatusText.setText(sb.toString());
            bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
        }

        installEnvBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                installToPojavLauncher(activeCustomEnvContent);
            }
        });

        pickFolderBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                openPojavSafFolderPicker();
            }
        });

        copyEnvBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) {
                    cm.setPrimaryClip(ClipData.newPlainText("custom_env.txt", activeCustomEnvContent));
                    bridgeStatusText.setText("Copied custom_env.txt lines to clipboard! Ensure there are no blank lines when pasting into custom_env.txt.");
                    bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
                }
            }
        });

        root.addView(pojavCard, cardParams());

        // 4. Automatic Dual-Pipeline Overview Card
        LinearLayout archCard = createCard("#101820", "#1F2937");
        archCard.addView(sectionHeader("UNIFIED AUTOMATIC PIPELINES"));
        archCard.addView(infoLine("Minecraft > 1.16.5 (1.17 – 1.21.x, 26.x+)",
                "Emesis Modern Core — OpenGL 3.2+ Core Profile over GLES 3.x (auto-promotes GLES 3.2/3.1/3.0 context)"));
        archCard.addView(infoLine("Minecraft <= 1.16.5 (Classic – 1.7.10 – 1.8.9 – 1.12.2 – 1.16.5)",
                "Emesis Compatibility Core — GLES 3.2/3.1/3.0 + SPIR-V ESSL Shader Accelerator (Sodium, Iris, OptiFine, ShadersMod, Angelica, Oculus) with Persistent Two-Tier Shader & GPU Binary Cache, 16-MRT FBO State-Diffing, GPU Quad IBO & Hardware PCF Shadows"));
        root.addView(archCard, cardParams());

        // 5. Interactive Native C Version Router Simulator
        LinearLayout simCard = createCard("#101820", "#1F2937");
        simCard.addView(sectionHeader("VERSION ROUTER TESTER"));

        final EditText verInput = new EditText(this);
        verInput.setText("1.16.5");
        verInput.setTextColor(Color.parseColor("#F3F7FA"));
        verInput.setHintTextColor(Color.parseColor("#64748B"));
        verInput.setHint("Enter MC version (e.g. 1.8.9, 1.16.5, 1.17.1, 1.21.4)");
        GradientDrawable inputBg = new GradientDrawable();
        inputBg.setColor(Color.parseColor("#080C0F"));
        inputBg.setCornerRadius(dp(8));
        inputBg.setStroke(dp(1), Color.parseColor("#334155"));
        verInput.setBackground(inputBg);
        verInput.setPadding(dp(12), dp(10), dp(12), dp(10));
        simCard.addView(verInput, marginTop(8));

        Button testBtn = createButton("Test Route in Native Engine");
        simCard.addView(testBtn, marginTop(10));

        simOutputText = new TextView(this);
        simOutputText.setTextColor(Color.parseColor("#00FF88"));
        simOutputText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        simOutputText.setPadding(0, dp(10), 0, 0);
        if (EmesisNative.isLoaded()) {
            simOutputText.setText(EmesisNative.simulateRoute("1.16.5") + "\n" + EmesisNative.simulateRoute("1.21.4"));
        }
        simCard.addView(simOutputText);

        testBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String q = verInput.getText().toString().trim();
                if (EmesisNative.isLoaded()) {
                    simOutputText.setText(EmesisNative.simulateRoute(q));
                }
            }
        });
        root.addView(simCard, cardParams());

        // 6. Routing Policy Override Card
        LinearLayout policyCard = createCard("#101820", "#1F2937");
        policyCard.addView(sectionHeader("ROUTING POLICY OVERRIDE"));

        policyStatusText = new TextView(this);
        policyStatusText.setText("Current Policy: Automatic by Minecraft Version (Recommended)");
        policyStatusText.setTextColor(Color.parseColor("#CBD5E1"));
        policyStatusText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        policyCard.addView(policyStatusText, marginTop(6));

        LinearLayout btnRow = new LinearLayout(this);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);

        Button autoBtn = createButton("Auto");
        Button modBtn = createButton("Force >1.16.5");
        Button legBtn = createButton("Force <=1.16.5");

        LinearLayout.LayoutParams rowBtnLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        rowBtnLp.rightMargin = dp(6);
        btnRow.addView(autoBtn, rowBtnLp);
        btnRow.addView(modBtn, rowBtnLp);
        LinearLayout.LayoutParams lastBtnLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        btnRow.addView(legBtn, lastBtnLp);

        autoBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                writePolicy("auto", "Automatic by Minecraft Version");
            }
        });
        modBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                writePolicy("force_modern", "Forced Modern Core (> 1.16.5)");
            }
        });
        legBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                writePolicy("force_legacy", "Forced Compatibility Core (<= 1.16.5)");
            }
        });

        policyCard.addView(btnRow, marginTop(10));
        root.addView(policyCard, cardParams());

        scroll.addView(root);
        setContentView(scroll);
    }

    private String buildPojavCustomEnv(String routerPath) {
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

    private String scanInstalledLaunchers() {
        PackageManager pm = getPackageManager();
        List<String> found = new ArrayList<String>();
        for (int i = 0; i < KNOWN_POJAV_VARIANTS.length; i++) {
            String pkg = KNOWN_POJAV_VARIANTS[i][0];
            String title = KNOWN_POJAV_VARIANTS[i][1];
            String mode = KNOWN_POJAV_VARIANTS[i][2];
            try {
                PackageInfo pi = pm.getPackageInfo(pkg, 0);
                if (pi != null) {
                    String badge = "plugin".equals(mode) ? "[Auto Plugin]" : "[SAF custom_env]";
                    found.add("• " + title + " " + badge);
                }
            } catch (Throwable ignored) {
            }
        }
        if (found.isEmpty()) {
            return "Ready for all Pojav variants (install any PojavLauncher, PGW, Zalith, Amethyst, Mojo, or FCL build)";
        }
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < found.size(); i++) {
            if (i > 0) sb.append("\n");
            sb.append(found.get(i));
        }
        return sb.toString();
    }

    private void installToPojavLauncher(String content) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText("custom_env.txt", content));
        }

        List<String> written = new ArrayList<String>();
        written.addAll(syncPersistedSafTrees(content));
        written.addAll(writeDirectLegacyLauncherDirs(content));

        if (!written.isEmpty()) {
            StringBuilder sb = new StringBuilder("Installed custom_env.txt to PojavLauncher (& copied to clipboard):\n");
            for (int i = 0; i < written.size(); i++) {
                if (i > 0) sb.append("\n");
                sb.append("• ").append(written.get(i));
            }
            sb.append("\nReady! Launch Minecraft in PojavLauncher (keep Renderer on Holy GL4ES / OpenGL ES 2).");
            bridgeStatusText.setText(sb.toString());
            bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
            return;
        }

        // On Android 11+ Scoped Storage, open PojavLauncher's FolderProvider SAF root directly
        openPojavSafFolderPicker();
    }

    private List<String> writeDirectLegacyLauncherDirs(String content) {
        String[] legacyLauncherDirs = new String[] {
            "/sdcard/games/PojavLauncher",
            "/sdcard/Android/data/net.kdt.pojavlaunch/files",
            "/sdcard/Android/data/net.kdt.pojavlaunch.debug/files",
            "/sdcard/Android/data/org.angelauramc.amethyst/files",
            "/sdcard/Android/data/org.angelauramc.amethyst.debug/files",
            "/sdcard/Android/data/git.artdeell.mojo/files",
            "/sdcard/Android/data/git.artdeell.mojo.debug/files"
        };
        List<String> written = new ArrayList<String>();
        byte[] bytes;
        try {
            bytes = content.getBytes("UTF-8");
        } catch (Throwable t) {
            return written;
        }
        for (int i = 0; i < legacyLauncherDirs.length; i++) {
            try {
                File dir = new File(legacyLauncherDirs[i]);
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

    private List<String> syncPersistedSafTrees(String content) {
        List<String> synced = new ArrayList<String>();
        if (Build.VERSION.SDK_INT < 21) return synced;
        try {
            ContentResolver cr = getContentResolver();
            List<UriPermission> perms = cr.getPersistedUriPermissions();
            if (perms == null) return synced;
            for (int i = 0; i < perms.size(); i++) {
                UriPermission perm = perms.get(i);
                if (perm != null && perm.isWritePermission() && perm.getUri() != null) {
                    String saved = writeCustomEnvToTreeUri(perm.getUri(), content);
                    if (saved != null) {
                        synced.add(saved);
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return synced;
    }

    private boolean isLegacyV10Installed() {
        if ("com.entitybrian.emesis".equals(getPackageName())) {
            return false;
        }
        try {
            return getPackageManager().getPackageInfo("com.entitybrian.emesis", 0) != null;
        } catch (Throwable ignored) {
            return false;
        }
    }

    private Uri discoverPojavInitialSafUri() {
        if (Build.VERSION.SDK_INT < 21) return null;
        PackageManager pm = getPackageManager();
        // Inspect declared DocumentsProviders on known Pojav packages via <queries><package .../>
        for (int i = 0; i < KNOWN_POJAV_VARIANTS.length; i++) {
            String pkg = KNOWN_POJAV_VARIANTS[i][0];
            try {
                PackageInfo pkgInfo = pm.getPackageInfo(pkg, PackageManager.GET_PROVIDERS);
                if (pkgInfo != null && pkgInfo.providers != null) {
                    for (int j = 0; j < pkgInfo.providers.length; j++) {
                        ProviderInfo pi = pkgInfo.providers[j];
                        if (pi != null && pi.authority != null && pi.authority.contains("gamefolder")) {
                            String rootDocId = "/storage/emulated/0/Android/data/" + pkg + "/files";
                            return DocumentsContract.buildDocumentUri(pi.authority, rootDocId);
                        }
                    }
                }
            } catch (Throwable ignored) {
            }
        }

        // Explicit fallback authority table for Official PojavLauncher, Debug/Custom Forks, Amethyst, and Mojo
        String[][] fallbackAuthorities = new String[][] {
            {"net.kdt.pojavlaunch.debug",     "net.kdt.pojavlaunch.scoped.gamefolder.debug"},
            {"net.kdt.pojavlaunch",           "net.kdt.pojavlaunch.scoped.gamefolder"},
            {"net.kdt.pojavlaunch.canary",    "net.kdt.pojavlaunch.scoped.gamefolder.canary"},
            {"org.angelauramc.amethyst",      "org.angelauramc.amethyst.scoped.gamefolder"},
            {"org.angelauramc.amethyst.debug","org.angelauramc.amethyst.scoped.gamefolder.debug"},
            {"git.artdeell.mojo",             "git.artdeell.mojo.scoped.gamefolder"},
            {"git.artdeell.mojo.debug",       "git.artdeell.mojo.scoped.gamefolder.debug"}
        };
        for (int i = 0; i < fallbackAuthorities.length; i++) {
            String pkg = fallbackAuthorities[i][0];
            String auth = fallbackAuthorities[i][1];
            try {
                if (pm.getPackageInfo(pkg, 0) != null) {
                    String rootDocId = "/storage/emulated/0/Android/data/" + pkg + "/files";
                    return DocumentsContract.buildDocumentUri(auth, rootDocId);
                }
            } catch (Throwable ignored) {
            }
        }
        return null;
    }

    private void openPojavSafFolderPicker() {
        if (Build.VERSION.SDK_INT < 21) {
            bridgeStatusText.setText("Copied custom_env.txt to clipboard! Paste into your PojavLauncher custom_env.txt.");
            bridgeStatusText.setTextColor(Color.parseColor("#FBBF24"));
            return;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);

            Uri initialUri = discoverPojavInitialSafUri();
            if (initialUri != null && Build.VERSION.SDK_INT >= 26) {
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, initialUri);
            }

            bridgeStatusText.setText(
                "Select PojavLauncher's root folder (or pick PojavLauncher from the top-left menu) and tap 'Use this folder' -> 'Allow' to auto-write custom_env.txt.");
            bridgeStatusText.setTextColor(Color.parseColor("#FBBF24"));
            startActivityForResult(intent, REQ_OPEN_POJAV_TREE);
        } catch (Throwable t) {
            bridgeStatusText.setText("Could not open folder picker (" + t.getMessage() + "). Copied custom_env.txt to clipboard.");
            bridgeStatusText.setTextColor(Color.parseColor("#FBBF24"));
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_OPEN_POJAV_TREE) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                Uri treeUri = data.getData();
                try {
                    int takeFlags = data.getFlags()
                            & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                    if (takeFlags == 0) {
                        takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
                    }
                    getContentResolver().takePersistableUriPermission(treeUri, takeFlags);
                } catch (Throwable ignored) {
                }

                String writtenDesc = writeCustomEnvToTreeUri(treeUri, activeCustomEnvContent);
                if (writtenDesc != null) {
                    bridgeStatusText.setText(
                        "Successfully wrote custom_env.txt -> " + writtenDesc
                        + "\nPermission saved! Future Emesis updates auto-sync automatically. Keep Pojav Renderer on 'Holy GL4ES (OpenGL ES 2)' and launch Minecraft!");
                    bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
                } else {
                    bridgeStatusText.setText(
                        "Could not write custom_env.txt into selected folder (" + treeUri + "). Please select PojavLauncher's root folder from the top-left menu.");
                    bridgeStatusText.setTextColor(Color.parseColor("#EF4444"));
                }
            }
        }
    }

    private String writeCustomEnvToTreeUri(Uri treeUri, String content) {
        if (Build.VERSION.SDK_INT < 21 || treeUri == null || content == null) return null;
        ContentResolver cr = getContentResolver();
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

    private void writePolicy(String code, String label) {
        try {
            File dir = new File("/sdcard/EmesisRenderer");
            if (!dir.exists()) {
                dir.mkdirs();
            }
            File cfg = new File(dir, "config.json");
            String json = "{\n  \"routingPolicy\": \"" + code + "\"\n}\n";
            FileOutputStream fos = new FileOutputStream(cfg);
            fos.write(json.getBytes("UTF-8"));
            fos.close();
            policyStatusText.setText("Saved Policy: " + label + " (/sdcard/EmesisRenderer/config.json)");
            policyStatusText.setTextColor(Color.parseColor("#00FF88"));
        } catch (Throwable t) {
            policyStatusText.setText("Policy active in session: " + label);
            policyStatusText.setTextColor(Color.parseColor("#FBBF24"));
        }
    }

    private String getSupportedAbis() {
        if (Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < Build.SUPPORTED_ABIS.length; i++) {
                if (i > 0) sb.append(", ");
                sb.append(Build.SUPPORTED_ABIS[i]);
            }
            return sb.toString();
        }
        return Build.CPU_ABI;
    }

    private LinearLayout createCard(String bgHex, String borderHex) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int p = dp(16);
        box.setPadding(p, p, p, p);
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(Color.parseColor(bgHex));
        gd.setCornerRadius(dp(14));
        gd.setStroke(dp(1), Color.parseColor(borderHex));
        box.setBackground(gd);
        return box;
    }

    private LinearLayout.LayoutParams cardParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(14);
        return lp;
    }

    private LinearLayout.LayoutParams marginTop(int dpVal) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(dpVal);
        return lp;
    }

    private TextView sectionHeader(String title) {
        TextView tv = new TextView(this);
        tv.setText(title);
        tv.setTextColor(Color.parseColor("#00FF88"));
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        tv.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        tv.setPadding(0, 0, 0, dp(6));
        return tv;
    }

    private View infoLine(String label, String value) {
        LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setPadding(0, dp(4), 0, dp(4));

        TextView k = new TextView(this);
        k.setText(label);
        k.setTextColor(Color.parseColor("#F3F7FA"));
        k.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        k.setTypeface(Typeface.DEFAULT_BOLD);
        wrap.addView(k);

        TextView v = new TextView(this);
        v.setText(value);
        v.setTextColor(Color.parseColor("#94A3B8"));
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        wrap.addView(v);

        return wrap;
    }

    private Button createButton(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.parseColor("#080C0F"));
        b.setTypeface(Typeface.DEFAULT_BOLD);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.parseColor("#00FF88"));
        bg.setCornerRadius(dp(8));
        b.setBackground(bg);
        return b;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
