package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.InFlightFlag;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

public final class InFlightFlagTest {
    private InFlightFlagTest() {}

    public static void run() throws InterruptedException {
        testSegundoToqueEBloqueado();
        testConcorrenciaElegeUmSo();
        System.out.println("  [OK] InFlightFlagTest (duplo toque não instala 2x)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    private static void testSegundoToqueEBloqueado() {
        InFlightFlag flag = new InFlightFlag();
        check("primeiro toque começa", flag.begin());
        check("segundo toque no mesmo trabalho é recusado", !flag.begin());
        check("em voo visível para a tela", flag.isInFlight());
        flag.end();
        check("após end, um novo trabalho pode começar", flag.begin());
        flag.end();
    }

    private static void testConcorrenciaElegeUmSo() throws InterruptedException {
        // 16 threads disputam o MESMO trabalho: exatamente 1 entra; as
        // demais são os toques repetidos que a tela agora ignora.
        final InFlightFlag flag = new InFlightFlag();
        final AtomicInteger winners = new AtomicInteger();
        final CountDownLatch done = new CountDownLatch(16);
        for (int i = 0; i < 16; i++) {
            new Thread(() -> {
                if (flag.begin()) winners.incrementAndGet();
                done.countDown();
            }).start();
        }
        done.await();
        check("16 chamadas simultâneas elegem exatamente 1", winners.get() == 1);
    }
}
