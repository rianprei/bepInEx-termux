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
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.LooseModInstaller;
import io.github.rianprei.bepinex.manager.core.UiLiveness;
import io.github.rianprei.bepinex.manager.core.CrashGuardState;
import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.ModInventory;
import io.github.rianprei.bepinex.manager.core.EngineDetector;
import io.github.rianprei.bepinex.manager.core.SelectedFileFlow;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;
import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.ModInfo;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

public class GameDetailActivity extends Activity implements UiLiveness.ActivityLike {
    private static final int REQUEST_PICK_BMOD_FOR_GAME = 1002;
    private static final Executor FILE_EXECUTOR =
            command -> new Thread(command, "mod-file-import").start();

    private String mPkg;
    private String mAppName;
    private String mEngine;

    private ImageView mImgIcon;
    private TextView mTvName;
    private TextView mTvPkg;
    private TextView mTvEngine;
    private TextView mTvEngineSupport;
    private ListView mListMods;
    private TextView mTvEmptyMods;
    private LinearLayout mCrashGuardBox;
    private TextView mTvCrashGuard;
    private Button mBtnReactivate;

    private final List<ModInfo> mMods = new ArrayList<>();
    private ModAdapter mAdapter;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    // Estado do arquivo escolhido: por tela e estático — rotação herda o
    // pendente e a flag de install (o duplo toque é recusado pela própria API).
    private final SelectedFileFlow.Flow mFlow = SelectedFileFlow.of(SelectedFileFlow.DETAIL);

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
        mTvEngineSupport = findViewById(R.id.detail_tv_engine_support);
        mListMods = findViewById(R.id.detail_list_mods);
        mTvEmptyMods = findViewById(R.id.detail_tv_empty_mods);
        mCrashGuardBox = findViewById(R.id.detail_crashguard_box);
        mTvCrashGuard = findViewById(R.id.detail_tv_crashguard);
        mBtnReactivate = findViewById(R.id.detail_btn_reactivate);

