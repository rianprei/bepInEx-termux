// thunk_exec — teste de EXECUÇÃO real do thunk mul do u_patch (arm64).
// Roda no device (adb push + exec) e no host via qemu-aarch64 (ver README).
// A estrutura do slot tem que bater com up_slot_t do u_patch_mod.cpp:
// orig@0 / value@8 / lr@16 / lock@24 (UP_SLOT_* de u_patch_arm64.h).
//
// Exit 0 = tudo ok. O que prova:
//   - mul int e float retornam orig*fator;
//   - x9-x17 sujos pelo orig não quebram o thunk (caller-saved de verdade);
//   - recursão através do fast path (lock ocupado) retorna certo, sem
//     self-loop (x30 do chamador intocado);
//   - 8 threads x 100k chamadas concorrentes: resultado sempre correto,
//     sem crash, sem hang (alarm(120) mata hang); lock == 0 no fim.
//
// Modo de falha (argv[1] = fixed|buga|bugb|bugb2): corrompe DE PROPÓSITO
// as palavras emitidas pra PROVAR que cada bug reintroduzido falha o teste:
//   buga:  2º 'add x17,x16,#24' vira NOP -> unlock (stlr) usa x17 morto
//          pós-call (sujado pelo orig) -> endereço lixo: lock fica preso
//          pra sempre (SIGALRM do alarm) ou SEGSEGV no stlr.
//   bugb:  troca restore-lr <-> unlock (ordem errada real). No DEVICE
//          (SMP verdadeiro) outra thread adquire o lock e sobrescreve
//          slot.lr dentro da janela -> retorno pra endereço errado.
//          No qemu-user NÃO falha (TCG é atômico entre instruções do
//          mesmo bloco — a janela nunca perde a corrida; ver README).
//   bugb2: restore de x30 vira NOP (o que a corrida do bugb causa na
//          prática: x30 lido nunca é o certo) -> ret usa o x30 que o blr
//          gravou (endereço DENTRO do thunk) -> volta pro meio do próprio
//          thunk = loop infinito/crash. Prova determinística no host.
// Em todos, exit != 0 com o run 'fixed' verde = bug pego pelo teste.
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

// Header é C++ (headers-only com lambdas); C chama via C++ shim.
long  up_emit_mul_thunk_c(uint32_t *out, const void *thunk_va,
                          const void *slot_va, int is_float);
int   up_mul_thunk_words_c(void);

// Offsets do slot (igual u_patch_mod.cpp / u_patch_arm64.h).
#define SLOT_ORIG  0
#define SLOT_VALUE 8
#define SLOT_LR    16
#define SLOT_LOCK  24
#define SLOT_SIZE  32

static uint8_t slot[SLOT_SIZE]  __attribute__((aligned(16)));   // int
static uint8_t slotf[SLOT_SIZE] __attribute__((aligned(16)));   // float
static void *g_thunk, *g_thunkf;

