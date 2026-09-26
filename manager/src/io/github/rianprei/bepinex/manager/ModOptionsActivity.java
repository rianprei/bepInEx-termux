package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.ConfManager;
import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.ModOption;

import java.util.LinkedHashMap;
import java.util.Map;

public class ModOptionsActivity extends Activity {
    private String mPkg;
    private String mModId;
    private String mModName;
    private ModManifest mManifest;

    private TextView mTvTitle;
    private LinearLayout mContainer;
    private Button mBtnSave;

    private String mExistingConfContent = "";
    private final Map<String, View> mFieldViews = new LinkedHashMap<>();
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_mod_options);

        mPkg = getIntent().getStringExtra("pkg");
        mModId = getIntent().getStringExtra("mod_id");
        mModName = getIntent().getStringExtra("mod_name");
        String manifestJson = getIntent().getStringExtra("manifest_json");

        if (manifestJson != null) {
            try {
                mManifest = ManifestParser.parse(manifestJson);
            } catch (Exception ignored) {}
        }

        mTvTitle = findViewById(R.id.opt_tv_title);
        mContainer = findViewById(R.id.options_container);
        mBtnSave = findViewById(R.id.opt_btn_save);

        mTvTitle.setText("Opções: " + (mModName != null ? mModName : mModId));

        mBtnSave.setOnClickListener(v -> saveOptions());

        loadOptions();
    }

    private void loadOptions() {
        new Thread(() -> {
            String confPath = "/data/local/tmp/mods/" + mPkg + "/" + mModId + ".conf";
            mExistingConfContent = SuHelper.readTextFile(confPath);
            if (mExistingConfContent == null) mExistingConfContent = "";
            Map<String, String> currentValues = ConfManager.parse(mExistingConfContent);

            mMainHandler.post(() -> buildUi(currentValues));
        }).start();
    }

    private void buildUi(Map<String, String> currentValues) {
        mContainer.removeAllViews();
        mFieldViews.clear();

        if (mManifest != null && mManifest.options != null && !mManifest.options.isEmpty()) {
            for (ModOption opt : mManifest.options) {
                addOptionView(opt, currentValues.get(opt.key));
            }
        } else if (!currentValues.isEmpty()) {
            for (Map.Entry<String, String> e : currentValues.entrySet()) {
                ModOption opt = new ModOption(e.getKey(), e.getKey(), "string", e.getValue());
                addOptionView(opt, e.getValue());
            }
        } else {
            TextView emptyTv = new TextView(this);
            emptyTv.setText("Este mod não possui opções configuráveis.");
            emptyTv.setTextColor(Color.parseColor("#94A3B8"));
            emptyTv.setPadding(0, 16, 0, 16);
            mContainer.addView(emptyTv);
        }
    }

    private void addOptionView(ModOption opt, String currentValue) {
        String val = (currentValue != null) ? currentValue :
                (opt.defaultValue != null ? String.valueOf(opt.defaultValue) : "");

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, 8, 0, 12);

        TextView label = new TextView(this);
        label.setText(opt.label != null ? opt.label : opt.key);
        label.setTextSize(14);
        label.setTextColor(Color.parseColor("#F8FAFC"));
        row.addView(label);

        if ("bool".equalsIgnoreCase(opt.type)) {
            Switch sw = new Switch(this);
            sw.setChecked("true".equalsIgnoreCase(val) || "1".equals(val));
            row.addView(sw);
            mFieldViews.put(opt.key, sw);
        } else if ("choice".equalsIgnoreCase(opt.type) && opt.choices != null && !opt.choices.isEmpty()) {
            Spinner spinner = new Spinner(this);
            ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, opt.choices);
            spinner.setAdapter(adapter);
            int idx = opt.choices.indexOf(val);
            if (idx >= 0) spinner.setSelection(idx);
            row.addView(spinner);
            mFieldViews.put(opt.key, spinner);
        } else {
            EditText et = new EditText(this);
            et.setText(val);
            et.setTextColor(Color.parseColor("#F8FAFC"));
            et.setBackgroundResource(R.drawable.card_background);
            et.setPadding(8, 8, 8, 8);

            if ("int".equalsIgnoreCase(opt.type)) {
                et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED);
            } else if ("float".equalsIgnoreCase(opt.type)) {
                et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL | InputType.TYPE_NUMBER_FLAG_SIGNED);
            }
            row.addView(et);
            mFieldViews.put(opt.key, et);
        }

        mContainer.addView(row);
    }

    private void saveOptions() {
        Map<String, String> newValues = new LinkedHashMap<>();

        for (Map.Entry<String, View> entry : mFieldViews.entrySet()) {
            String key = entry.getKey();
            View view = entry.getValue();

            if (view instanceof Switch) {
                newValues.put(key, ((Switch) view).isChecked() ? "true" : "false");
            } else if (view instanceof Spinner) {
                Object item = ((Spinner) view).getSelectedItem();
                newValues.put(key, item != null ? item.toString() : "");
            } else if (view instanceof EditText) {
                newValues.put(key, ((EditText) view).getText().toString().trim());
            }
        }

        String updatedContent = ConfManager.update(mExistingConfContent, newValues);

        new Thread(() -> {
            String confPath = "/data/local/tmp/mods/" + mPkg + "/" + mModId + ".conf";
            boolean ok = SuHelper.writeTextFile(confPath, updatedContent);
            mMainHandler.post(() -> {
                if (ok) {
                    Toast.makeText(this, "Opções salvas com sucesso!", Toast.LENGTH_SHORT).show();
                    finish();
                } else {
                    Toast.makeText(this, "Falha ao salvar arquivo .conf via root.", Toast.LENGTH_LONG).show();
                }
            });
        }).start();
    }
}
