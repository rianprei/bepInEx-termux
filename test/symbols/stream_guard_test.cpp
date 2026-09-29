// test/symbols/stream_guard_test.cpp — o caminho QUENTE do socket compartilhado.
//
// O que este teste mede, e por que cada parte existe:
//
//   (A) Com o mutex do pedido EM CURSO, bc_stream_try_send() tem que VOLTAR
//       (nao esperar) e CONTAR o descarte. A thread que chama e a thread do
//       JOGO: se ela esperar, o jogo trava ate 5s, sem log e sem crash — o
//       pior modo de falha possível, e pior que o mod não carregar.
//
//   (B) [estático] O android_dlopen_ext() não pode rodar com o mutex tomado:
//       o construtor do mod pode disparar um hook que chama o streaming na
//       MESMA thread, e o relock de um mutex não-recursivo pela mesma thread
//       trava. Verificado sobre o texto de jni/main.cpp.
//
// ARMADILHA QUE JÁ CUSTOU 10 MINUTOS AQUI: try_lock() num mutex não-recursivo
// que a MESMA thread já segura é comportamento indefinido em glibc, e TRAVA.
// Por isso o dono do mutex aqui é SEMPRE outra thread. A primeira versão deste
// arquivo media exatamente o undefined e pendurou.
//
// A SABOTAGEM (trocar trylock por lock no header) faz o caminho quente ESPERAR
// o pedido terminar. O alarm(2) abaixo estoura e o teste FALHA em vez de
// pendurar o gate.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <thread>
#include <string>
#include <vector>
#include <unistd.h>

#include "../../jni/bc_stream_guard.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

using clock = std::chrono::steady_clock;
static long ms_since(clock::time_point t0) {
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
}

static void on_alarm(int) {
    static const char msg[] = "  [FAIL] ALARME: o caminho quente ESPEROU o mutex (trylock virou lock)\n";
    ssize_t r = write(2, msg, sizeof(msg) - 1);
    (void)r;
    _exit(3);
}

// O send() e injetado, entao nao ha socket: e a MESMA funcao que roda no
// aparelho, com o mutex e o contador reais do helper.
static std::atomic<int> g_envios{0};
static int fake_send(void *ctx, const void *data, size_t len) {
    (void)ctx; (void)data; (void)len;
    g_envios.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

}  // namespace

int main() {
    // A sabotagem faz este caminho esperar; o alarme estoura o teste em vez de
    // deixar o gate pendurado.
    signal(SIGALRM, on_alarm);
    alarm(2);

    printf("== stream_guard: o caminho quente nao espera (risco A) ==\n");

    pthread_mutex_t io = PTHREAD_MUTEX_INITIALIZER;
    std::atomic<unsigned> dropped{0};

    // --- (A) com o pedido em curso: volta rapido e conta ------------------
    {
        // O DONO E OUTRA THREAD. Ver a nota de UB acima.
        std::mutex m;
        std::condition_variable cv;
        bool tomados = false, pode_soltar = false;
        std::thread pedido([&] {
            pthread_mutex_lock(&io);
            {
                std::lock_guard<std::mutex> lk(m);
                tomados = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return pode_soltar; });
            pthread_mutex_unlock(&io);
        });

        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return tomados; });
        }

        g_envios.store(0);
        clock::time_point t0 = clock::now();
        int mandou = bc_stream_try_send(&io, &dropped, fake_send, nullptr, "x", 1);
        long d = ms_since(t0);

        char nome[140];
        snprintf(nome, sizeof(nome), "(A) com o pedido em curso volta em <10ms (medido %ldms)", d);
        check(nome, d < 10);
        check("(A) e devolveu 'perdi' sem esperar", mandou == 0);
        check("(A) e contou o descarte", dropped.load() == 1);
        check("(A) e nao mandou nada", g_envios.load() == 0);

        // --- o pedido solta: o proximo envio passa -------------------------
        {
            std::lock_guard<std::mutex> lk(m);
            pode_soltar = true;
        }
        cv.notify_all();
        pedido.join();

        g_envios.store(0);
        mandou = bc_stream_try_send(&io, &dropped, fake_send, nullptr, "x", 1);
        check("(A) depois que o pedido solta, envia normalmente", mandou != 0 && g_envios.load() == 1);
        check("(A) e nao contou descarte nessa vez", dropped.load() == 1);
    }

    // --- (A) carga: varias linhas de streaming e pedidos ao mesmo tempo -----
    {
        std::atomic<bool> parar{false};
        std::atomic<long> pior{0};
        std::atomic<long> enviados{0};
        std::vector<std::thread> linhas;
        for (int t = 0; t < 4; t++) {
            linhas.emplace_back([&] {
                while (!parar.load()) {
                    clock::time_point t0 = clock::now();
                    if (bc_stream_try_send(&io, &dropped, fake_send, nullptr, "x", 1))
                        enviados.fetch_add(1);
                    long d = ms_since(t0);
                    long atual = pior.load();
                    while (d > atual && !pior.compare_exchange_weak(atual, d)) {}
                }
            });
        }
        std::vector<std::thread> pedidos;
        for (int t = 0; t < 2; t++) {
            pedidos.emplace_back([&] {
                for (int i = 0; i < 200; i++) {
                    pthread_mutex_lock(&io);
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    pthread_mutex_unlock(&io);
                }
            });
        }
        for (auto &p : pedidos) p.join();
        parar.store(true);
        for (auto &l : linhas) l.join();

        char nome[160];
        snprintf(nome, sizeof(nome), "(A) com 2 pedidos e 4 streams, pior caso %ldms", pior.load());
        // Nenhuma linha de streaming pode esperar o pedido. O teto e o que
        // importa: 100ms ja e o que o jogo nao deve ver.
        check(nome, pior.load() < 100);
        snprintf(nome, sizeof(nome), "(A) e alguma linha foi descartada mesmo assim (%u)",
                 dropped.load());
        check(nome, dropped.load() > 0);
    }

    // --- (B) estatico: o dlopen fora do lock -------------------------------
    // O android_dlopen_ext nao pode aparecer entre um lock e o unlock
    // correspondente de g_companion_io. O comentario no codigo de producao diz o
    // mesmo; aqui e a verificacao automatica.
    {
        std::string self = __FILE__;
        size_t b = self.rfind('/');
        if (b != std::string::npos) self = self.substr(0, b);
        FILE *c = fopen((self + "/../../jni/main.cpp").c_str(), "r");
        if (c == nullptr) {
            check("abri jni/main.cpp para o check estatico do dlopen", false);
        } else {
            char buf[1048576] = {};
            size_t n = fread(buf, 1, sizeof(buf) - 1, c);
            fclose(c);
            std::string code(buf, n);
            // Procura: pthread_mutex_lock(&g_companion_io) ... android_dlopen_ext
            size_t p = 0;
            bool preso = false;
            while ((p = code.find("pthread_mutex_lock(&g_companion_io)", p)) != std::string::npos) {
                size_t unlock = code.find("pthread_mutex_unlock(&g_companion_io)", p);
                size_t dlopen = code.find("android_dlopen_ext", p);
                if (unlock != std::string::npos && dlopen != std::string::npos && dlopen < unlock)
                    preso = true;
                p += 10;
            }
            check("(B) nenhum android_dlopen_ext entre lock e unlock de g_companion_io", !preso);
        }
    }

    alarm(0);
    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