// --- originais reais chamados pelo thunk ---
static __attribute__((noinline)) long orig_int_dirty(long x) {
    // Suja TODOS os caller-saved que o thunk reutiliza pós-call:
    // x9-x17 (x16/x17 inclusive — é o bug (a) à prova).
    __asm__ volatile(
        "mov x9, #0x1111\n\t"
        "mov x10, #0x2222\n\t"
        "mov x11, #0x3333\n\t"
        "mov x12, #0x4444\n\t"
        "mov x13, #0x5555\n\t"
        "mov x14, #0x6666\n\t"
        "mov x15, #0x7777\n\t"
        "ldr x16, =0xdeadbeef00\n\t"
        "ldr x17, =0xcafebabf00\n\t"
        ::: "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    return x;
}
static __attribute__((noinline)) float orig_float_dirty(float x) {
    __asm__ volatile("mov x16, #0x9999\n\tmov x17, #0x8888\n\t"
                     ::: "x16", "x17");
    return x;
}

// --- recursivo de verdade: rec_fn é o ORIG do 3º thunk — cada chamada
// aninhada passa pelo thunk com o lock OCUPADO pela chamada externa
// (cai no fast path: sem save de lr, sem lock, x30 do chamador intocado).
// rec_fn(4) = 1+2+3+4 = 10; o teste chama VIA THUNK -> 3*10 = 30.
static long (*rec_via_thunk)(long);
static __attribute__((noinline)) long rec_fn(long n) {
    if (n <= 0) return 0;
    return rec_via_thunk(n - 1) + n;
}

// --- falha = sinal -> contagem. longjmp só é válido na thread main
// (testes 1 e 2); sinal em worker = aborta com 86 (longjmp entre threads
// é comportamento indefinido).
static jmp_buf g_jb;
static volatile sig_atomic_t g_sig = 0;
static volatile int g_threaded = 0;
static void on_fatal(int sig) {
    (void)sig;
    g_sig = 1;
    if (g_threaded) _exit(86);
    longjmp(g_jb, 1);
}

// --- modo caos: orig dorme segurando o lock (amplia a janela do bug (b))
// e atrasa o NOVO lock holder pra forçar leitura de slot.lr de outra
// thread (sem isso o bug (b) fica invisível em qemu) ---
static volatile int g_chaos = 0;
static volatile uint32_t g_chaos_n = 0;
static __attribute__((noinline)) long chaos_int(long x) {
    // Dorme a cada 64 chamadas ( workers chamam com x fixo — não dá pra
    // usar o argumento): ampla a janela de corrida sem estourar o alarm.
    if (g_chaos && ((++g_chaos_n) & 0x3F) == 0) usleep(50);
    return orig_int_dirty(x);
}

typedef long (*fn_long)(long);
typedef float (*fn_float)(float);

static int g_fail = 0;
static void check(const char *name, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

// --- injeção de bugs nas palavras emitidas (só nos modos de prova) ---
static void apply_bug(uint32_t *t, int n, const char *mode) {
    if (strcmp(mode, "buga") == 0) {
        // Mata o 2º 'add x17, x16, #24' (índice 13 — o 1º é o índice 2):
        // pós-call o thunk passa a destravar com o x17 SUJO pelo orig ->
        // endereço lixo.
        for (int i = 13; i < n; i++) {
            if (t[i] == 0x91006211u) { t[i] = 0xD503201Fu; return; } // nop
        }
    } else if (strcmp(mode, "bugb") == 0) {
        // Troca restore-lr <-> unlock (índices 16/17): unlock antes de ler
        // slot.lr = outra thread sobrescreve o lr salvo (SMP real).
        uint32_t tmp = t[16]; t[16] = t[17]; t[17] = tmp;
    } else if (strcmp(mode, "bugb2") == 0) {
        // Restore de x30 vira NOP: o ret executa com o x30 do blr (dentro
        // do thunk) — mecanismo determinístico do retorno errado.
        for (int i = 14; i < n; i++) {
            if (t[i] == 0xF9400A1Eu) { t[i] = 0xD503201Fu; return; } // nop
        }
    }
}

// --- workers da concorrência (100k chamadas int + 100k float cada) ---
// Contrato sob disputa (enunciado): resultado = orig OU orig*fator conforme
// lock — chamada que chega com o lock ocupado (outra thread no orig) passa
// direto, sem fator. Fora desses dois valores = corrupção.
static volatile uint32_t g_bad = 0;
static volatile long g_bad_r;
static volatile float g_bad_rf;
static volatile uint32_t g_n_mul, g_n_orig;
static void *worker(void *arg) {
    (void)arg;
    fn_long ti = (fn_long)g_thunk;
    fn_float tf = (fn_float)g_thunkf;
    for (int i = 0; i < 100000; i++) {
        long r = ti(10);
        if (r == 30) g_n_mul++;            // pegou o lock: com fator
        else if (r == 10) g_n_orig++;      // lock ocupado: direto, sem fator
        else if (g_bad == 0) { g_bad = 1; g_bad_r = r; }
        float rf = tf(2.0f);
        if (rf != 5.0f && rf != 2.0f && g_bad == 0) { g_bad = 2; g_bad_rf = rf; }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *mode = (argc > 1) ? argv[1] : "fixed";
    setvbuf(stdout, NULL, _IONBF, 0);  // crash não come a saída (diagnóstico)
    signal(SIGSEGV, on_fatal);
    signal(SIGBUS, on_fatal);
    signal(SIGILL, on_fatal);
    alarm(120);  // hang (lock preso) -> SIGALRM mata o processo

    printf("thunk_exec: modo = %s\n", mode);
    int buggy = strcmp(mode, "fixed") != 0;

    // Slots no .bss (espelha up_slots estático do mod). A página RWX tem
    // que ficar a <= ±4GB dos slots (alcance do adrp) — no mod real .bss e
    // mmap nunca distam isso. Hint perto do .bss; se não rolar, slots
    // entram na própria página (sempre encodável).
    uint8_t *hint = (uint8_t *)(((uintptr_t)slot + 0x1000000ull) & ~0xFFFull);
    void *base = mmap(hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        printf("mmap RWX falhou: %s\n", strerror(errno));
        return 2;
    }
    uint8_t *slot_i = slot, *slot_f = slotf;
    uintptr_t d = (uintptr_t)base > (uintptr_t)slot
                      ? (uintptr_t)base - (uintptr_t)slot
                      : (uintptr_t)slot - (uintptr_t)base;
    if (d > 0x7F000000ull) {
        slot_i = (uint8_t *)base + 0x800;
        slot_f = (uint8_t *)base + 0x820;
        printf("nota: slots fora do alcance do adrp -> dentro da página\n");
    }
    memset(slot_i, 0, SLOT_SIZE);
    memset(slot_f, 0, SLOT_SIZE);

    // Thunks na página RWX; slots com fator fixo (3 e 2.5f).
    // Nota: thunk fica PERTO dos slots (só os endereços importam — o
    // emissor recebe thunk_va/slot_va reais).
    uint32_t *code = (uint32_t *)base;
    // Mapa da página: int 0x000-0x054 | float 0x080-0x0D4 | slot rec
    // 0x0E0-0x100 | thunk rec 0x100-0x154 (slot rec NÃO pode encostar no
    // float thunk — 0xC0 sobrepunha e corrompia as últimas palavras dele).
    uint8_t *slot_r = (uint8_t *)base + 0xE0;
    int nint = up_emit_mul_thunk_c(code, code, slot_i, 0);
    int nflt = up_emit_mul_thunk_c(code + 32, code + 32, slot_f, 1);
    int nrec = up_emit_mul_thunk_c(code + 64, code + 64, slot_r, 0);
    printf("thunk int: %d palavras @ %p | float: %d @ %p | rec: %d @ %p (slots %p/%p/%p)\n",
           nint, (void *)code, nflt, (void *)(code + 32), nrec, (void *)(code + 64),
           slot_i, slot_f, slot_r);
    if (nint != up_mul_thunk_words_c() || nflt != up_mul_thunk_words_c() ||
        nrec != up_mul_thunk_words_c()) {
        printf("UP_MUL_THUNK_WORDS divergente do emissor\n");
        return 2;
    }
    if (buggy) apply_bug(code, nint, mode);

    g_thunk = code;
    g_thunkf = code + 32;
    *(void **)&slot_i[SLOT_ORIG] = (void *)chaos_int;
    *(long *)&slot_i[SLOT_VALUE] = 3;
    *(void **)&slot_f[SLOT_ORIG] = (void *)orig_float_dirty;
    { float f = 2.5f; memcpy(&slot_f[SLOT_VALUE], &f, 4); }
    *(void **)&slot_r[SLOT_ORIG] = (void *)rec_fn;
    *(long *)&slot_r[SLOT_VALUE] = 3;
    rec_via_thunk = (fn_long)(code + 64);

    fn_long thunk = (fn_long)g_thunk;
    fn_float thrf = (fn_float)g_thunkf;

    // ---- teste 1: resultado multiplicado (com x9-x17 sujos) ----
    if (setjmp(g_jb) == 0) {
        long r = thunk(10);
        check("mul int: 3*10 = 30 (com x9-x17 sujos pelo orig)", r == 30);
        float rf = thrf(2.0f);
        check("mul float: 2.5*2.0 = 5.0", rf == 5.0f);
    } else {
        check("execução sobreviveu ao orig sujo (x9-x17)", 0);
    }

    // ---- teste 2: recursão de verdade via fast path (lock ocupado pela
    // chamada externa; x30 do chamador tem que chegar intocado no fundo) ----
    if (setjmp(g_jb) == 0) {
        long r = rec_via_thunk(4);   // 3 * (1+2+3+4)
        check("recursão (4 níveis via fast path): 3*(1+2+3+4) = 30", r == 30);
    } else {
        check("recursão via fast path retornou (sem self-loop)", 0);
    }

    // ---- teste 3: concorrência 8 threads x 100k ----
    if (setjmp(g_jb) == 0) {
        g_chaos = 1;
        g_threaded = 1;
        pthread_t th[8];
        for (int i = 0; i < 8; i++) {
            if (pthread_create(&th[i], NULL, worker, NULL) != 0) {
                printf("pthread_create falhou\n");
                return 2;
            }
        }
        for (int i = 0; i < 8; i++) pthread_join(th[i], NULL);
        g_chaos = 0;
        check("8 threads x 100k: só orig ou orig*fator (sem corrupção)", g_bad == 0);
        printf("  (com fator: %u | direto/lock ocupado: %u)\n", g_n_mul, g_n_orig);
        if (g_bad == 1) printf("  (1º int errado: r=%ld, válido {10,30})\n", g_bad_r);
        if (g_bad == 2) printf("  (1º float errado: rf=%g, válido {2,5})\n", (double)g_bad_rf);
        check("lock int == 0 no fim", slot_i[SLOT_LOCK] == 0);
        check("lock float == 0 no fim", slot_f[SLOT_LOCK] == 0);
    } else {
        check("concorrência sobreviveu", 0);
    }

    printf("== thunk_exec: %s (%d falhas) ==\n", g_fail == 0 ? "OK" : "FALHOU", g_fail);
    return g_fail == 0 ? 0 : 1;
}
