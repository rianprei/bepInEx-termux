package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Fluxo completo do arquivo escolhido (stage → pendente → inspect → install
 * → delete), fora da Activity. O estado vive por TELA em campos estáticos:
 * rotação recria a Activity, mas a instância nova chama of() e herda o
 * estado — flag de install e pendente NUNCA nascem vazios na recriação.
 *
 * O conjunto "em uso" (pendente de cada tela + stage em voo + install em
 * voo) é o que o sweep respeita: {@link #sweepOrphans} nunca apaga arquivo
 * que o fluxo está usando, e pula a rodada inteira enquanto um stage está
 * em voo (o diretório recém-criado ainda não é rastreável por arquivo).
 *
 * As Activities só chamam e desenham. Puro: os testes dirigem com Executor
 * falso, passo a passo.
 */
public final class SelectedFileFlow {
    public static final String MAIN = "main";
    public static final String DETAIL = "detail";

    /** O que a inspeção devolve para a tela desenhar. */
    public record Inspection(ModManifest manifest, SelectedFileRouter.Decision decision) {}

    public interface Callback<T> { void complete(T value, Exception error); }

    /** O trabalho de cópia (depende da Activity: cache dir + Uri). */
    public interface StageWork { File stage() throws Exception; }

    /** O trabalho de instalação (depende do jogo escolhido). */
    public interface InstallWork { LooseModInstaller.Result run() throws Exception; }

    private static final Map<String, Flow> SCREENS = new LinkedHashMap<>();

    /** Estado da tela; a mesma instância depois de a Activity ser recriada. */
    public static synchronized Flow of(String screen) {
        Flow flow = SCREENS.get(screen);
        if (flow == null) {
            flow = new Flow();
            SCREENS.put(screen, flow);
        }
        return flow;
    }

    /**
     * Retaguarda do órfão: apaga staging que ninguém usa. Devolve -1 quando
     * pulou a rodada (stage em voo em qualquer tela); senão, quantos
     * diretórios órfãos apagou. Pendentes e arquivos em install em voo são
     * preservados, de qualquer tela.
     */
    public static synchronized int sweepOrphans(File cacheDirectory) {
        if (anyStageInFlight()) return -1;
        List<File> keep = new ArrayList<>();
        for (Flow flow : SCREENS.values()) {
            File p = flow.pending().peek();
            if (p != null) keep.add(p);
            keep.addAll(flow.busyFiles());
        }
        return SelectedFileStager.sweep(cacheDirectory, keep.toArray(new File[0]));
    }

    public static synchronized boolean anyStageInFlight() {
        for (Flow flow : SCREENS.values()) {
            if (flow.stagesInFlight.get() > 0) return true;
        }
        return false;
    }

    public static final class Flow {
        private final AtomicInteger stagesInFlight = new AtomicInteger();
        private final InFlightFlag installFlag = new InFlightFlag();
        private final PendingStagedFile pending = new PendingStagedFile();
        private final Set<File> busy = ConcurrentHashMap.newKeySet();

        private Flow() {}

        /** Pendente da tela (a instância recriada herda daqui). */
        public PendingStagedFile pending() { return pending; }

        public boolean installInFlight() { return installFlag.isInFlight(); }

        /** Arquivos desta tela em uso por stage/install em voo. */
        public List<File> busyFiles() { return new ArrayList<>(busy); }

        /** Marca arquivo em uso sem passar pelo fluxo (ex.: staging de terceiros). */
        public void protect(File file) {
            if (file != null) busy.add(file);
        }

        /**
         * Cópia do arquivo escolhido. O staged fica "em uso" desde que passa
         * a existir (o sweep não apaga) até consume(). Callback na ui.
         */
        public void stage(Executor worker, Executor ui, StageWork work, Callback<File> callback) {
            stagesInFlight.incrementAndGet();
            worker.execute(() -> {
                File staged = null;
                Exception error = null;
                try {
                    staged = work.stage();
                    if (staged != null) busy.add(staged);
                } catch (Exception e) {
                    error = e;
                } finally {
                    stagesInFlight.decrementAndGet();
                }
                final File result = staged;
                final Exception e = error;
                ui.execute(() -> callback.complete(result, e));
            });
        }

        /** Inspeciona por conteúdo (detect + manifest + roteamento). Callback na ui. */
        public void inspect(Executor worker, Executor ui, File file, Callback<Inspection> callback) {
            worker.execute(() -> {
                Inspection inspection = null;
                Exception error = null;
                try {
                    ModContentDetector.Detection detection = ModContentDetector.detect(
                            LooseModInstaller.probe(file), true);
                    ModManifest manifest = detection.kind == ModContentDetector.Kind.BMOD
                            ? BmodInstaller.inspect(file) : null;
                    inspection = new Inspection(manifest,
                            SelectedFileRouter.decide(detection, manifest));
                } catch (Exception e) {
                    error = e;
                }
                final Inspection result = inspection;
                final Exception e = error;
                ui.execute(() -> callback.complete(result, e));
            });
        }

        /**
         * Instala. false = já há install em voo NESTA tela (o segundo toque
         * é ignorado pela MESMA API que a Activity chama — a flag vive no
         * estado da tela, não no método). O arquivo fica "em uso" até o
         * callback ser entregue.
         */
        public boolean install(Executor worker, Executor ui, File file, InstallWork work,
                               Callback<LooseModInstaller.Result> callback) {
            if (!installFlag.begin()) return false;
            busy.add(file);
            worker.execute(() -> {
                LooseModInstaller.Result result = null;
                Exception error = null;
                try {
                    result = work.run();
                } catch (Exception e) {
                    error = e;
                }
                final LooseModInstaller.Result r = result;
                final Exception e = error;
                ui.execute(() -> {
                    busy.remove(file);
                    installFlag.end();
                    callback.complete(r, e);
                });
            });
            return true;
        }

        /** O staged saiu de circulação (instalou, falhou ou foi cancelado): apaga. */
        public void consume(File file) {
            if (file == null) return;
            busy.remove(file);
            SelectedFileStager.delete(file);
        }
    }
}
