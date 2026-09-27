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

import io.github.rianprei.bepinex.manager.core.InFlightFlag;
import io.github.rianprei.bepinex.manager.core.ModInventory;
import io.github.rianprei.bepinex.manager.core.EngineDetector;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;
import io.github.rianprei.bepinex.manager.core.SelectedFileRouter;
import io.github.rianprei.bepinex.manager.core.SelectedFileWork;
import io.github.rianprei.bepinex.manager.core.StatusChecker;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.GameInfo;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

public class MainActivity extends Activity {
    private static final int REQUEST_PICK_FILE = 1001;
    private static final Executor FILE_EXECUTOR =
            command -> new Thread(command, "mod-file-import").start();

    private TextView mTvStatusRoot;
    private TextView mTvStatusModule;
    private TextView mTvStatusZygisk;
    private TextView mTvStatusVersion;
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
    private Uri mPendingIncomingUri;
    private File mPendingImportFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mTvStatusRoot = findViewById(R.id.tv_status_root);
        mTvStatusModule = findViewById(R.id.tv_status_module);
        mTvStatusZygisk = findViewById(R.id.tv_status_zygisk);
        mTvStatusVersion = findViewById(R.id.tv_status_version);
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
            startActivityForResult(intent, REQUEST_PICK_FILE);
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
        if (intent == null) return;
        Uri uri = null;
        if (Intent.ACTION_VIEW.equals(intent.getAction())) {
            uri = intent.getData();
        } else if (Intent.ACTION_SEND.equals(intent.getAction())) {
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                uri = intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class);
            } else {
                //noinspection deprecation
                uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            }
            if (uri == null && intent.getClipData() != null
                    && intent.getClipData().getItemCount() > 0) {
                uri = intent.getClipData().getItemAt(0).getUri();
            }
        }
        if (uri != null) {
            if (mAllGames.isEmpty()) mPendingIncomingUri = uri;
            else processSelectedUri(uri);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_FILE && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                processSelectedUri(uri);
                return;
            }
        } else if (requestCode == REQUEST_PICK_FILE && resultCode == RESULT_CANCELED) {
            new AlertDialog.Builder(this)
                    .setMessage("O seletor de arquivos deste celular não devolveu o arquivo. Quer escolher direto da pasta Download?")
                    .setPositiveButton("Escolher da pasta Download",
                            (dialog, which) -> DownloadFileDialog.show(this, this::processSelectedFile))
                    .setNegativeButton("Cancelar", null)
                    .show();
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

            // Inventário de mods de TODOS os apps em UMA chamada root, ANTES do
            // laço (achado de device 2026-09-27: uma chamada su por app
            // instalado esgotava a memória do aparelho e travava o Manager).
            List<String> allPkgs = new ArrayList<>();
            for (ApplicationInfo ai : apps) {
                if (getPackageName().equals(ai.packageName)) continue;
                allPkgs.add(ai.packageName);
            }
            Map<String, ModInventory.Counts> inv = ModInventory.inventory(allPkgs,
                    new ModInventory.RootCall() {
                        @Override public String exec() { return SuHelper.listAllModsInventory(); }
                    });

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

                // Mods deste jogo vêm do inventário de UMA chamada acima —
                // sem chamada root por app, por refresh, por lista.
                ModInventory.Counts counts = inv.get(ai.packageName);
                int totalMods = (counts != null) ? counts.total : 0;
                int activeMods = (counts != null) ? counts.active : 0;

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

                // Versao do APK: BuildVersion vem do VERSION da raiz do repo
                // (gerado no build), nunca de um numero solto no codigo.
                mTvStatusVersion.setText("Manager: " + status.appVersion);

                mAllGames.clear();
                mAllGames.addAll(loaded);
                applyFilter();
                if (mPendingIncomingUri != null) {
                    Uri pending = mPendingIncomingUri;
                    mPendingIncomingUri = null;
                    processSelectedUri(pending);
                } else if (mPendingImportFile != null) {
                    File pending = mPendingImportFile;
                    mPendingImportFile = null;
                    processSelectedFile(pending);
                }
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
                        EngineDetector.isGameEngine(g.engine) ||
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

    private void processSelectedUri(Uri uri) {
        SelectedFileWork.stage(FILE_EXECUTOR, getCacheDir(), () -> resolveDisplayName(uri),
                () -> getContentResolver().openInputStream(uri), (staged, error) ->
                        mMainHandler.post(() -> {
                            if (isFinishing()) {
                                SelectedFileStager.delete(staged);
                                return;
                            }
                            if (error != null) {
                                SelectedFileStager.delete(staged);
                                new AlertDialog.Builder(this)
                                        .setTitle("Não foi possível abrir o arquivo")
                                        .setMessage("Erro ao ler o arquivo escolhido:\n" + error.getMessage())
                                        .setPositiveButton("OK", null)
                                        .show();
                            } else if (mAllGames.isEmpty()) {
                                mPendingImportFile = staged;
                            } else {
                                processSelectedFile(staged);
                            }
                        }));
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
        return name;
    }

    private void processSelectedFile(File file) {
        if (mAllGames.isEmpty()) {
            mPendingImportFile = file;
            return;
        }
        SelectedFileWork.inspect(FILE_EXECUTOR, file, (inspection, error) ->
                mMainHandler.post(() -> {
                    if (isFinishing()) {
                        SelectedFileStager.delete(file);
                        return;
                    }
                    if (error != null) {
                        SelectedFileStager.delete(file);
                        showImportMessage("Não foi possível abrir o arquivo",
                                "O arquivo não pôde ser identificado: " + error.getMessage());
                        return;
                    }
                    showInspectionResult(file, inspection);
                }));
    }

    private void showInspectionResult(File file, SelectedFileWork.Inspection inspection) {
        SelectedFileRouter.Decision decision = inspection.decision();
        ModManifest manifest = inspection.manifest();
        if (decision.action == SelectedFileRouter.Action.REJECT) {
            showImportMessage("Arquivo não aceito", decision.message);
            SelectedFileStager.delete(file);
        } else if (decision.action == SelectedFileRouter.Action.INSTALL_DECLARED_GAME) {
            new AlertDialog.Builder(this)
                    .setTitle("Instalar mod")
                    .setMessage("Este pacote foi feito para o jogo " + decision.packageName
                            + ". Deseja continuar?")
                    .setPositiveButton("Instalar", (dialog, which) -> {
                        GameInfo target = findGame(decision.packageName);
                        installSelectedFile(file, decision.packageName,
                                target != null ? target.engine : null);
                    })
                    .setNegativeButton("Cancelar", (dialog, which) -> SelectedFileStager.delete(file))
                    .show();
        } else {
            showSelectGameDialogForFile(file, manifest);
        }
    }

    private void showSelectGameDialogForFile(File file, ModManifest manifest) {
        final List<GameInfo> candidates = new ArrayList<>();
        if (manifest != null) {
            for (GameInfo g : mAllGames) {
                if (manifest.matchesEngine(g.engine)) candidates.add(g);
            }
        }
        if (candidates.isEmpty()) candidates.addAll(mAllGames);
        if (candidates.isEmpty()) {
            SelectedFileStager.delete(file);
            showImportMessage("Nenhum jogo disponível", "Não há jogos na lista para instalar este arquivo.");
            return;
        }
        String[] names = new String[candidates.size()];
        for (int i = 0; i < candidates.size(); i++) {
            names[i] = candidates.get(i).appName + " (" + candidates.get(i).packageName + ")";
        }

        new AlertDialog.Builder(this)
                .setTitle("Escolha o jogo para instalar o arquivo")
                .setItems(names, (dialog, which) -> {
                    GameInfo selected = candidates.get(which);
                    installSelectedFile(file, selected.packageName, selected.engine);
                })
                .setNegativeButton("Cancelar", (dialog, which) -> SelectedFileStager.delete(file))
                .show();
    }

    private GameInfo findGame(String packageName) {
        for (GameInfo game : mAllGames) {
            if (packageName.equals(game.packageName)) return game;
        }
        return null;
    }

    private void installSelectedFile(File file, String packageName, String engine) {
        // Duplo toque no mesmo diálogo não pode disparar o segundo install
        // enquanto o primeiro está em voo (dois su + resultado sobreposto).
        final InFlightFlag installInFlight = new InFlightFlag();
        if (!installInFlight.begin()) return;
        SelectedFileWork.install(FILE_EXECUTOR, file, packageName, engine, (result, error) ->
                mMainHandler.post(() -> {
                    SelectedFileStager.delete(file);
                    installInFlight.end();
                    if (error != null) {
                        showImportMessage("Não instalado", "Falha ao instalar o arquivo: "
                                + error.getMessage());
                        return;
                    }
                    showImportMessage(result.success ? "Sucesso" : "Não instalado", result.message);
                    loadStatusAndApps();
                }));
    }

    private void showImportMessage(String title, String message) {
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton("OK", null)
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
