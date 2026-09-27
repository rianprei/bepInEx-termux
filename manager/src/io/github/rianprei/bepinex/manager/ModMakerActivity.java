package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.BmodInstaller;
import io.github.rianprei.bepinex.manager.core.DumpParser;
import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.core.PatchGenerator;
import io.github.rianprei.bepinex.manager.core.ScanFlow;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.core.UiLiveness;
import io.github.rianprei.bepinex.manager.model.DumpEntry;
import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class ModMakerActivity extends Activity implements UiLiveness.ActivityLike {
    private static final int PAGE_SIZE = 25;

    private String mPkg;
    private String mAppName;
    private String mEngine;

    private TextView mTvGame;
    private TextView mTvScannerStatus;
    private Button mBtnScan;
    private Button mBtnCheckDump;

    private EditText mEtSearch;
    private Button mBtnDoSearch;
    private Button mBtnPrevPage;
    private Button mBtnNextPage;
    private TextView mTvPageInfo;
    private LinearLayout mContainerDumpResults;
    private TextView mTvDumpEmpty;

    private TextView mTvRulesCount;
    private LinearLayout mContainerRules;

    private EditText mEtModId;
    private EditText mEtModName;
    private EditText mEtModDesc;
    private Button mBtnSaveInstall;
    private Button mBtnExportBmod;

    private int mCurrentPage = 0;
    private boolean mHasMorePages = false;
    private final List<PatchRule> mCurrentRules = new ArrayList<>();
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private File mLocalDumpFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_mod_maker);

        mPkg = getIntent().getStringExtra("pkg");
        mAppName = getIntent().getStringExtra("name");
        mEngine = getIntent().getStringExtra("engine");

        if (mPkg == null) {
            finish();
            return;
        }

        mLocalDumpFile = new File(getCacheDir(), "dump_" + mPkg + ".tsv");

        mTvGame = findViewById(R.id.mm_tv_game);
        mTvScannerStatus = findViewById(R.id.mm_tv_scanner_status);
        mBtnScan = findViewById(R.id.mm_btn_scan);
        mBtnCheckDump = findViewById(R.id.mm_btn_check_dump);

        mEtSearch = findViewById(R.id.mm_et_search);
        mBtnDoSearch = findViewById(R.id.mm_btn_do_search);
        mBtnPrevPage = findViewById(R.id.mm_btn_prev_page);
        mBtnNextPage = findViewById(R.id.mm_btn_next_page);
        mTvPageInfo = findViewById(R.id.mm_tv_page_info);
        mContainerDumpResults = findViewById(R.id.mm_container_dump_results);
        mTvDumpEmpty = findViewById(R.id.mm_tv_dump_empty);

        mTvRulesCount = findViewById(R.id.mm_tv_rules_count);
        mContainerRules = findViewById(R.id.mm_container_rules);

        mEtModId = findViewById(R.id.mm_et_mod_id);
        mEtModName = findViewById(R.id.mm_et_mod_name);
        mEtModDesc = findViewById(R.id.mm_et_mod_desc);
        mBtnSaveInstall = findViewById(R.id.mm_btn_save_install);
        mBtnExportBmod = findViewById(R.id.mm_btn_export_bmod);

        mTvGame.setText("Jogo: " + (mAppName != null ? mAppName : mPkg) + " (" + mPkg + ")");

        // Sugestao de ID baseada no pacote
        String simplePkg = mPkg.contains(".") ? mPkg.substring(mPkg.lastIndexOf('.') + 1) : mPkg;
        mEtModId.setText(simplePkg + "-mod");

        mBtnScan.setOnClickListener(v -> scanGame());
        mBtnCheckDump.setOnClickListener(v -> checkAndSyncDump(true));

        mBtnDoSearch.setOnClickListener(v -> {
            mCurrentPage = 0;
            executeSearch();
        });

        mBtnPrevPage.setOnClickListener(v -> {
            if (mCurrentPage > 0) {
                mCurrentPage--;
                executeSearch();
            }
        });

        mBtnNextPage.setOnClickListener(v -> {
            if (mHasMorePages) {
                mCurrentPage++;
                executeSearch();
            }
        });

        mBtnSaveInstall.setOnClickListener(v -> saveAndInstallMod());
        mBtnExportBmod.setOnClickListener(v -> exportBmod());

        checkAndSyncDump(false);
    }

    private boolean hasAsset(String filename) {
        try (InputStream is = getAssets().open(filename)) {
            return is.available() >= 0;
        } catch (Exception e) {
            return false;
        }
    }

    // 1. Escanear Jogo com u_dump.so
    private void scanGame() {
        String activityComponent = LaunchActivityResolver.resolve(getPackageManager(), mPkg);
        if (activityComponent == null) {
            mTvScannerStatus.setText(LaunchActivityResolver.NOT_FOUND_MESSAGE);
            Toast.makeText(this, LaunchActivityResolver.NOT_FOUND_MESSAGE, Toast.LENGTH_LONG).show();
            return;
        }
        if (!hasAsset("u_dump.so")) {
            new AlertDialog.Builder(this)
                    .setTitle("Componente Ausente")
                    .setMessage(getString(R.string.component_missing_udump))
                    .setPositiveButton("OK", null)
                    .show();
            return;
        }

        mTvScannerStatus.setText("Injetando u_dump.so e reiniciando o jogo...");

        new Thread(() -> {
            File tmpSo = new File(getCacheDir(), "u_dump.so");
            try {
                ScanFlow.run(name -> getAssets().open(name), new ScanFlow.Device() {
                    public boolean ensureModDir() { return SuHelper.ensureModDir(mPkg); }
                    public boolean install(String local, String ignored) {
                        return SuHelper.installFile(local, "/data/local/tmp/mods/" + mPkg + "/u_dump.so", "755");
                    }
                    public boolean deleteDump() { return SuHelper.deleteDump(mPkg); }
                    public boolean restartGame() { return SuHelper.restartGame(mPkg, activityComponent); }
                    public boolean dumpReady() {
                        return SuHelper.readDump(mPkg) != null;
                    }
                    public boolean removeScanner() {
                        return SuHelper.deleteFile("/data/local/tmp/mods/" + mPkg + "/u_dump.so");
                    }
                    public void sleep(long millis) throws InterruptedException { Thread.sleep(millis); }
                }, tmpSo);
                mMainHandler.post(() -> {
                    mTvScannerStatus.setText("Scanner concluído: dump.tsv gerado.");
                    Toast.makeText(this, "Scanner concluído.", Toast.LENGTH_LONG).show();
                });
                mMainHandler.post(() -> checkAndSyncDump(false));
            } catch (Exception e) {
                mMainHandler.post(() -> {
                    mTvScannerStatus.setText("Erro ao escanear: " + e.getMessage());
                });
            }
        }).start();
    }

    // Verifica se dump.tsv existe no device e sincroniza com o cache local
    private void checkAndSyncDump(boolean showToast) {
        mTvScannerStatus.setText("Verificando dump.tsv no dispositivo...");
        new Thread(() -> {
            String remoteDumpPath = "/data/data/" + mPkg + "/files/bepinex/dump.tsv";
            SuHelper.Result r = SuHelper.exec(ScanFlow.dumpProbeCommand(remoteDumpPath));
            boolean exists = r.success && !r.stdout.contains("missing");

            if (exists) {
                // Copia para o cache local do app
                SuHelper.copyFile(remoteDumpPath, mLocalDumpFile.getAbsolutePath(), "666");
                SuHelper.exec("chmod 666 '" + mLocalDumpFile.getAbsolutePath() + "' 2>/dev/null");

                mMainHandler.post(() -> {
                    mTvScannerStatus.setText("dump.tsv pronto! (" + r.stdout.trim() + ")");
                    mTvScannerStatus.setTextColor(Color.parseColor("#22C55E"));
                    if (showToast) Toast.makeText(this, "dump.tsv sincronizado!", Toast.LENGTH_SHORT).show();
                    mCurrentPage = 0;
                    executeSearch();
                });
            } else {
                mMainHandler.post(() -> {
                    mTvScannerStatus.setText("dump.tsv não encontrado em /data/data/" + mPkg + "/files/bepinex/dump.tsv. Toque em 'Escanear Jogo'.");
                    mTvScannerStatus.setTextColor(Color.parseColor("#EAB308"));
                    if (showToast) Toast.makeText(this, "dump.tsv ainda não foi gerado.", Toast.LENGTH_SHORT).show();
                });
            }
        }).start();
    }

    // 2. Busca paginada no Dump local (100k+ linhas via streaming sem OOM)
    private void executeSearch() {
        if (!mLocalDumpFile.exists()) {
            mTvDumpEmpty.setText("Nenhum dump carregado. Execute o scanner primeiro.");
            mTvDumpEmpty.setVisibility(View.VISIBLE);
            mContainerDumpResults.removeAllViews();
            return;
        }

        String query = mEtSearch.getText().toString().trim();
        int offset = mCurrentPage * PAGE_SIZE;

        new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    new FileInputStream(mLocalDumpFile), StandardCharsets.UTF_8))) {

                DumpParser.SearchResult result = DumpParser.search(reader, query, null, offset, PAGE_SIZE);

                mMainHandler.post(() -> {
                    mHasMorePages = result.hasMore;
                    mTvPageInfo.setText("Página " + (mCurrentPage + 1));
                    mBtnPrevPage.setEnabled(mCurrentPage > 0);
                    mBtnNextPage.setEnabled(mHasMorePages);

                    mContainerDumpResults.removeAllViews();
                    if (result.entries.isEmpty()) {
                        mTvDumpEmpty.setText("Nenhum resultado encontrado para '" + query + "'.");
                        mTvDumpEmpty.setVisibility(View.VISIBLE);
                    } else {
                        mTvDumpEmpty.setVisibility(View.GONE);
                        for (DumpEntry entry : result.entries) {
                            addDumpItemView(entry);
                        }
                    }
                });
            } catch (Exception e) {
                mMainHandler.post(() -> {
                    mTvDumpEmpty.setText("Erro ao ler dump: " + e.getMessage());
                    mTvDumpEmpty.setVisibility(View.VISIBLE);
                });
            }
        }).start();
    }

    private void addDumpItemView(DumpEntry entry) {
        View v = LayoutInflater.from(this).inflate(R.layout.item_dump_entry, mContainerDumpResults, false);

        TextView tvKind = v.findViewById(R.id.dump_tv_kind);
        TextView tvClass = v.findViewById(R.id.dump_tv_class);
        TextView tvMember = v.findViewById(R.id.dump_tv_member);
        Button btnAdd = v.findViewById(R.id.dump_btn_add_rule);

        tvKind.setText(entry.kind);
        tvClass.setText(entry.className);

        if (DumpEntry.KIND_METHOD.equals(entry.kind)) {
            tvMember.setText((entry.isStatic ? "static " : "") + entry.name + "(" + entry.nargs + " args) : " + entry.type);
        } else if (DumpEntry.KIND_FIELD.equals(entry.kind)) {
            tvMember.setText((entry.isStatic ? "static " : "") + entry.name + " : " + entry.type);
        } else {
            tvMember.setText(entry.assembly != null ? entry.assembly : "Classe");
        }

        btnAdd.setOnClickListener(view -> showCreateRuleDialog(entry));

        mContainerDumpResults.addView(v);
    }

    // Dialogo para configurar regra de patch (Contrato C4)
    private void showCreateRuleDialog(DumpEntry entry) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("Criar Regra: " + entry.name);

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 16, 32, 16);

        TextView info = new TextView(this);
        info.setText("Classe: " + entry.className + "\nMembro: " + entry.name + " (" + entry.type + ")");
        info.setTextColor(Color.parseColor("#94A3B8"));
        layout.addView(info);

        final RadioGroup rg = new RadioGroup(this);
        rg.setOrientation(RadioGroup.VERTICAL);
        rg.setPadding(0, 16, 0, 16);

        final EditText etValue = new EditText(this);
        etValue.setTextColor(Color.parseColor("#F8FAFC"));
        etValue.setBackgroundResource(R.drawable.card_background);
        etValue.setPadding(8, 8, 8, 8);
        etValue.setText("true");

        boolean isBoolean = (entry.type != null && (entry.type.contains("Boolean") || entry.type.equalsIgnoreCase("bool")));
        boolean isNumeric = (entry.type != null && (entry.type.contains("Int") || entry.type.contains("Single") || entry.type.contains("Double") || entry.type.equalsIgnoreCase("float")));

        // Só o verbo field (campo de instância) precisa de método + nargs.
        FieldInputs fieldInputs = null;

        if (DumpEntry.KIND_METHOD.equals(entry.kind)) {
            if (isBoolean) {
                RadioButton rbTrue = new RadioButton(this);
                rbTrue.setText("Sempre Verdadeiro (true)");
                rbTrue.setId(View.generateViewId());
                rbTrue.setChecked(true);
                rg.addView(rbTrue);

                RadioButton rbFalse = new RadioButton(this);
                rbFalse.setText("Sempre Falso (false)");
                rbFalse.setId(View.generateViewId());
                rg.addView(rbFalse);

                etValue.setVisibility(View.GONE);
            } else if (isNumeric) {
                RadioButton rbFixed = new RadioButton(this);
                rbFixed.setText("Sempre Retornar Valor Fixo");
                rbFixed.setId(View.generateViewId());
                rbFixed.setChecked(true);
                rg.addView(rbFixed);

                RadioButton rbMul = new RadioButton(this);
                rbMul.setText("Multiplicar Retorno por Fator");
                rbMul.setId(View.generateViewId());
                rg.addView(rbMul);

                etValue.setHint("Ex: 10 ou 2.5");
                etValue.setText("2");
                etValue.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
            } else {
                RadioButton rbFixed = new RadioButton(this);
                rbFixed.setText("Retorno Fixo");
                rbFixed.setChecked(true);
                rg.addView(rbFixed);
            }
        } else if (DumpEntry.KIND_FIELD.equals(entry.kind)) {
            etValue.setHint("Valor (ex: true, 10, 999)");
            etValue.setText(isBoolean ? "true" : "999");
            fieldInputs = buildFieldInputs(rg, entry);
        } else {
            Toast.makeText(this, "Selecione um método ou campo para aplicar regra.", Toast.LENGTH_SHORT).show();
            return;
        }

        layout.addView(rg);
        layout.addView(etValue);
        if (fieldInputs != null) {
            layout.addView(fieldInputs.method);
            layout.addView(fieldInputs.nargs);
            layout.addView(fieldInputs.note);
        }
        builder.setView(layout);

        final FieldInputs fi = fieldInputs;
        builder.setPositiveButton("Adicionar Regra", (dialog, which) -> {
            String valType = isBoolean ? "bool" : (isNumeric ? "float" : "int");
            PatchRule rule;

            if (DumpEntry.KIND_METHOD.equals(entry.kind)) {
                if (isBoolean) {
                    int checkedId = rg.getCheckedRadioButtonId();
                    RadioButton selected = rg.findViewById(checkedId);
                    String val = (selected != null && selected.getText().toString().contains("false")) ? "false" : "true";
                    rule = PatchRule.makeReturn(entry.className, entry.name, entry.nargs, "bool", val);
                } else if (isNumeric) {
                    int checkedId = rg.getCheckedRadioButtonId();
                    RadioButton selected = rg.findViewById(checkedId);
                    boolean isMul = (selected != null && selected.getText().toString().contains("Multiplicar"));
                    String val = etValue.getText().toString().trim();
                    if (val.isEmpty()) val = isMul ? "2" : "1";

                    if (isMul) {
                        rule = PatchRule.makeMul(entry.className, entry.name, entry.nargs, valType, val);
                    } else {
                        rule = PatchRule.makeReturn(entry.className, entry.name, entry.nargs, valType, val);
                    }
                } else {
                    rule = PatchRule.makeReturn(entry.className, entry.name, entry.nargs, "int", "0");
                }
            } else if (entry.isStatic) {
                String val = etValue.getText().toString().trim();
                rule = PatchRule.makeStatic(entry.className, entry.name, valType, val);
            } else {
                // Campo de instância: verbo field do C4. Sem método escolhido,
                // o u_patch escolhe sozinho (regra de 4 campos).
                String val = etValue.getText().toString().trim();
                boolean withMethod = (fi != null && rg.getCheckedRadioButtonId() == fi.methodRadioId);
                if (withMethod) {
                    String method = fi.method.getText().toString().trim();
                    if (method.isEmpty()) {
                        Toast.makeText(this, "Informe o método de instância, ou escolha 'deixar o u_patch escolher'.", Toast.LENGTH_LONG).show();
                        return;
                    }
                    int nargs;
                    try {
                        nargs = Integer.parseInt(fi.nargs.getText().toString().trim());
                    } catch (NumberFormatException e) {
                        nargs = -1;
                    }
                    if (nargs < 0) {
                        Toast.makeText(this, "nargs inválido: use um número >= 0 (0 = método sem argumentos).", Toast.LENGTH_LONG).show();
                        return;
                    }
                    rule = PatchRule.makeField(entry.className, entry.name, valType, val, method, nargs);
                } else {
                    rule = PatchRule.makeField(entry.className, entry.name, valType, val);
                }
            }

            mCurrentRules.add(rule);
            updateRulesListUi();
            Toast.makeText(this, "Regra adicionada!", Toast.LENGTH_SHORT).show();
        });

        builder.setNegativeButton("Cancelar", null);
        builder.show();
    }

    // Entradas extras do verbo field (C4) para campo de INSTANCIA: metodo +
    // nargs + aviso honesto. null quando o campo é estático (verbo static).
    private static final class FieldInputs {
        int methodRadioId;
        EditText method;
        EditText nargs;
        TextView note;
    }

    private FieldInputs buildFieldInputs(RadioGroup rg, DumpEntry entry) {
        FieldInputs fi = new FieldInputs();

        if (entry.isStatic) {
            RadioButton rbStatic = new RadioButton(this);
            rbStatic.setText("Fixar Valor do Campo Estático (static)");
            rbStatic.setChecked(true);
            rg.addView(rbStatic);
            return fi;
        }

        RadioButton rbMethod = new RadioButton(this);
        rbMethod.setText("Fixar o campo a cada chamada de um método (field)");
        rbMethod.setId(View.generateViewId());
        rbMethod.setChecked(true);
        rg.addView(rbMethod);
        fi.methodRadioId = rbMethod.getId();

        RadioButton rbAuto = new RadioButton(this);
        rbAuto.setText("Deixar o u_patch escolher o método (field sem método)");
        rbAuto.setId(View.generateViewId());
        rg.addView(rbAuto);

        fi.method = new EditText(this);
        fi.method.setTextColor(Color.parseColor("#F8FAFC"));
        fi.method.setBackgroundResource(R.drawable.card_background);
        fi.method.setPadding(8, 8, 8, 8);
        fi.method.setHint("Método de instância da mesma classe (ex: Update)");
        fi.method.setSingleLine(true);

        fi.nargs = new EditText(this);
        fi.nargs.setTextColor(Color.parseColor("#F8FAFC"));
        fi.nargs.setBackgroundResource(R.drawable.card_background);
        fi.nargs.setPadding(8, 8, 8, 8);
        fi.nargs.setHint("nargs do método (0 = sem argumentos)");
        fi.nargs.setText("0");
        fi.nargs.setSingleLine(true);

        fi.note = new TextView(this);
        fi.note.setText("field " + entry.className + " " + entry.name + " <tipo> <valor> <Método> <nargs>\n"
                + "O campo é reescrito a cada chamada do método. Atenção: se o jogo também decrementa/usa o campo "
                + "direto (sem passar pelo método), o método patchado não segura o valor. "
                + "O verbo field está no contrato C4 mas o u_patch (F4) ainda não o implementa: a regra é gerada e "
                + "salva, e só passa a valer quando o u_patch suportar 'field'.");
        fi.note.setTextColor(Color.parseColor("#94A3B8"));
        fi.note.setTextSize(11f);
        fi.note.setPadding(0, 8, 0, 0);

        final EditText methodBox = fi.method;
        final EditText nargsBox = fi.nargs;
        rg.setOnCheckedChangeListener((group, checkedId) -> {
            boolean withMethod = (checkedId == fi.methodRadioId);
            methodBox.setVisibility(withMethod ? View.VISIBLE : View.GONE);
            nargsBox.setVisibility(withMethod ? View.VISIBLE : View.GONE);
        });
        return fi;
    }

    private void updateRulesListUi() {
        mTvRulesCount.setText("3. Regras Criadas (" + mCurrentRules.size() + ")");
        mContainerRules.removeAllViews();

        for (int i = 0; i < mCurrentRules.size(); i++) {
            final int index = i;
            PatchRule rule = mCurrentRules.get(i);

            View v = LayoutInflater.from(this).inflate(R.layout.item_patch_rule, mContainerRules, false);
            TextView tvRule = v.findViewById(R.id.rule_tv_text);
            Button btnRemove = v.findViewById(R.id.rule_btn_remove);

            tvRule.setText(rule.toLine());
            btnRemove.setOnClickListener(view -> {
                mCurrentRules.remove(index);
                updateRulesListUi();
            });

            mContainerRules.addView(v);
        }
    }

    // 4. Salvar e Instalar Mod Declarativo (.patch + manifest)
    private void saveAndInstallMod() {
        if (mCurrentRules.isEmpty()) {
            Toast.makeText(this, "Adicione ao menos uma regra antes de salvar.", Toast.LENGTH_LONG).show();
            return;
        }

        String modId = mEtModId.getText().toString().trim();
        String modName = mEtModName.getText().toString().trim();
        String modDesc = mEtModDesc.getText().toString().trim();

        if (modId.isEmpty() || !modId.matches("^[a-z0-9-]{3,48}$")) {
            Toast.makeText(this, "ID inválido! Deve ter 3 a 48 caracteres (apenas letras minúsculas, números e traço).", Toast.LENGTH_LONG).show();
            return;
        }

        if (modName.isEmpty()) {
            Toast.makeText(this, "Digite um nome para o mod.", Toast.LENGTH_SHORT).show();
            return;
        }

        ModManifest manifest = new ModManifest();
        manifest.format = 1;
        manifest.id = modId;
        manifest.name = modName;
        manifest.description = modDesc;
        manifest.author = "Mod Maker";
        manifest.game = mPkg;
        manifest.engine = mEngine != null ? mEngine : "unity-il2cpp";
        manifest.type = "patch";

        String patchContent = PatchGenerator.format(mCurrentRules);
        String manifestJson = ManifestParser.toJson(manifest);

        new Thread(() -> {
            String dir = "/data/local/tmp/mods/" + mPkg + "/";
            SuHelper.ensureModDir(mPkg);

            SuHelper.writeTextFile(dir + modId + ".patch", patchContent);
            SuHelper.writeTextFile(dir + modId + ".json", manifestJson);

            // Checa u_patch.so em assets/
            boolean hasUpatch = hasAsset("u_patch.so");
            String missingWarning = "";
            if (hasUpatch) {
                try {
                    File tmpSo = new File(getCacheDir(), "u_patch.so");
                    try (InputStream is = getAssets().open("u_patch.so");
                         FileOutputStream fos = new FileOutputStream(tmpSo)) {
                        byte[] buf = new byte[4096];
                        int n;
                        while ((n = is.read(buf)) != -1) fos.write(buf, 0, n);
                    }
                    SuHelper.installFile(tmpSo.getAbsolutePath(), dir + "u_patch.so", "755");
                    tmpSo.delete();
                } catch (Exception ignored) {}
            } else {
                missingWarning = "\n\nNota: " + getString(R.string.component_missing_upatch);
            }

            final String finalWarning = missingWarning;
            mMainHandler.post(() -> {
                // Callback longo (su): a tela pode ter morrido (rotação) —
                // dialog em Activity destruída é BadTokenException.
                if (!UiLiveness.alive(this)) return;
                new AlertDialog.Builder(this)
                        .setTitle("Mod Salvo!")
                        .setMessage("O mod '" + modName + "' foi salvo e ativado para " + mPkg + "!" + finalWarning)
                        .setPositiveButton("OK", (dialog, which) -> finish())
                        .show();
            });
        }).start();
    }

    // Exportar pacote .bmod para a pasta Download/
    private void exportBmod() {
        if (mCurrentRules.isEmpty()) {
            Toast.makeText(this, "Adicione ao menos uma regra antes de exportar.", Toast.LENGTH_LONG).show();
            return;
        }

        String modId = mEtModId.getText().toString().trim();
        String modName = mEtModName.getText().toString().trim();
        String modDesc = mEtModDesc.getText().toString().trim();

        if (modId.isEmpty() || !modId.matches("^[a-z0-9-]{3,48}$")) {
            Toast.makeText(this, "ID inválido!", Toast.LENGTH_SHORT).show();
            return;
        }

        ModManifest manifest = new ModManifest();
        manifest.format = 1;
        manifest.id = modId;
        manifest.name = modName.isEmpty() ? modId : modName;
        manifest.description = modDesc;
        manifest.author = "Mod Maker";
        manifest.game = mPkg;
        manifest.engine = mEngine != null ? mEngine : "unity-il2cpp";
        manifest.type = "patch";

        String patchContent = PatchGenerator.format(mCurrentRules);

        new Thread(() -> {
            try {
                File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                if (!downloadDir.exists()) downloadDir.mkdirs();

                File bmod = BmodInstaller.createBmod(manifest, patchContent, true, downloadDir);

                mMainHandler.post(() -> {
                    if (!UiLiveness.alive(this)) return;
                    new AlertDialog.Builder(this)
                            .setTitle("Mod Exportado!")
                            .setMessage("Arquivo criado com sucesso:\n\n" + bmod.getAbsolutePath())
                            .setPositiveButton("OK", null)
                            .show();
                });
            } catch (Exception e) {
                mMainHandler.post(() -> {
                    Toast.makeText(this, "Erro ao exportar: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }
}
