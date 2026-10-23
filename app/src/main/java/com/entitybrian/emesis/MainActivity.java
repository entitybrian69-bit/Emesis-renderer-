package com.entitybrian.emesis;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
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
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {

    private TextView simOutputText;
    private TextView policyStatusText;
    private TextView bridgeStatusText;

    private static final String[][] KNOWN_POJAV_VARIANTS = new String[][] {
        {"net.kdt.pojavlaunch",              "PojavLauncher (Official)",        "custom_env"},
        {"net.kdt.pojavlaunch.debug",        "PojavLauncher (Debug / Nightly)", "custom_env"},
        {"net.kdt.pojavlaunch.firefly",      "Pojav Firefly / PGW",             "plugin"},
        {"net.kdt.pojavlaunch.beta",         "PojavLauncher Beta / Zink",       "plugin"},
        {"com.movtery.pojavzh",              "Pojav Glow Worm (PojavZH)",       "plugin"},
        {"com.movtery.pojavzh.debug",        "Pojav Glow Worm (Debug)",         "plugin"},
        {"com.movtery.zalithlauncher",       "ZalithLauncher 1",                "plugin"},
        {"com.movtery.zalithlauncher.debug", "ZalithLauncher 1 (Debug)",        "plugin"},
        {"com.movtery.zalithlauncher.v2",    "ZalithLauncher 2 (Plugin V2)",    "plugin"},
        {"com.movtery.zalithlauncher.v2.debug", "ZalithLauncher 2 (Debug)",     "plugin"},
        {"org.angelauramc.amethyst",         "Amethyst (Pojav Continuation)",   "custom_env"},
        {"org.angelauramc.amethyst.debug",   "Amethyst (Debug)",                "custom_env"},
        {"git.artdeell.mojo",                "MojoLauncher",                    "custom_env"},
        {"git.artdeell.mojo.debug",          "MojoLauncher (Debug)",            "custom_env"},
        {"git.artdeell.solcraft",            "SolCraftLauncher",                "custom_env"},
        {"com.soldev.solcraft",              "SolCraftLauncher",                "custom_env"},
        {"com.tungsten.fcl",                 "FoldCraftLauncher (Plugin V1/V2)","plugin"},
        {"com.tungsten.fcm",                 "FoldCraftLauncher Variant",       "plugin"},
        {"com.tungsten.fcl.ngg",             "FoldCraftLauncher Custom",        "plugin"},
        {"org.fcl.enchantnet",               "EnchantNet Launcher",             "plugin"},
        {"com.qcxr.qcxr",                    "QuestCraft (VR Pojav)",           "custom_env"}
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
        titleView.setText("EMESIS RENDERER");
        titleView.setTextColor(Color.parseColor("#F3F7FA"));
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setGravity(Gravity.CENTER);
        heroCard.addView(titleView);

        TextView authorView = new TextView(this);
        authorView.setText("Made by Entitybrian");
        authorView.setTextColor(Color.parseColor("#00FF88"));
        authorView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
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
                "Plugin V2 (fclPlugin_V2) • Plugin V1 (PGW / Zalith / FCL / Boat) • Vanilla Pojav custom_env Bridge"));
        statusCard.addView(infoLine("Engine Build", engineVer));
        statusCard.addView(infoLine("Device ABIs", abiList));
        statusCard.addView(infoLine("Bundled Targets",
                "armeabi-v7a (32-bit), arm64-v8a (64-bit), x86 (32-bit), x86_64 (64-bit)"));
        statusCard.addView(infoLine("Extracted Native Libs",
                "router=" + (hasRouter ? "OK" : "MISSING")
                + " • modern=" + (hasModern ? "OK" : "MISSING")
                + " • legacy=" + (hasLegacy ? "OK" : "MISSING")));
        root.addView(statusCard, cardParams());

        // 3. All Pojav Variants Compatibility & One-Tap Bridge Card
        LinearLayout pojavCard = createCard("#101820", "#1F2937");
        pojavCard.addView(sectionHeader("ALL POJAV VARIANTS COMPATIBILITY"));

        pojavCard.addView(infoLine("Detected Launchers on This Device", scanInstalledLaunchers()));
        pojavCard.addView(infoLine("Plugin-Aware Pojav Variants (Zero Config)",
                "Pojav Glow Worm (PGW), Pojav Beta-Zink, Pojav Firefly, ZalithLauncher 1 & 2, and FoldCraftLauncher automatically list 'Emesis Renderer' in Renderer Settings."));
        pojavCard.addView(infoLine("Vanilla Pojav / Amethyst / QuestCraft Bridge",
                "Official PojavLauncher and Amethyst load external renderers via custom_env.txt. Keep the launcher's built-in renderer on default OpenGL ES 2.0 and apply the generated custom_env.txt below:"));

        final String customEnvContent = buildPojavCustomEnv(routerPath);

        TextView envPreview = new TextView(this);
        envPreview.setText(customEnvContent);
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

        Button copyEnvBtn = createButton("Copy custom_env");
        Button applyEnvBtn = createButton("Write to Pojav");
        Button shareEnvBtn = createButton("Share");

        LinearLayout.LayoutParams bLp1 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.15f);
        bLp1.rightMargin = dp(6);
        LinearLayout.LayoutParams bLp2 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.15f);
        bLp2.rightMargin = dp(6);
        LinearLayout.LayoutParams bLp3 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 0.75f);

        bridgeBtnRow.addView(copyEnvBtn, bLp1);
        bridgeBtnRow.addView(applyEnvBtn, bLp2);
        bridgeBtnRow.addView(shareEnvBtn, bLp3);
        pojavCard.addView(bridgeBtnRow, marginTop(10));

        bridgeStatusText = new TextView(this);
        bridgeStatusText.setText("Active library path: " + routerPath);
        bridgeStatusText.setTextColor(Color.parseColor("#94A3B8"));
        bridgeStatusText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        pojavCard.addView(bridgeStatusText, marginTop(8));

        copyEnvBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) {
                    cm.setPrimaryClip(ClipData.newPlainText("custom_env.txt", customEnvContent));
                    bridgeStatusText.setText("Copied custom_env.txt lines to clipboard! Paste into your Pojav custom_env.txt.");
                    bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
                }
            }
        });

        applyEnvBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                writePojavCustomEnvFiles(customEnvContent);
            }
        });

        shareEnvBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("text/plain");
                send.putExtra(Intent.EXTRA_SUBJECT, "custom_env.txt");
                send.putExtra(Intent.EXTRA_TEXT, customEnvContent);
                startActivity(Intent.createChooser(send, "Export custom_env.txt for Pojav"));
            }
        });

        root.addView(pojavCard, cardParams());

        // 4. Automatic Dual-Pipeline Overview Card
        LinearLayout archCard = createCard("#101820", "#1F2937");
        archCard.addView(sectionHeader("UNIFIED AUTOMATIC PIPELINES"));
        archCard.addView(infoLine("Minecraft > 1.16.5 (1.17 – 1.21.x, 26.x+)",
                "Emesis Modern Core — OpenGL 3.2+ Core Profile over GLES 3.x (auto-promotes GLES 3.2/3.1/3.0 context)"));
        archCard.addView(infoLine("Minecraft <= 1.16.5 (Classic – 1.7.10 – 1.8.9 – 1.12.2 – 1.16.5)",
                "Emesis Compatibility Core — GLES 3.2/3.1/3.0 + SPIR-V ESSL Shader Accelerator (ShadersMod, OptiFine, Angelica, Oculus) with Persistent Two-Tier Shader & GPU Binary Cache, 16-MRT FBO State-Diffing, GPU Quad IBO & Hardware PCF Shadows"));
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
        return "POJAVEXEC_EGL=" + routerPath + "\n"
             + "LIBGL_ES=3\n"
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
                    String badge = "plugin".equals(mode) ? "[Auto Plugin]" : "[custom_env Bridge]";
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

    private void writePojavCustomEnvFiles(String content) {
        String[] candidateDirs = new String[] {
            "/sdcard/games/PojavLauncher",
            "/sdcard/Android/data/net.kdt.pojavlaunch/files",
            "/sdcard/Android/data/net.kdt.pojavlaunch.debug/files",
            "/sdcard/Android/data/org.angelauramc.amethyst/files",
            "/sdcard/Android/data/git.artdeell.mojo/files",
            "/sdcard/EmesisRenderer"
        };
        List<String> written = new ArrayList<String>();
        for (int i = 0; i < candidateDirs.length; i++) {
            try {
                File dir = new File(candidateDirs[i]);
                if (i == candidateDirs.length - 1 && !dir.exists()) {
                    dir.mkdirs();
                }
                if (dir.exists() && dir.isDirectory()) {
                    File out = new File(dir, "custom_env.txt");
                    FileOutputStream fos = new FileOutputStream(out);
                    fos.write((content + "\n").getBytes("UTF-8"));
                    fos.close();
                    written.add(out.getAbsolutePath());
                }
            } catch (Throwable ignored) {
            }
        }
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText("custom_env.txt", content));
        }
        if (!written.isEmpty()) {
            StringBuilder sb = new StringBuilder("Wrote custom_env.txt (& copied to clipboard):\n");
            for (int i = 0; i < written.size(); i++) {
                if (i > 0) sb.append("\n");
                sb.append("• ").append(written.get(i));
            }
            bridgeStatusText.setText(sb.toString());
            bridgeStatusText.setTextColor(Color.parseColor("#00FF88"));
        } else {
            bridgeStatusText.setText("Copied custom_env.txt to clipboard! On Android 11+ scoped storage, paste into your launcher's custom_env.txt or tap Share.");
            bridgeStatusText.setTextColor(Color.parseColor("#FBBF24"));
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