        mTvName.setText(mAppName != null ? mAppName : mPkg);
        mTvPkg.setText(mPkg);
        mTvEngine.setText(EngineDetector.getDisplayName(mEngine));
        mTvEngineSupport.setText(EngineDetector.getModSupport(mEngine));

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
            String activityComponent = LaunchActivityResolver.resolve(getPackageManager(), mPkg);
            if (activityComponent == null) {
                Toast.makeText(this, LaunchActivityResolver.NOT_FOUND_MESSAGE, Toast.LENGTH_LONG).show();
                return;
            }
            Toast.makeText(this, "Reiniciando " + mAppName + "...", Toast.LENGTH_SHORT).show();
            new Thread(() -> {
                boolean ok = SuHelper.restartGame(mPkg, activityComponent);
                mMainHandler.post(() -> {
                    if (!UiLiveness.alive(this)) return;
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

        // Reativar: apaga o marcador do crashguard e zera o contador. Sem
        // isso o aviso volta a aparecer mesmo com o jogo funcionando.
        mBtnReactivate.setOnClickListener(v -> {
            mBtnReactivate.setEnabled(false);
            new Thread(() -> {
                boolean ok = SuHelper.reactivateMods(mPkg);
                mMainHandler.post(() -> {
                    if (!UiLiveness.alive(this)) return;
                    mBtnReactivate.setEnabled(true);
                    if (ok) {
                        mCrashGuardBox.setVisibility(View.GONE);
                        Toast.makeText(this, R.string.crashguard_reactivated, Toast.LENGTH_LONG).show();
                        loadMods();
                    } else {
                        // A PRIMEIRA chamada ja falhou: mostrar o resultado
                        // dela, nao rodar o comando root de novo dentro da
                        // mensagem de erro (isso reescrevia o contador do
                        // crashguard sem o usuario pedir, e o texto ficava
                        // mentindo sobre o que aconteceu).
                        new AlertDialog.Builder(this)
                                .setTitle("Nao deu para reativar")
                                .setMessage("O comando root falhou. Se o Magisk/KernelSU nao esta "
                                        + "concedendo root ao Manager, o aviso volta a aparecer. "
                                        + "O aviso do crashguard continua valendo: abra o jogo de novo "
                                        + "depois de conceder root ao Manager.")
                                .setPositiveButton("OK", null)
                                .show();
                    }
                });
            }).start();
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
        // Herança de rotação: o staged da instância anterior retoma o install
        // (aqui o jogo de destino é fixo, definido quando o usuário escolheu).
        File pending = mFlow.pending().take();
        if (pending != null) {
            installStagedFile(pending);
            return;
        }
        loadMods();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Saída definitiva: nenhum staged pendente sobra no cache. Em rotação
        // (isFinishing == false) o arquivo fica pendente para a nova instância.
        if (isFinishing()) {
            File pending = mFlow.pending().take();
            if (pending != null) SelectedFileStager.delete(pending);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_BMOD_FOR_GAME && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                installSelectedFile(uri);
                return;
            }
        } else if (requestCode == REQUEST_PICK_BMOD_FOR_GAME && resultCode == RESULT_CANCELED) {
            new AlertDialog.Builder(this)
                    .setMessage("O seletor de arquivos deste celular não devolveu o arquivo. Quer escolher direto da pasta Download?")
                    .setPositiveButton("Escolher da pasta Download",
                            (dialog, which) -> DownloadFileDialog.show(this, this::installStagedFile))
                    .setNegativeButton("Cancelar", null)
                    .show();
        }
    }

    // C7: o usuario escolhe QUALQUER arquivo e o Manager descobre pelo
    // conteudo o que e (.bmod, .so arm64, .bpatch, .js, .dll de PC...). O
    // nome original so importa para virar o id do arquivo instalado.
    private void installSelectedFile(Uri uri) {
        mFlow.stage(FILE_EXECUTOR, mMainHandler::post,
                () -> SelectedFileStager.copyIntoStaging(getCacheDir(), resolveDisplayName(uri),
                        () -> getContentResolver().openInputStream(uri)),
                (staged, error) -> {
                    if (!UiLiveness.alive(this)) {
                        // Tela morreu no meio da cópia (rotação etc.):
                        // fica pendente para a instância nova.
                        if (staged != null && !mFlow.pending().set(staged)) {
                            SelectedFileStager.delete(staged);
                        }
                        return;
                    }
                    if (error != null) {
                        mFlow.consume(staged);
                        new AlertDialog.Builder(this)
                                .setTitle("Não instalado")
                                .setMessage("Erro ao ler o arquivo: " + error.getMessage())
                                .setPositiveButton("OK", null)
                                .show();
                    } else {
                        installStagedFile(staged);
                    }
                });
    }

    private void installStagedFile(File file) {
        // A flag vive no ESTADO da tela: o segundo toque chama a mesma API
        // e é recusado enquanto o primeiro roda (bug A, agora na raiz).
        boolean started = mFlow.install(FILE_EXECUTOR, mMainHandler::post, file,
                () -> LooseModInstaller.installFromFile(file, mPkg, mEngine),
                (result, error) -> {
                    if (!UiLiveness.alive(this)) {
                        // Morreu com o install em voo: se instalou, o staged
                        // cumpriu o papel; se não, a instância nova retoma.
                        if (error == null && result != null && result.success) {
                            mFlow.consume(file);
                        } else if (!mFlow.pending().set(file)) {
                            mFlow.consume(file);
                        }
                        return;
                    }
                    mFlow.consume(file);
                    if (isFinishing()) return;
                    String title = error != null || !result.success ? "Não instalado" : "Sucesso";
                    String message = error != null
                            ? "Falha ao instalar o arquivo: " + error.getMessage() : result.message;
                    new AlertDialog.Builder(this)
                            .setTitle(title)
                            .setMessage(message)
                            .setPositiveButton("OK", null)
                            .show();
                    loadMods();
                });
        if (!started) return;   // duplo toque: o primeiro install continua
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
            List<String> files = SuHelper.listFiles(SuHelper.modsDir(mPkg));
            Map<String, ModInfo> map = new LinkedHashMap<>();

            // Crashguard (F1d): leitura antes da lista, para o aviso aparecer
            // junto com a lista de mods que ele protege.
            final CrashGuardState.State cg = SuHelper.isRootAvailable()
                    ? SuHelper.readCrashGuard(mPkg)
                    : CrashGuardState.parse(null, false);
            final boolean cgMostrar = cg.marker;

            // 1. Encontra todos os arquivos de mod (.so, .bpatch, .off)
            boolean gadgetSoFound = false;
            for (String f : files) {
                if (f.equals("u_dump.so") || f.equals("u_patch.so")) {
                    // Arquivos do motor interno do sistema
                    continue;
                }
                if (f.equals("frida-gadget.so") || f.equals("libfrida-gadget.so")) {
                    // Garantia (c): o gadget como .so NAO entra na lista (nem
                    // com toggle): o loader da dlopen nele sem config e o jogo
                    // trava esperando cliente. O lugar certo e frida-gadget.bin
                    // + frida-gadget.config, que o botao de instalar faz.
                    gadgetSoFound = true;
                    continue;
                }

                // O corte do nome mora em ModInventory.parseModFileName (puro,
                // testado no host) e não aqui. A versão que estava aqui cortava
                // o id com números soltos: "- 6" para ".bpatch" (7 letras) e
                // "- 10" para ".bpatch.off" (11). "t1.bpatch" virava id "t1.", e
                // o nome que o Manager montava de volta não existia no aparelho.
                ModInventory.ModFile modFile = ModInventory.parseModFileName(f);
                String baseId = modFile == null ? null : modFile.id();
                boolean enabled = modFile == null || modFile.enabled();
                String type = modFile == null ? "patch" : modFile.type();

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
            // Um `su` só: o conteúdo de todos os <id>.json deste jogo, com
            // separador (SuHelper.readTextFiles). Antes era um readTextFile
            // por mod — 30 mods = 30 processos su.
            List<String> jsonNames = new ArrayList<>();
            for (String f : files) {
                if (f.endsWith(".json")) jsonNames.add(f);
            }
            Map<String, String> jsonBundle = ModInventory.parseBundle(
                    SuHelper.readTextFiles(SuHelper.modsDir(mPkg), jsonNames),
                    SuHelper.BUNDLE_SEP);

            for (Map.Entry<String, ModInfo> entry : map.entrySet()) {
                String id = entry.getKey();
                ModInfo info = entry.getValue();

                if (files.contains(id + ".conf")) {
                    info.hasConf = true;
                }

                if (files.contains(id + ".json")) {
                    // O conteúdo vem do PACOTE abaixo: uma única chamada root
                    // para todos os .json do jogo, em vez de um `su` por mod
                    // (mesmo bug de orçamento da tela de jogos, em escala menor — sem o `su` por mod.
                    String json = jsonBundle.get(id + ".json");
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

            final boolean temGadgetSo = gadgetSoFound;
            mMainHandler.post(() -> {
                if (!UiLiveness.alive(this)) return;
                if (cgMostrar) {
                    mTvCrashGuard.setText(CrashGuardState.describe(cg, System.currentTimeMillis() / 1000L));
                    mCrashGuardBox.setVisibility(View.VISIBLE);
                } else {
                    mCrashGuardBox.setVisibility(View.GONE);
                }
                mMods.clear();
                mMods.addAll(result);
                mAdapter.notifyDataSetChanged();
                mTvEmptyMods.setVisibility(mMods.isEmpty() ? View.VISIBLE : View.GONE);
                if (temGadgetSo) {
                    new AlertDialog.Builder(this)
                            .setTitle("frida-gadget no lugar errado")
                            .setMessage("Tem um frida-gadget.so nesta pasta. Ele NAO e um mod: o jogo "
                                    + "vai travar esperando um PC conectar, porque o loader abre "
                                    + "qualquer .so sem o frida-gadget.config.\n\n"
                                    + "Apague o frida-gadget.so e toque em '+ Instalar mod' escolhendo "
                                    + "o binario do gadget: ele volta como frida-gadget.bin (sem .so) "
                                    + "com o frida-gadget.config, e seus .js rodam sem porta.")
                            .setPositiveButton("OK", null)
                            .show();
                }
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
                                mMainHandler.post(() -> {
                                    if (!UiLiveness.alive(GameDetailActivity.this)) return;
                                    loadMods();
                                });
                            }).start();
                        })
                        .setNegativeButton(R.string.btn_cancel, null)
                        .show();
            });

            return convertView;
        }
    }
}
