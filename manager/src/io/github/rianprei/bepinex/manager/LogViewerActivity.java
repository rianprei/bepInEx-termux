package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import io.github.rianprei.bepinex.manager.core.SuHelper;

public class LogViewerActivity extends Activity {
    private String mPkg;
    private TextView mTvTitle;
    private TextView mTvPath;
    private TextView mTvContent;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_log_viewer);

        mPkg = getIntent().getStringExtra("pkg");
        if (mPkg == null) {
            finish();
            return;
        }

        mTvTitle = findViewById(R.id.log_tv_title);
        mTvPath = findViewById(R.id.log_tv_path);
        mTvContent = findViewById(R.id.log_tv_content);

        mTvTitle.setText("Log de Mods — " + mPkg);
        mTvPath.setText("/data/data/" + mPkg + "/files/bepinex/log.txt");

        findViewById(R.id.log_btn_refresh).setOnClickListener(v -> loadLog());

        findViewById(R.id.log_btn_clear).setOnClickListener(v -> {
            new Thread(() -> {
                boolean ok = SuHelper.clearLog(mPkg);
                mMainHandler.post(() -> {
                    if (ok) {
                        Toast.makeText(this, "Log limpo com sucesso!", Toast.LENGTH_SHORT).show();
                        loadLog();
                    } else {
                        Toast.makeText(this, "Falha ao limpar o log.", Toast.LENGTH_SHORT).show();
                    }
                });
            }).start();
        });

        loadLog();
    }

    private void loadLog() {
        mTvContent.setText("Carregando log...");
        new Thread(() -> {
            String content = SuHelper.readLog(mPkg);
            mMainHandler.post(() -> {
                if (content == null || content.trim().isEmpty()) {
                    mTvContent.setText("Nenhum log encontrado ainda em /data/data/" + mPkg + "/files/bepinex/log.txt\n" +
                            "Certifique-se de que o jogo foi iniciado com mods ativos.");
                } else {
                    mTvContent.setText(content);
                }
            });
        }).start();
    }
}
