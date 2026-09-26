package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.BmodInstaller;
import io.github.rianprei.bepinex.manager.core.EngineDetector;
import io.github.rianprei.bepinex.manager.core.StatusChecker;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.GameInfo;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class MainActivity extends Activity {
    private static final int REQUEST_PICK_BMOD = 1001;

    private TextView mTvStatusRoot;
    private TextView mTvStatusModule;
    private TextView mTvStatusZygisk;
    private EditText mEtSearch;
    private CheckBox mCbFilterGames;
    private ProgressBar mProgress;
    private ListView mListGames;
    private TextView mTvEmpty;
    private Button mBtnPickBmod;

    private final List<GameInfo> mAllGames = new ArrayList<>();
    private final List<GameInfo> mFilteredGames = new ArrayList<>();
    private GameAdapter mAdapter;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mTvStatusRoot = findViewById(R.id.tv_status_root);
        mTvStatusModule = findViewById(R.id.tv_status_module);
        mTvStatusZygisk = findViewById(R.id.tv_status_zygisk);
        mEtSearch = findViewById(R.id.et_search);
        mCbFilterGames = findViewById(R.id.cb_filter_games);
        mProgress = findViewById(R.id.progress_loading);
        mListGames = findViewById(R.id.list_games);
        mTvEmpty = findViewById(R.id.tv_empty);
        mBtnPickBmod = findViewById(R.id.btn_pick_bmod);

        mAdapter = new GameAdapter(this, mFilteredGames);
        mListGames.setAdapter(mAdapter);

        mListGames.setOnItemClickListener((parent, view, position, id) -> {
            GameInfo game = mFilteredGames.get(position);
            Intent intent = new Intent(MainActivity.this, GameDetailActivity.class);
            intent.putExtra("pkg", game.packageName);
            intent.putExtra("name", game.appName);
            intent.putExtra("engine", game.engine);
            startActivity(intent);
        });

        mEtSearch.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                applyFilter();
            }
            @Override
            public void afterTextChanged(Editable s) {}
        });

        mCbFilterGames.setOnCheckedChangeListener((buttonView, isChecked) -> applyFilter());

        mBtnPickBmod.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, REQUEST_PICK_BMOD);
        });

        handleIncomingIntent(getIntent());
    }

    @Override
    protected void onResume() {
        super.onResume();
        loadStatusAndApps();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIncomingIntent(intent);
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent != null && Intent.ACTION_VIEW.equals(intent.getAction())) {
            Uri uri = intent.getData();
            if (uri != null) {
                processBmodUri(uri);
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_BMOD && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                processBmodUri(uri);
            }
        }
    }

    private void loadStatusAndApps() {
        mProgress.setVisibility(View.VISIBLE);
        new Thread(() -> {
            // 1. Checa status do sistema
            StatusChecker.SystemStatus status = StatusChecker.check();

            // 2. Carrega lista de aplicativos instalados
            PackageManager pm = getPackageManager();
            List<ApplicationInfo> apps = pm.getInstalledApplications(PackageManager.GET_META_DATA);
            List<GameInfo> loaded = new ArrayList<>();

            for (ApplicationInfo ai : apps) {
                // Pular o proprio bepInEx Manager
                if (getPackageName().equals(ai.packageName)) continue;

                // Detecta se eh categorizado como jogo
                boolean isGame = false;
                if ((ai.flags & ApplicationInfo.FLAG_IS_GAME) != 0) {
                    isGame = true;
                } else if (ai.category == ApplicationInfo.CATEGORY_GAME) {
                    isGame = true;
                }

                // Detecta engine (Contrato C6)
                List<File> apks = new ArrayList<>();
                if (ai.sourceDir != null) apks.add(new File(ai.sourceDir));
                if (ai.splitSourceDirs != null) {
                    for (String s : ai.splitSourceDirs) apks.add(new File(s));
                }
                File nativeDir = ai.nativeLibraryDir != null ? new File(ai.nativeLibraryDir) : null;
                String engine = EngineDetector.detectFromApks(apks, nativeDir);

                // Checa se ja existem mods para este jogo em /data/local/tmp/mods/<pkg>/
                List<String> modFiles = SuHelper.listFiles("/data/local/tmp/mods/" + ai.packageName);
                int totalMods = 0;
                int activeMods = 0;
                for (String f : modFiles) {
                    if (f.endsWith(".so") || f.endsWith(".patch")) {
                        totalMods++;
                        activeMods++;
                    } else if (f.endsWith(".so.off") || f.endsWith(".patch.off")) {
                        totalMods++;
                    }
                }

                GameInfo info = new GameInfo();
                info.packageName = ai.packageName;
                info.appName = ai.loadLabel(pm).toString();
                info.engine = engine;
                info.isGame = isGame;
                info.installedModsCount = totalMods;
                info.activeModsCount = activeMods;

                loaded.add(info);
            }

            // Ordena alfabeticamente
            Collections.sort(loaded, (a, b) -> a.appName.compareToIgnoreCase(b.appName));

            mMainHandler.post(() -> {
                mProgress.setVisibility(View.GONE);

                // Atualiza cards de status
                mTvStatusRoot.setText(status.rootOk ? "Root: OK" : "Root: Não");
                mTvStatusRoot.setTextColor(status.rootOk ? Color.parseColor("#22C55E") : Color.parseColor("#EF4444"));

                mTvStatusModule.setText(status.moduleActive ? "Módulo: OK" : "Módulo: Não");
                mTvStatusModule.setTextColor(status.moduleActive ? Color.parseColor("#22C55E") : Color.parseColor("#EAB308"));

                mTvStatusZygisk.setText(status.zygiskActive ? "Zygisk: OK" : "Zygisk: ?");
                mTvStatusZygisk.setTextColor(status.zygiskActive ? Color.parseColor("#22C55E") : Color.parseColor("#EAB308"));

                mAllGames.clear();
                mAllGames.addAll(loaded);
                applyFilter();
            });
        }).start();
    }

    private void applyFilter() {
        String q = mEtSearch.getText().toString().trim().toLowerCase();
        boolean filterGamesOnly = mCbFilterGames.isChecked();

        mFilteredGames.clear();
        for (GameInfo g : mAllGames) {
            if (filterGamesOnly) {
                boolean isCompatible = g.isGame ||
                        !EngineDetector.ENGINE_JAVA.equals(g.engine) ||
                        g.installedModsCount > 0;
                if (!isCompatible) continue;
            }

            if (!q.isEmpty()) {
                boolean matchesName = g.appName.toLowerCase().contains(q);
                boolean matchesPkg = g.packageName.toLowerCase().contains(q);
                if (!matchesName && !matchesPkg) continue;
            }

            mFilteredGames.add(g);
        }

        mAdapter.notifyDataSetChanged();
        mTvEmpty.setVisibility(mFilteredGames.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private void processBmodUri(Uri uri) {
        try {
            File tmp = new File(getCacheDir(), "imported_" + System.currentTimeMillis() + ".bmod");
            try (InputStream in = getContentResolver().openInputStream(uri);
                 FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[4096];
                int n;
                while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            }

            ModManifest manifest = BmodInstaller.inspect(tmp);

            if (manifest.isUniversalGame()) {
                // Mod universal: pergunta ao usuario para qual jogo instalar
                showSelectGameDialogForBmod(tmp, manifest);
            } else {
                new AlertDialog.Builder(this)
                        .setTitle("Instalar Mod (.bmod)")
                        .setMessage("Deseja instalar o mod '" + manifest.name + "' (" + manifest.id + ") para o jogo:\n\n" +
                                manifest.game + "\n\nEngine: " + manifest.engine)
                        .setPositiveButton("Instalar", (dialog, which) -> {
                            BmodInstaller.InstallResult res = BmodInstaller.install(tmp, manifest.game, null);
                            new AlertDialog.Builder(MainActivity.this)
                                    .setTitle(res.success ? "Sucesso" : "Falha na Instalação")
                                    .setMessage(res.message)
                                    .setPositiveButton("OK", null)
                                    .show();
                            loadStatusAndApps();
                        })
                        .setNegativeButton("Cancelar", null)
                        .show();
            }
        } catch (Exception e) {
            new AlertDialog.Builder(this)
                    .setTitle("Erro ao abrir .bmod")
                    .setMessage("Não foi possível ler o pacote .bmod:\n" + e.getMessage())
                    .setPositiveButton("OK", null)
                    .show();
        }
    }

    private void showSelectGameDialogForBmod(File bmodFile, ModManifest manifest) {
        final List<GameInfo> candidates = new ArrayList<>();
        for (GameInfo g : mAllGames) {
            if (manifest.matchesEngine(g.engine)) candidates.add(g);
        }
        if (candidates.isEmpty()) candidates.addAll(mAllGames);

        String[] names = new String[candidates.size()];
        for (int i = 0; i < candidates.size(); i++) {
            names[i] = candidates.get(i).appName + " (" + candidates.get(i).packageName + ")";
        }

        new AlertDialog.Builder(this)
                .setTitle("Escolha o jogo para o mod universal")
                .setItems(names, (dialog, which) -> {
                    GameInfo selected = candidates.get(which);
                    BmodInstaller.InstallResult res = BmodInstaller.install(bmodFile, selected.packageName, selected.engine);
                    new AlertDialog.Builder(MainActivity.this)
                            .setTitle(res.success ? "Sucesso" : "Erro")
                            .setMessage(res.message)
                            .setPositiveButton("OK", null)
                            .show();
                    loadStatusAndApps();
                })
                .setNegativeButton("Cancelar", null)
                .show();
    }

    private static class GameAdapter extends BaseAdapter {
        private final Context mContext;
        private final List<GameInfo> mList;
        private final PackageManager mPm;

        GameAdapter(Context context, List<GameInfo> list) {
            mContext = context;
            mList = list;
            mPm = context.getPackageManager();
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
                convertView = LayoutInflater.from(mContext).inflate(R.layout.item_game, parent, false);
            }

            GameInfo g = mList.get(position);
            ImageView img = convertView.findViewById(R.id.img_game_icon);
            TextView tvName = convertView.findViewById(R.id.tv_game_name);
            TextView tvPkg = convertView.findViewById(R.id.tv_game_pkg);
            TextView tvBadge = convertView.findViewById(R.id.tv_badge_engine);
            TextView tvMods = convertView.findViewById(R.id.tv_mod_count);

            tvName.setText(g.appName);
            tvPkg.setText(g.packageName);
            tvBadge.setText(EngineDetector.getDisplayName(g.engine));

            // Define cor do badge de engine
            GradientDrawable gd = new GradientDrawable();
            gd.setCornerRadius(8);
            switch (g.engine) {
                case EngineDetector.ENGINE_UNITY_IL2CPP:
                case EngineDetector.ENGINE_UNITY_MONO:
                    gd.setColor(Color.parseColor("#10B981")); // verde
                    break;
                case EngineDetector.ENGINE_COCOS2DX:
                    gd.setColor(Color.parseColor("#F59E0B")); // laranja
                    break;
                case EngineDetector.ENGINE_UNREAL:
                    gd.setColor(Color.parseColor("#8B5CF6")); // roxo
                    break;
                case EngineDetector.ENGINE_GODOT:
                    gd.setColor(Color.parseColor("#06B6D4")); // ciano
                    break;
                case EngineDetector.ENGINE_NATIVE:
                    gd.setColor(Color.parseColor("#3B82F6")); // azul
                    break;
                default:
                    gd.setColor(Color.parseColor("#64748B")); // cinza
                    break;
            }
            tvBadge.setBackground(gd);

            if (g.installedModsCount > 0) {
                tvMods.setText(g.installedModsCount + " mod(s) (" + g.activeModsCount + " ativo)");
                tvMods.setTextColor(Color.parseColor("#22C55E"));
            } else {
                tvMods.setText("Nenhum mod");
                tvMods.setTextColor(Color.parseColor("#94A3B8"));
            }

            try {
                Drawable icon = mPm.getApplicationIcon(g.packageName);
                img.setImageDrawable(icon);
            } catch (Exception e) {
                img.setImageResource(android.R.drawable.sym_def_app_icon);
            }

            return convertView;
        }
    }
}
