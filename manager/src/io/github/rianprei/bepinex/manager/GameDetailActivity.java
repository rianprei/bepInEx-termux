package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.EngineDetector;
import io.github.rianprei.bepinex.manager.core.LooseModInstaller;
import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.ModInfo;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class GameDetailActivity extends Activity {
    private static final int REQUEST_PICK_BMOD_FOR_GAME = 1002;

    private String mPkg;
    private String mAppName;
    private String mEngine;

    private ImageView mImgIcon;
    private TextView mTvName;
    private TextView mTvPkg;
    private TextView mTvEngine;
    private ListView mListMods;
    private TextView mTvEmptyMods;

    private final List<ModInfo> mMods = new ArrayList<>();
    private ModAdapter mAdapter;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_game_detail);

        mPkg = getIntent().getStringExtra("pkg");
        mAppName = getIntent().getStringExtra("name");
        mEngine = getIntent().getStringExtra("engine");

        if (mPkg == null) {
            finish();
            return;
        }

        mImgIcon = findViewById(R.id.detail_img_icon);
        mTvName = findViewById(R.id.detail_tv_name);
        mTvPkg = findViewById(R.id.detail_tv_pkg);
        mTvEngine = findViewById(R.id.detail_tv_engine);
        mListMods = findViewById(R.id.detail_list_mods);
        mTvEmptyMods = findViewById(R.id.detail_tv_empty_mods);

        mTvName.setText(mAppName != null ? mAppName : mPkg);
        mTvPkg.setText(mPkg);
        mTvEngine.setText(EngineDetector.getDisplayName(mEngine));

        // Cor do badge de engine
        GradientDrawable gd = new GradientDrawable();
        gd.setCornerRadius(8);
        gd.setColor(Color.parseColor("#10B981"));
        mTvEngine.setBackground(gd);

        try {
            PackageManager pm = getPackageManager();
            Drawable icon = pm.getApplicationIcon(mPkg);
            mImgIcon.setImageDrawable(icon);
        } catch (Exception e) {
            mImgIcon.setImageResource(android.R.drawable.sym_def_app_icon);
        }

        mAdapter = new ModAdapter(this, mMods);
        mListMods.setAdapter(mAdapter);

        findViewById(R.id.btn_action_restart).setOnClickListener(v -> {
            Toast.makeText(this, "Reiniciando " + mAppName + "...", Toast.LENGTH_SHORT).show();
            new Thread(() -> {
                boolean ok = SuHelper.restartGame(mPkg);
                mMainHandler.post(() -> {
                    if (ok) {
                        Toast.makeText(this, "Jogo reiniciado!", Toast.LENGTH_SHORT).show();
                    } else {
                        Toast.makeText(this, "Falha ao reiniciar o jogo.", Toast.LENGTH_LONG).show();
                    }
                });
            }).start();
        });

        findViewById(R.id.btn_action_log).setOnClickListener(v -> {
            Intent intent = new Intent(this, LogViewerActivity.class);
            intent.putExtra("pkg", mPkg);
            startActivity(intent);
        });

        findViewById(R.id.btn_action_install_bmod).setOnClickListener(v -> {
            // type */*: o C7 olha o CONTEUDO do arquivo, nao a extensao.
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, REQUEST_PICK_BMOD_FOR_GAME);
        });

        findViewById(R.id.btn_action_mod_maker).setOnClickListener(v -> {
            Intent intent = new Intent(this, ModMakerActivity.class);
            intent.putExtra("pkg", mPkg);
            intent.putExtra("name", mAppName);
            intent.putExtra("engine", mEngine);
            startActivity(intent);
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        loadMods();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_BMOD_FOR_GAME && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                installSelectedFile(uri);
            }
        }
    }

    // C7: o usuario escolhe QUALQUER arquivo e o Manager descobre pelo
    // conteudo o que e (.bmod, .so arm64, .patch, .js, .dll de PC...). O
    // nome original so importa para virar o id do arquivo instalado.
    private void installSelectedFile(Uri uri) {
        String displayName = resolveDisplayName(uri);
        File tmp = new File(getCacheDir(), "install_" + System.currentTimeMillis() + "_" + displayName);
        try {
            try (InputStream in = getContentResolver().openInputStream(uri);
                 FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            }

            LooseModInstaller.Result res = LooseModInstaller.installFromFile(tmp, mPkg, mEngine);
            tmp.delete();

            new AlertDialog.Builder(this)
                    .setTitle(res.success ? "Sucesso" : "Nao instalado")
                    .setMessage(res.message)
                    .setPositiveButton("OK", null)
                    .show();

            loadMods();
        } catch (Exception e) {
            tmp.delete();
            new AlertDialog.Builder(this)
                    .setTitle("Nao instalado")
                    .setMessage("Erro ao ler o arquivo: " + e.getMessage())
                    .setPositiveButton("OK", null)
                    .show();
        }
    }

    private String resolveDisplayName(Uri uri) {
        String name = null;
        try (android.database.Cursor c = getContentResolver()
                .query(uri, new String[]{android.provider.OpenableColumns.DISPLAY_NAME},
                        null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) name = c.getString(idx);
            }
        } catch (Exception ignored) {}
        if (name == null || name.trim().isEmpty()) {
            String last = uri.getLastPathSegment();
            name = (last != null) ? last : "mod.bin";
        }
        return name.replaceAll("[^A-Za-z0-9._-]", "_");
    }

    private void loadMods() {
        new Thread(() -> {
            List<String> files = SuHelper.listFiles("/data/local/tmp/mods/" + mPkg);
            Map<String, ModInfo> map = new LinkedHashMap<>();

            // 1. Encontra todos os arquivos de mod (.so, .patch, .off)
            for (String f : files) {
                if (f.equals("u_dump.so") || f.equals("u_patch.so")) {
                    // Arquivos do motor interno do sistema
                    continue;
                }

                String baseId = null;
                boolean enabled = true;
                String type = "patch";

                if (f.endsWith(".so")) {
                    baseId = f.substring(0, f.length() - 3);
                    type = "native";
                } else if (f.endsWith(".so.off")) {
                    baseId = f.substring(0, f.length() - 7);
                    type = "native";
                    enabled = false;
                } else if (f.endsWith(".patch")) {
                    baseId = f.substring(0, f.length() - 6);
                    type = "patch";
                } else if (f.endsWith(".patch.off")) {
                    baseId = f.substring(0, f.length() - 10);
                    type = "patch";
                    enabled = false;
                }

                if (baseId != null) {
                    ModInfo info = map.get(baseId);
                    if (info == null) {
                        info = new ModInfo(baseId);
                        map.put(baseId, info);
                    }
                    info.type = type;
                    info.isEnabled = enabled;
                }
            }

            // 2. Le metadados de <id>.json e verifica <id>.conf
            for (Map.Entry<String, ModInfo> entry : map.entrySet()) {
                String id = entry.getKey();
                ModInfo info = entry.getValue();

                if (files.contains(id + ".conf")) {
                    info.hasConf = true;
                }

                if (files.contains(id + ".json")) {
                    String json = SuHelper.readTextFile("/data/local/tmp/mods/" + mPkg + "/" + id + ".json");
                    if (json != null) {
                        try {
                            ModManifest m = ManifestParser.parse(json);
                            info.manifest = m;
                            info.name = m.name;
                            info.version = m.version;
                            info.author = m.author;
                            info.description = m.description;
                            if (m.options != null && !m.options.isEmpty()) {
                                info.hasOptions = true;
                            }
                        } catch (Exception ignored) {}
                    }
                }
            }

            List<ModInfo> result = new ArrayList<>(map.values());

            mMainHandler.post(() -> {
                mMods.clear();
                mMods.addAll(result);
                mAdapter.notifyDataSetChanged();
                mTvEmptyMods.setVisibility(mMods.isEmpty() ? View.VISIBLE : View.GONE);
            });
        }).start();
    }

    private class ModAdapter extends BaseAdapter {
        private final Context mContext;
        private final List<ModInfo> mList;

        ModAdapter(Context context, List<ModInfo> list) {
            mContext = context;
            mList = list;
        }

        @Override
        public int getCount() { return mList.size(); }
        @Override
        public Object getItem(int position) { return mList.get(position); }
        @Override
        public long getItemId(int position) { return position; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            if (convertView == null) {
                convertView = LayoutInflater.from(mContext).inflate(R.layout.item_mod, parent, false);
            }

            ModInfo mod = mList.get(position);
            TextView tvName = convertView.findViewById(R.id.mod_tv_name);
            TextView tvMeta = convertView.findViewById(R.id.mod_tv_meta);
            TextView tvDesc = convertView.findViewById(R.id.mod_tv_description);
            Switch swEnabled = convertView.findViewById(R.id.mod_switch_enabled);
            Button btnOptions = convertView.findViewById(R.id.mod_btn_options);
            Button btnDelete = convertView.findViewById(R.id.mod_btn_delete);

            tvName.setText(mod.name);

            String authorStr = (mod.author != null && !mod.author.isEmpty()) ? " por " + mod.author : "";
            tvMeta.setText("v" + mod.version + authorStr + " (" + mod.type + ")");

            if (mod.description != null && !mod.description.isEmpty()) {
                tvDesc.setText(mod.description);
                tvDesc.setVisibility(View.VISIBLE);
            } else {
                tvDesc.setVisibility(View.GONE);
            }

            // Desvincula listener antigo antes de setChecked para evitar loops
            swEnabled.setOnCheckedChangeListener(null);
            swEnabled.setChecked(mod.isEnabled);
            swEnabled.setOnCheckedChangeListener((buttonView, isChecked) -> {
                mod.isEnabled = isChecked;
                new Thread(() -> {
                    String filename = mod.getMainFilename();
                    SuHelper.toggleMod(mPkg, filename, isChecked);
                }).start();
            });

            // Botao de opcoes (aparece se houver opcoes no manifest ou .conf)
            if (mod.hasOptions || mod.hasConf) {
                btnOptions.setVisibility(View.VISIBLE);
                btnOptions.setOnClickListener(v -> {
                    Intent optIntent = new Intent(GameDetailActivity.this, ModOptionsActivity.class);
                    optIntent.putExtra("pkg", mPkg);
                    optIntent.putExtra("mod_id", mod.id);
                    optIntent.putExtra("mod_name", mod.name);
                    if (mod.manifest != null) {
                        optIntent.putExtra("manifest_json", ManifestParser.toJson(mod.manifest));
                    }
                    startActivity(optIntent);
                });
            } else {
                btnOptions.setVisibility(View.GONE);
            }

            btnDelete.setOnClickListener(v -> {
                new AlertDialog.Builder(GameDetailActivity.this)
                        .setTitle("Excluir Mod")
                        .setMessage(getString(R.string.confirm_delete_mod, mod.name))
                        .setPositiveButton(R.string.btn_delete, (dialog, which) -> {
                            new Thread(() -> {
                                SuHelper.deleteMod(mPkg, mod.id);
                                mMainHandler.post(GameDetailActivity.this::loadMods);
                            }).start();
                        })
                        .setNegativeButton(R.string.btn_cancel, null)
                        .show();
            });

            return convertView;
        }
    }
}
