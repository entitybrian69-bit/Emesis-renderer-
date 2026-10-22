package com.entitybrian.emesis;

import android.app.Activity;
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

public class MainActivity extends Activity {

    private TextView simOutputText;
    private TextView policyStatusText;

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
        subView.setText("Universal All-in-One Renderer Plugin APK • 32-bit & 64-bit");
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
        String nativeDir = getApplicationInfo().nativeLibraryDir;
        boolean hasRouter = new File(nativeDir, "libemesis_renderer.so").exists();
        boolean hasModern = new File(nativeDir, "libemesis_modern.so").exists();
        boolean hasLegacy = new File(nativeDir, "libemesis_legacy.so").exists();

        String engineVer = EmesisNative.isLoaded()
                ? EmesisNative.getEngineVersion()
                : ("JNI Error: " + EmesisNative.getLoadError());

        statusCard.addView(infoLine("Plugin State", "Active Launcher Renderer Plugin (FCL / Zalith / Pojav)"));
        statusCard.addView(infoLine("Engine Build", engineVer));
        statusCard.addView(infoLine("Device ABIs", abiList));
        statusCard.addView(infoLine("Bundled Targets", "armeabi-v7a (32-bit), arm64-v8a (64-bit), x86 (32-bit), x86_64 (64-bit)"));
        statusCard.addView(infoLine("Extracted Native Libs",
                "router=" + (hasRouter ? "OK" : "MISSING")
                + " • modern=" + (hasModern ? "OK" : "MISSING")
                + " • legacy=" + (hasLegacy ? "OK" : "MISSING")));
        root.addView(statusCard, cardParams());

        // 3. Automatic Dual-Pipeline Overview Card
        LinearLayout archCard = createCard("#101820", "#1F2937");
        archCard.addView(sectionHeader("UNIFIED AUTOMATIC PIPELINES"));
        archCard.addView(infoLine("Minecraft > 1.16.5 (1.17 – 1.21.x, 26.x+)",
                "Emesis Modern Core — OpenGL 3.2+ Core Profile over GLES 3.x"));
        archCard.addView(infoLine("Minecraft <= 1.16.5 (Classic – 1.8.9 – 1.12.2 – 1.16.5)",
                "Emesis Compatibility Core — OpenGL 2.1 / GLSL-SPIRV Pipeline with 32-bit GLES 2/3 auto-fallback"));
        root.addView(archCard, cardParams());

        // 4. Interactive Native C Version Router Simulator
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

        // 5. Routing Policy Override Card
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

        // 6. Launcher Instructions Card
        LinearLayout guideCard = createCard("#101820", "#1F2937");
        guideCard.addView(sectionHeader("HOW TO USE IN YOUR LAUNCHER"));
        guideCard.addView(infoLine("1. Install Plugin APK",
                "Keep Emesis Renderer installed on your Android device."));
        guideCard.addView(infoLine("2. Select Renderer",
                "Open FoldCraftLauncher (FCL), ZalithLauncher 1/2, or Pojav Glow Worm -> Renderer Settings -> select 'Emesis Renderer (All Versions — Made by Entitybrian)'."));
        guideCard.addView(infoLine("3. Launch Any Minecraft Version",
                "Emesis Renderer automatically dispatches <= 1.16.5 and > 1.16.5 to their dedicated native pipelines in one go."));
        root.addView(guideCard, cardParams());

        scroll.addView(root);
        setContentView(scroll);
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
