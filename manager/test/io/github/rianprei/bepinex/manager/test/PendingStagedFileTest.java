package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.PendingStagedFile;

import java.io.File;

public final class PendingStagedFileTest {
    private PendingStagedFileTest() {}

    public static void run() {
        File first = new File("staged-primeiro");
        File second = new File("staged-segundo");

        PendingStagedFile holder = new PendingStagedFile();
        check("primeiro staged entra", holder.set(first));
        // Cenário do órfão: Activity recebe outro staged com o holder ocupado
        // (install anterior ainda em voo). set=false obriga o chamador a
        // apagar o novo em vez de sobrescrever e perder o primeiro.
        check("segundo com holder ocupado é recusado (o chamador apaga o dele)",
                !holder.set(second));
        check("take devolve o primeiro e libera", holder.take() == first);
        check("holder vazio não devolve nada", holder.take() == null);

        // Reaproveite na rotação: a instância nova faz take e volta a setar.
        holder.set(second);
        check("arquivo sobrevive à troca de instância", holder.take() == second);
        System.out.println("  [OK] PendingStagedFileTest (staged nunca fica órfão)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }
}
