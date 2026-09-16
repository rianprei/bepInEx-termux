// Companion Process para Zygisk BC POC - Bridge para Termux via Unix Domain Socket
//
// Este companion process roda como root fora do sandbox do app e expõe
// um socket abstract para comunicação com o Termux.
//
// Arquitetura:
// - Companion cria socket abstract @bc_companion
// - Termux conecta ao socket (via Python)
// - Companion autentica via SO_PEERCRED (verifica UID)
// - Companion processa comandos e envia respostas

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/system_properties.h>
#include <stdio.h>
#include <pthread.h>
#include <atomic>
#include <cstdint>

#include "zygisk.hpp"
#include "bc_mods_conf.h"
#include "bc_loader.h"  // BC_MODS_DIR + bc_loader_is_mod_filename() — validação de nome pro push_mod

#define LOG_TAG "BC_COMPANION"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

static const char *SOCKET_NAME = "bc_companion";

// Forward decl: stream_broadcast é usado por stream_add_client (mais
// abaixo, mas antes da definição real de stream_broadcast no arquivo).
static void stream_broadcast(const char *line, size_t len);

// Forward decl: handle_list_patches é usado por list_patches_thread (mais
// abaixo, mas antes da definição real de handle_list_patches no arquivo).
static void handle_list_patches(int fd);

// Anuncia evento do PRÓPRIO companion (não do jogo) pros clientes de
// streaming já conectados, no mesmo formato BepInEx-style do main.cpp.
// Sem relógio de parede aqui de propósito (simplicidade — companion não
// linka <time.h> pra isso ainda); cliente ainda reconhece "[Nível :Fonte]"
// no início da linha e colore certo mesmo sem o prefixo de hora.
static void companion_announce(const char *level, const char *msg) {
    char line[200];
    int len = snprintf(line, sizeof(line), "[%-7s:%10s] %s\n", level, "Companion", msg);
    if (len > 0) stream_broadcast(line, (size_t)(len > (int)sizeof(line) - 1 ? (int)sizeof(line) - 1 : len));
}

// ============================================================================
// STREAMING de eventos do jogo → clientes Termux (comando "stream")
// ============================================================================
// Contrato: o módulo (game process) publica linhas no socket do companion
// (zygisk_socket, via publish_event). Um reader thread lê essas linhas e faz
// broadcast pra todos os clientes Termux conectados em modo streaming.
//
// Como game e companion são PROCESSOS separados, a "queue" é o kernel socket
// buffer — thread-safe e não-bloqueante por natureza (o jogo nunca espera IO;
// o companion nunca trava com um cliente lento: cada broadcast é não-bloqueante).

#define MAX_STREAM_CLIENTS 16
static int g_stream_clients[MAX_STREAM_CLIENTS];   // fds dos clientes em streaming
static int g_stream_count = 0;
static pthread_mutex_t g_stream_lock = PTHREAD_MUTEX_INITIALIZER;  // protege a lista

// Adiciona um cliente ao grupo de streaming (conexão "stream" aceita).
// Non-blocking flag é set no accept; broadcast usa MSG_DONTWAIT.
static void stream_add_client(int fd) {
    pthread_mutex_lock(&g_stream_lock);
    if (g_stream_count >= MAX_STREAM_CLIENTS) {
        pthread_mutex_unlock(&g_stream_lock);
        LOGE("limite de %d stream clients atingido — recusando fd=%d", MAX_STREAM_CLIENTS, fd);
        close(fd);
        return;
    }
    g_stream_clients[g_stream_count++] = fd;
    // Captura o total AINDA dentro do lock — ler g_stream_count depois do
    // unlock é race real (TOCTOU): outra thread pode mudar o valor entre o
    // unlock e a leitura pro log. Achado via inspeção de código, mesma
    // classe de bug que ASan/TSan pegam, mas visível aqui por leitura.
    int total = g_stream_count;
    pthread_mutex_unlock(&g_stream_lock);
    LOGI("stream client adicionado (fd=%d, total=%d)", fd, total);
    char msg[64];
    snprintf(msg, sizeof(msg), "novo cliente stream conectado (total=%d)", total);
    companion_announce("Info", msg);
}

// Remove um cliente (desconectou / falha de write). Fecha o fd.
// PRECONDIÇÃO: chamador já segura g_stream_lock (única chamadora é
// stream_broadcast, que faz o lock/unlock em volta do loop inteiro — dar
// lock/unlock aqui dentro causaria double-unlock/UB no fim do loop).
static void stream_remove_client(int idx) {
    int fd = g_stream_clients[idx];
    g_stream_clients[idx] = g_stream_clients[g_stream_count - 1];
    g_stream_count--;
    close(fd);
    LOGI("stream client removido (fd=%d, total=%d)", fd, g_stream_count);
}

// Broadcast de uma linha de evento pra todos os clientes de streaming.
// Não-bloqueante por cliente: MSG_DONTWAIT + MSG_NOSIGNAL. Cliente lento que
// enche o buffer recebe drop (segue vivo) — não trava os outros nem o daemon.
// Cliente que fechou → EPIPE → removido da lista e fechado (sem crash).
static void stream_broadcast(const char *line, size_t len) {
    pthread_mutex_lock(&g_stream_lock);
    for (int i = g_stream_count - 1; i >= 0; i--) {   // p/ trás: remoções shift seguras
        int fd = g_stream_clients[i];
        ssize_t r = send(fd, line, len, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            // EPIPE/ECONNRESET etc = cliente caiu. Remove e segue.
            if (i < g_stream_count) stream_remove_client(i);
        }
    }
    pthread_mutex_unlock(&g_stream_lock);
}

// Lê linhas de evento do jogo (zygisk_socket, fd passado via arg) e faz
// broadcast. Roda numa thread própria dentro do daemon Termux. Encerra
// quando o jogo fecha o socket (EOF) ou erro de leitura.
static void *stream_socket_reader(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[512];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            LOGI("stream_socket_reader: canal do jogo fechado (n=%zd)", n);
            break;
        }
        stream_broadcast(buf, (size_t)n);
    }
    close(fd);
    // Fix EADDRINUSE (antes documentado, não corrigido): esse daemon foi
    // criado pra servir ESTE game_fd. Quando o jogo morre/relança, o fd
    // fica morto pra sempre — não há como recuperar streaming nesta
    // instância. Antes a thread só saía e o processo (accept loop na
    // thread principal) continuava vivo pra sempre, segurando
    // @bc_companion e bloqueando o bind() do daemon do próximo launch.
    // _exit() mata o processo inteiro: fd do socket abstract fecha,
    // nome libera na hora pro próximo companion_handler() conseguir bind.
    LOGI("daemon encerrando (canal do jogo morreu) — liberando @%s pro proximo launch", SOCKET_NAME);
    _exit(0);
}

// ============================================================================
// Setup Socket Abstract
// ============================================================================

int setup_abstract_socket(const char *name) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';  // Abstract namespace
    size_t name_len = strlen(name);
    if (name_len > sizeof(addr.sun_path) - 2) {
        LOGE("socket name too long (%zu > %zu), truncating", name_len, sizeof(addr.sun_path) - 2);
        name_len = sizeof(addr.sun_path) - 2;
    }
    memcpy(addr.sun_path + 1, name, name_len);
    addr.sun_path[1 + name_len] = '\0';  // null-term explícita no fim do nome

    // Achado real (comparação /proc/net/unix vs termux_client.py): abstract
    // socket name = exatamente os bytes de addrlen após sun_path[0]. Passar
    // sizeof(addr) (tamanho da struct inteira) faz o nome incluir todo o
    // padding zerado até o fim de sun_path (108 bytes), não só "bc_companion"
    // — bind() "funciona" e aparece em /proc/net/unix, mas ninguém que
    // conecta com o nome exato curto (ex: Python '\0bc_companion') bate,
    // porque abstract socket exige match exato de comprimento + bytes.
    // addrlen correto = family (2 bytes) + '\0' + nome, sem o resto do buffer.
    socklen_t addrlen = sizeof(addr.sun_family) + 1 + name_len;
    if (bind(fd, (struct sockaddr*)&addr, addrlen) < 0) {
        LOGE("bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        LOGE("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOGI("abstract socket listening: @%s", name);
    return fd;
}

// ============================================================================
// Autenticação via SO_PEERCRED
// ============================================================================

int getpeercred(int fd, struct ucred *cred) {
    socklen_t len = sizeof(*cred);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, cred, &len);
}

uid_t get_termux_uid() {
    struct stat st;
    if (stat("/data/data/com.termux/", &st) == 0) {
        return st.st_uid;
    }
    LOGE("failed to stat /data/data/com.termux/");
    return -1;
}

bool is_authorized_uid(uid_t uid) {
    // Whitelist mínima: Termux (UID real do app) + shell (UID 2000, adb forward)
    // + root (UID 0, debugging local). SO_PEERCRED garante UID real (não spoofable
    // pro abstract socket — exigem estar no device).
    uid_t termux_uid = get_termux_uid();
    if (uid == 0) return true;        // root
    if (uid == 2000) return true;     // shell (adb forward)
    // termux_uid >= 0 seria tautologia morta: uid_t é não-assinado (POSIX),
    // nunca filtra o sentinel -1 de get_termux_uid() em falha de stat().
    // Funcionava por acidente (uid real nunca bate com (uid_t)-1 == UINT_MAX),
    // não por design — comparação correta contra o sentinel explícito.
    if (termux_uid != (uid_t)-1 && uid == termux_uid) return true;  // Termux
    return false;
}

// ============================================================================
// Processamento de Comandos
// ============================================================================

// Lê um comando com boundary explícito: '\n' como delimitador de fim de
// mensagem. Loop garante que comandos chegando em múltiplos chunks TCP não
// sejam truncados — read() único corta mensagens > MTU e corrompe o strcmp.
// Retorna -1 em erro, ou 0..len em sucesso.
static ssize_t read_command(int fd, char *buf, size_t cap) {
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t n = read(fd, buf + used, 1);
        if (n < 0) {
            if (errno == EINTR) continue;   // repete em sinal
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // timeout SO_RCVTIMEO
            LOGE("read() failed: %s", strerror(errno));
            return -1;
        }
        if (n == 0) break;                  // EOF (peer fechou)
        char c = buf[used];
        used++;
        if (c == '\n') break;               // fim de mensagem
    }
    buf[used] = '\0';
    // Descarta terminador '\n' se presente, pra strcmp funcionar direto
    if (used > 0 && buf[used - 1] == '\n') buf[used - 1] = '\0';
    return (ssize_t)used;
}

// Envia resposta com verificação de retorno; loop cobre write() parcial em
// socket non-blocking ou buffer cheio.
static ssize_t write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // timeout SO_SNDTIMEO
            LOGE("write() failed: %s", strerror(errno));
            return -1;
        }
        if (n == 0) break;                  // EOF
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

// ============================================================================
// Config de hooks (bc_mods.conf) — comandos list_mods / toggle_mod
// ============================================================================

// (KNOWN_HOOKS removido — a lista de chaves agora é o BC_SCHEMA logo abaixo,
// que inclui as chaves tipadas novas além dos 4 hooks bool)

// Schema espelho do main.cpp (1:1 — tem que bater). Mantido aqui porque o
// companion é processo separado; se adicionar chave, adicionar nos DOIS.
static const char *const BC_SRC_DOMAIN[] = {"game", "companion", nullptr};
static const struct bc_mod_schema BC_SCHEMA[] = {
    {"appInit",       BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appUpdateDraw", BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appTouch",      BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appKey",        BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"throttle_every",BC_MOD_INT,  false, 1, 600, 60, nullptr, nullptr},
    {"stream_source", BC_MOD_ENUM, false, 0, 0, 0,    BC_SRC_DOMAIN, "game"},
};
static const int BC_SCHEMA_N = (int)(sizeof(BC_SCHEMA) / sizeof(BC_SCHEMA[0]));

// Busca schema por nome (nullptr = chave desconhecida)
static const struct bc_mod_schema *schema_find(const char *name) {
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        if (strcmp(BC_SCHEMA[i].name, name) == 0) return &BC_SCHEMA[i];
    }
    return nullptr;
}

// Carrega o config atual preenchendo TODAS as chaves do schema (default quando
// ausente). Retorna sempre BC_SCHEMA_N.
static int load_mods_conf(struct bc_mod_entry *out, int cap) {
    int fd = open(BC_MODS_CONF_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        bc_mods_parse(nullptr, BC_SCHEMA, BC_SCHEMA_N, out, cap);
        return BC_SCHEMA_N;  // ausente = defaults
    }
    char buf[2048];
    ssize_t total = 0;
    while (total < (ssize_t)sizeof(buf) - 1) {
        ssize_t r = read(fd, buf + total, sizeof(buf) - 1 - (size_t)total);
        if (r < 0) {
            if (errno == EINTR) continue;
            total = -1;
            break;
        }
        if (r == 0) break;
        total += r;
    }
    close(fd);
    if (total < 0) total = 0;
    buf[total] = '\0';
    bc_mods_parse(buf, BC_SCHEMA, BC_SCHEMA_N, out, cap);
    return BC_SCHEMA_N;
}

// Persiste o config atomicamente: escreve em temp + rename (mesmo filesystem,
// rename é atômico). Permissões 0644: legível por qualquer um (list é
// inofensivo), escrita só root — e o toggle só é aceito de UID Termux
// autenticado via SO_PEERCRED.
static bool save_mods_conf(const struct bc_mod_entry *entries, int n) {
    char tmp[] = BC_MODS_CONF_PATH ".tmp.XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) {
        LOGE("mkstemp(%s) falhou: %s", tmp, strerror(errno));
        return false;
    }
    char buf[2048];
    int len = bc_mods_format(BC_SCHEMA, BC_SCHEMA_N, entries, n, buf, sizeof(buf));
    if (len < 0 || write_all(fd, buf, (size_t)len) != len) {
        LOGE("write config falhou");
        close(fd);
        unlink(tmp);
        return false;
    }
    if (fsync(fd) < 0) {
        LOGE("fsync falhou: %s", strerror(errno));
        close(fd);
        unlink(tmp);
        return false;
    }
    close(fd);
    if (chmod(tmp, 0644) < 0 || rename(tmp, BC_MODS_CONF_PATH) < 0) {
        LOGE("chmod/rename falhou: %s", strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

// Resposta de list_mods: "name=value" por linha (todas as chaves do schema,
// com default quando ausente). Formato de linha idêntico ao arquivo em si.
static void handle_list_mods(int fd) {
    struct bc_mod_entry entries[BC_MODS_CONF_MAX];
    load_mods_conf(entries, BC_MODS_CONF_MAX);
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        const struct bc_mod_schema *sc = &BC_SCHEMA[i];
        char line[64];
        int w;
        switch (sc->type) {
            case BC_MOD_BOOL:
                w = snprintf(line, sizeof(line), "%s=%s%s\n", entries[i].name,
                             entries[i].b ? "on" : "off",
                             entries[i].present ? "" : " (default)");
                break;
            case BC_MOD_INT:
                w = snprintf(line, sizeof(line), "%s=%ld%s\n", entries[i].name,
                             entries[i].i, entries[i].present ? "" : " (default)");
                break;
            case BC_MOD_ENUM:
                w = snprintf(line, sizeof(line), "%s=%s%s\n", entries[i].name,
                             entries[i].s, entries[i].present ? "" : " (default)");
                break;
            default:
                w = -1;
        }
        if (w > 0) write_all(fd, line, (size_t)w);
    }
}

// ============================================================================
// list_patches — estado REAL dos hooks no game process (Harmony
// GetPatchedMethods equivalent). O companion não tem acesso à memória do
// jogo; o game process é quem sabe (PLANS[] backup + hits). Canal invertido
// do unpatch_mod: companion pede via property, game escreve snapshot e o
// companion correlaciona pela seq antes de responder (resposta velha de boot
// anterior é descartada).
// ============================================================================

// Snapshot publicado pelo game process: nome|estado|hits por linha, com
// header "seq=N" na primeira linha. Estado: active (backup != NULL) /
// unpatched / disabled (desligado por config) / no-target (RVA/símbolo/
// assinatura não resolveram). O arquivo persiste entre boots — por isso a
// resposta NUNCA é servida de snapshot órfão: só conteúdo com a seq do
// pedido atual (snapshot velho de boot anterior seria mentira).
#define BC_PATCHES_PATH "/data/local/tmp/bc_patches.txt"
#define BC_PQ_PROP "persist.bc_poc.patches_req"

// ACHADO REAL (device, 2026-09-15): 2 clientes list_patches concorrentes
// (thread-per-client) competem pela MESMA property compartilhada
// (BC_PQ_PROP) — o game process só suporta 1 pedido em voo por vez (1
// property, não fila). Segundo cliente sobrescreve a seq do primeiro antes
// dele ler a resposta; primeiro cliente nunca recebe o snapshot dele e
// estoura timeout ("game process did not answer"), mesmo com o jogo vivo e
// respondendo. Reproduzido: 2 chamadas simultâneas, 1 falhou. Fix: mutex em
// volta do ciclo sinaliza→espera→lê inteiro — serializa só ESSE trecho
// (request/response com o game process), não o accept loop inteiro (outros
// comandos como ping/toggle_mod continuam livres, e o mutex nunca segura
// mais que ~2.5s no pior caso).
static pthread_mutex_t g_patches_req_lock = PTHREAD_MUTEX_INITIALIZER;

// Última seq pedida (companion é processo único de vida longa — atomic<unsigned>
// garante uniq seq por cliente mesmo com threads paralelas no accept loop).
// Wrap em 2^32 é irrecorrente (<1 chamada/segundo pra list_patches).
static std::atomic<unsigned> g_patches_req_seq{0};

// Forward decl: list_patches_thread chama handle_list_patches, que está
// definida logo abaixo (thread wrapper vem antes da implementação do corpo).
static void handle_list_patches(int fd);

// Thread entry: assume ownership do client_fd (close no final).
// Cada cliente list_patches roda isoladamente — thread é o unit de
// multi-cliente (o accept loop não bloqueia mais em 2.5s de polling).
static void *list_patches_thread(void *arg) {
    // ASSUMES OWNERSHIP of the client fd — closes it at the end (success or error).
    int client_fd = (int)(intptr_t)arg;
    handle_list_patches(client_fd);
    close(client_fd);
    return nullptr;
}

// Lê o snapshot que o game process publicou, valida contra a seq pedida e
// repassa pro cliente. Timeout → erro explícito (game não rodando ou thread
// de hooks morta) — nunca dados de outra época.
static void handle_list_patches(int fd) {
    // Serializa o ciclo request/response inteiro — property compartilhada
    // suporta só 1 pedido em voo (ver comentário em g_patches_req_lock).
    pthread_mutex_lock(&g_patches_req_lock);

    // 1. sinaliza o game process via property (mesmo padrão do unpatch_mod)
    unsigned seq = ++g_patches_req_seq;
    char seqbuf[16];
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    __system_property_set(BC_PQ_PROP, seqbuf);

    // 2. espera o snapshot com essa seq (deadline ~2.5s; o game process
    //    pode estar em load pesado — o poll dele roda a cada 1s)
    char content[2048];
    bool got = false;
    for (int attempt = 0; attempt < 25 && !got; attempt++) {
        usleep(100 * 1000);
        int sfd = open(BC_PATCHES_PATH, O_RDONLY | O_CLOEXEC);
        if (sfd < 0) continue;
        ssize_t n = read(sfd, content, sizeof(content) - 1);
        close(sfd);
        if (n <= 0) continue;
        content[n] = '\0';
        char hdr[16] = {0};
        if (sscanf(content, "seq=%15s", hdr) == 1 && strcmp(hdr, seqbuf) == 0)
            got = true;  // seq bate: snapshot desta requisição (não velho)
    }

    // 3. sem resposta: limpa o pedido e erro explícito
    if (!got) {
        __system_property_set(BC_PQ_PROP, "");
        const char *e = "error: game process did not answer (game not running? hook thread dead?)\n";
        write_all(fd, e, strlen(e));
        pthread_mutex_unlock(&g_patches_req_lock);
        return;
    }

    // 4. repassa o corpo (pula a linha seq=, que é metadado interno)
    const char *body = strchr(content, '\n');
    if (body != nullptr) body++;
    else body = content;
    write_all(fd, body, strlen(body));
    pthread_mutex_unlock(&g_patches_req_lock);
}

// toggle_mod <nome> (só chaves BOOL): inverte o estado. Semântica de arquivo
// mínimo: ausente = default (ON), toggle cria a linha oposta, toggle de volta
// ao default REMOVE a linha. Config todo-default = arquivo removido.
// Chaves não-bool (int/enum) são recusadas — usar set_mod pra essas.
static void handle_toggle_mod(int fd, const char *arg) {
    if (arg == nullptr || arg[0] == '\0') {
        const char *e = "error: usage: toggle_mod <nome>\n";
        write_all(fd, e, strlen(e));
        return;
    }
    const struct bc_mod_schema *sc = schema_find(arg);
    if (sc == nullptr) {
        const char *e = "error: unknown key\n";
        write_all(fd, e, strlen(e));
        return;
    }
    if (sc->type != BC_MOD_BOOL) {
        const char *e = "error: not a bool key (use set_mod)\n";
        write_all(fd, e, strlen(e));
        return;
    }
    struct bc_mod_entry entries[BC_MODS_CONF_MAX];
    load_mods_conf(entries, BC_MODS_CONF_MAX);
    int slot = -1;
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        if (strcmp(entries[i].name, arg) == 0) { slot = i; break; }
    }
    if (slot < 0) return;  // impossível (schema completo), mas fail-closed
    bool now_on = !entries[slot].b;
    entries[slot].b = now_on;
    entries[slot].present = true;
    if (now_on && sc->def_b) {
        entries[slot].present = false;  // voltou pro default = remove linha
    }
    // arquivo todo-default → unlink
    bool any = false;
    for (int i = 0; i < BC_SCHEMA_N; i++) if (entries[i].present) { any = true; break; }
    if (!any) {
        unlink(BC_MODS_CONF_PATH);
        const char *ok = "ok: all default\n";
        write_all(fd, ok, strlen(ok));
        return;
    }
    if (!save_mods_conf(entries, BC_SCHEMA_N)) {
        const char *e = "error: save failed\n";
        write_all(fd, e, strlen(e));
        return;
    }
    char msg[64];
    int w = snprintf(msg, sizeof(msg), "ok: %s=%s\n", arg, now_on ? "on" : "off");
    if (w > 0) write_all(fd, msg, (size_t)w);
}

// set_mod <nome> <valor>: define valor tipado (int/enum; bool aceita on/off).
// Coação igual ao parse do módulo: fora do range/domínio → erro pro cliente
// (diferente do parse que clampa — aqui a UI merece saber que errou).
static void handle_set_mod(int fd, const char *name, const char *value) {
    if (name == nullptr || name[0] == '\0' || value == nullptr || value[0] == '\0') {
        const char *e = "error: usage: set_mod <nome> <valor>\n";
        write_all(fd, e, strlen(e));
        return;
    }
    const struct bc_mod_schema *sc = schema_find(name);
    if (sc == nullptr) {
        const char *e = "error: unknown key\n";
        write_all(fd, e, strlen(e));
        return;
    }
    struct bc_mod_entry entries[BC_MODS_CONF_MAX];
    load_mods_conf(entries, BC_MODS_CONF_MAX);
    int slot = -1;
    for (int i = 0; i < BC_SCHEMA_N; i++) {
        if (strcmp(entries[i].name, name) == 0) { slot = i; break; }
    }
    if (slot < 0) return;
    switch (sc->type) {
        case BC_MOD_BOOL: {
            bool b;
            bc_mod_coerce_bool(value, sc->def_b, &b);
            entries[slot].b = b;
            break;
        }
        case BC_MOD_INT: {
            char *end = nullptr;
            long v = strtol(value, &end, 10);
            if (end == value || *end != '\0') {
                const char *e = "error: not an integer\n";
                write_all(fd, e, strlen(e));
                return;
            }
            if (v < sc->min_i || v > sc->max_i) {
                char e[80];
                int w = snprintf(e, sizeof(e), "error: out of range [%ld..%ld]\n", sc->min_i, sc->max_i);
                if (w > 0) write_all(fd, e, (size_t)w);
                return;
            }
            entries[slot].i = v;
            break;
        }
        case BC_MOD_ENUM: {
            bool ok = false;
            for (const char *const *v = sc->enum_values; *v; v++)
                if (strcmp(value, *v) == 0) { ok = true; break; }
            if (!ok) {
                const char *e = "error: value not in domain\n";
                write_all(fd, e, strlen(e));
                return;
            }
            snprintf(entries[slot].s, sizeof(entries[slot].s), "%s", value);
            break;
        }
    }
    // default → remove a linha (arquivo mínimo)
    bool is_default = false;
    switch (sc->type) {
        case BC_MOD_BOOL: is_default = (entries[slot].b == sc->def_b); break;
        case BC_MOD_INT:  is_default = (entries[slot].i == sc->def_i); break;
        case BC_MOD_ENUM: is_default = (strcmp(entries[slot].s, sc->def_s) == 0); break;
    }
    entries[slot].present = !is_default;
    bool any = false;
    for (int i = 0; i < BC_SCHEMA_N; i++) if (entries[i].present) { any = true; break; }
    if (!any) {
        unlink(BC_MODS_CONF_PATH);
        const char *ok = "ok: all default\n";
        write_all(fd, ok, strlen(ok));
        return;
    }
    if (!save_mods_conf(entries, BC_SCHEMA_N)) {
        const char *e = "error: save failed\n";
        write_all(fd, e, strlen(e));
        return;
    }
    char msg[96];
    int w = snprintf(msg, sizeof(msg), "ok: %s=%s\n", name, value);
    if (w > 0) write_all(fd, msg, (size_t)w);
}

// Limite de tamanho pro push_mod — protege contra client malicioso/bugado
// mandando "size" absurdo e travando o companion lendo pra sempre.
#define BC_PUSH_MOD_MAX_SIZE (16 * 1024 * 1024)

// Recebe um .so via socket e escreve em BC_MODS_DIR. Protocolo: linha
// "push_mod <nome> <tamanho>" já consumida por read_command() antes de
// chamar aqui — o payload bruto (exatamente <tamanho> bytes) vem em
// seguida, ainda não lido (read_command lê byte-a-byte só até '\n', não
// passa do delimitador). Root já autenticado via SO_PEERCRED antes deste
// ponto (mesmo socket, mesma conexão) — reusa a mesma confiança, sem novo
// gate.
static void handle_push_mod(int fd, const char *name, long size) {
    if (!bc_loader_is_mod_filename(name)) {
        const char *e = "error: invalid mod filename\n";
        write_all(fd, e, strlen(e));
        return;
    }
    if (size <= 0 || size > BC_PUSH_MOD_MAX_SIZE) {
        const char *e = "error: invalid size\n";
        write_all(fd, e, strlen(e));
        return;
    }

    mkdir(BC_MODS_DIR, 0755);

    char path[512];
    int pw = snprintf(path, sizeof(path), "%s/%s", BC_MODS_DIR, name);
    if (pw <= 0 || (size_t)pw >= sizeof(path)) {
        const char *e = "error: path too long\n";
        write_all(fd, e, strlen(e));
        return;
    }

    int out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        LOGE("push_mod: open(%s) failed: %s", path, strerror(errno));
        const char *e = "error: open failed\n";
        write_all(fd, e, strlen(e));
        return;
    }

    char chunk[8192];
    long remaining = size;
    bool ok = true;
    while (remaining > 0) {
        size_t want = remaining < (long)sizeof(chunk) ? (size_t)remaining : sizeof(chunk);
        ssize_t got = read(fd, chunk, want);
        if (got <= 0) {
            LOGE("push_mod: read failed at %ld bytes remaining: %s",
                 remaining, got == 0 ? "EOF" : strerror(errno));
            ok = false;
            break;
        }
        ssize_t w = write(out, chunk, (size_t)got);
        if (w != got) {
            LOGE("push_mod: write(%s) failed: %s", path, strerror(errno));
            ok = false;
            break;
        }
        remaining -= got;
    }
    close(out);

    if (!ok) {
        unlink(path);  // arquivo parcial não deve ficar meio-carregado no diretório de mods
        const char *e = "error: transfer incomplete\n";
        write_all(fd, e, strlen(e));
        return;
    }

    char msg[64];
    int w = snprintf(msg, sizeof(msg), "ok: %ld bytes written\n", size);
    if (w > 0) write_all(fd, msg, (size_t)w);
    LOGI("push_mod: wrote %s (%ld bytes)", path, size);
    // Sinaliza o game process (main.cpp, event_thread) pra RE-EXECUTAR o
    // loader canônico load_dynamic_mods() (bc_loader.h + bc_mod_graph.h).
    // Cross-process via property (mesmo padrão reload_config/unpatch/repatch):
    // o companion não tem acesso à memória do game process pra chamar o loader
    // direto; o poll remoto enxerga o arquivo novo em BC_MODS_DIR e o carrega.
    __system_property_set("persist.bc_poc.reload_mods", "1");
}

// Retorna true se o fd foi "adotado" por outro dono (ex.: stream) e o
// chamador (accept loop) NÃO deve fechar o client_fd — false = fluxo normal
// request/response, chamador fecha como sempre.
bool handle_termux_request(int client_fd) {
    char buf[4096];
    ssize_t n = read_command(client_fd, buf, sizeof(buf));
    if (n > 0) {
        LOGI("received command: %s", buf);

        // Comando "stream": vira cliente de tail-f, não fecha o fd —
        // ownership passa pra lista de streaming (stream_add_client fecha
        // ele próprio quando o cliente cair/desconectar).
        if (strcmp(buf, "stream") == 0) {
            // Sem timeout de leitura/escrita nesse fd daqui pra frente — é
            // conexão de vida longa, não request/response pontual.
            struct timeval notimeo = { .tv_sec = 0, .tv_usec = 0 };
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &notimeo, sizeof(notimeo));
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &notimeo, sizeof(notimeo));
            stream_add_client(client_fd);
            return true;
        }

        // Command whitelist. toggle_mod aceita 1 argumento; set_mod aceita 2
        // (nome + valor, separados por 1 espaço cada).
        if (strcmp(buf, "ping") == 0) {
            const char *response = "pong";
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "reload_config") == 0) {
            __system_property_set("persist.bc_poc.reload_config", "1");
            const char *response = "ok: reload signal sent";
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "hook_overhead") == 0) {
            char overhead[PROP_VALUE_MAX] = {0};
            __system_property_get("persist.bc_poc.hook_overhead_us", overhead);
            char response[128];
            snprintf(response, sizeof(response), "avg_dispatcher_overhead_us=%s", overhead);
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "status") == 0) {
            const char *response = "companion_active";
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "list_mods") == 0) {
            handle_list_mods(client_fd);
        } else if (strncmp(buf, "toggle_mod ", 11) == 0) {
            handle_toggle_mod(client_fd, buf + 11);
        } else if (strncmp(buf, "set_mod ", 8) == 0) {
            char *sp = strchr(buf + 8, ' ');
            if (sp == nullptr) {
                const char *e = "error: usage: set_mod <nome> <valor>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                *sp = '\0';
                handle_set_mod(client_fd, buf + 8, sp + 1);
            }
        } else if (strcmp(buf, "list_patches") == 0) {
            // Thread-per-client: list_patches bloqueia ~2.5s esperando o game
            // publicar snapshot. Se for serial no accept loop, clientes 2/N
            // ficam presos na backlog queue listen(5) — sem multi-cliente real.
            // Spawna pthread detached: accept loop libera IMMEDIATELY, cada
            // cliente atende simultaneamente. Thread assume ownership do fd
            // (close no final).
            //
            // ACHADO REAL (device, 2026-09-15): o caller (termux_accept_loop)
            // faz `if (!adopted) close(client)` — ou seja, `false` aqui
            // significa "FECHA agora", o oposto do que o comentário antigo
            // assumia. Com `return false`, o caller fechava o fd IMEDIATAMENTE
            // enquanto a thread detached ainda estava esperando o snapshot —
            // cliente recebia EOF vazio na hora, thread ficava escrevendo num
            // fd morto. Sintoma: log mostrava a thread iniciar e setar a
            // property, mas nunca chegar no write_all. Fix: `true` (adotado)
            // em AMBOS os caminhos — thread lançada OU fallback que já
            // fechou o fd sozinho (evita double-close também).
            pthread_t t;
            void *arg = (void *)(intptr_t)client_fd;
            if (pthread_create(&t, nullptr, list_patches_thread, arg) == 0) {
                pthread_detach(t);  // libera recursos sem join
                // NÃO close(client_fd) aqui — thread é responsável
            } else {
                // pthread_create falhou — fallback serial (melhor que leakar fd)
                handle_list_patches(client_fd);
                close(client_fd);
            }
            return true;  // adotado — caller NÃO fecha (thread ou fallback já fecharam)
        } else if (strncmp(buf, "unpatch_mod ", 12) == 0) {
            // Sinal cross-process pro game process rodar unpatch_hook() real
            // (DobbyDestroy) — companion não tem acesso à memória do alvo,
            // só sinaliza via property, mesmo padrão do reload_config.
            const char *name = buf + 12;
            if (strlen(name) == 0 || strlen(name) >= PROP_VALUE_MAX) {
                const char *e = "error: usage: unpatch_mod <nome>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                __system_property_set("persist.bc_poc.unpatch_target", name);
                const char *response = "ok: unpatch signal sent";
                write_all(client_fd, response, strlen(response));
            }
        } else if (strncmp(buf, "push_mod ", 9) == 0) {
            // "push_mod <nome> <tamanho>" — parse manual em vez de sscanf(%s)
            // porque precisamos saber onde o nome termina pra achar o
            // tamanho depois, sem limite implícito de sscanf em nome.
            char name[256];
            long size = -1;
            int matched = sscanf(buf + 9, "%255s %ld", name, &size);
            if (matched != 2) {
                const char *e = "error: usage: push_mod <nome> <tamanho>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                handle_push_mod(client_fd, name, size);
            }
        } else if (strncmp(buf, "repatch_mod ", 12) == 0) {
            // Sinal cross-process pro game process re-instalar um hook que
            // foi removido via unpatch_mod. Mesmo padrão (property), só que
            // o game process chama repatch_hook() → try_install() no hook.
            const char *name = buf + 12;
            if (strlen(name) == 0 || strlen(name) >= PROP_VALUE_MAX) {
                const char *e = "error: usage: repatch_mod <nome>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                __system_property_set("persist.bc_poc.repatch_target", name);
                const char *response = "ok: repatch signal sent";
                write_all(client_fd, response, strlen(response));
            }
        } else {
            const char *response = "unknown_command";
            write_all(client_fd, response, strlen(response));
        }
    } else if (n < 0) {
        LOGE("read_command() failed: %s", strerror(errno));
    }
    return false;
}

// ============================================================================
// Companion Handler Principal
// ============================================================================

// Loop de aceitação do socket Termux, isolado numa thread própria.
//
// Achado real (revisão hermes+kilo, comparado com zygisk-module-sample,
// TrickyStore, PlayIntegrityFix): companion_handler() é chamado pelo daemon
// zygiskd com o contrato de "responde rápido e volta" — o fd recebido
// (zygisk_socket) É o canal de IPC com o processo do app, não um convite pra
// virar daemon de vida longa. Bloquear aqui num accept() próprio nunca
// deixava a primeira linha de log rodar (nem uma vez, em 4 tentativas de
// fix). Corrigido: o accept loop pro socket Termux roda numa pthread
// detached separada; companion_handler() apenas dispara essa thread (uma
// vez, via pthread_once) e retorna imediatamente, sem tocar no
// zygisk_socket (não usamos esse canal — ver comentário abaixo).
//
// ATUALIZAÇÃO: thread detached não sobrevivia ao processo companion
// efêmero sair (ver comentário em daemonize_termux_server() abaixo) — a
// função virou o corpo do processo daemonizado via double-fork, chamada
// direto (sem pthread), mas o nome/assinatura ficou igual pra reaproveitar
// via chamada de função comum em vez de thread.
static void *termux_accept_loop(void *) {
    int termux_server = setup_abstract_socket(SOCKET_NAME);
    if (termux_server < 0) {
        // Limitação conhecida, não-crítica: se o jogo for reaberto (2º
        // launch), companion_handler() roda de novo e este bind() falha com
        // EADDRINUSE — o daemon da 1ª sessão ainda segura @bc_companion.
        // Fail-safe: thread sai limpo (sem crash), mas o streaming do 2º
        // launch fica sem canal novo (daemon antigo serve um game_fd morto).
        // Não corrigido aqui de propósito — exigiria detectar e matar o
        // daemon anterior (mais superfície de bug do que vale agora).
        LOGE("failed to setup Termux socket (bind ocupado por daemon anterior?), thread exiting");
        return nullptr;
    }

    LOGI("companion ready for Termux connections on @%s", SOCKET_NAME);

    // Loop de aceitação (indefinido até o daemon ser morto)
    while (1) {
        // SOCK_CLOEXEC: fd não vaza pra processos filho.
        int client = accept4(termux_server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            // Achado real (kilo): erro não-EINTR (ex: EMFILE/ENFILE por
            // exaustão de fd) antes virava loop apertado sem backoff —
            // spin de CPU 100% sem chance de o sistema se recuperar.
            // 100ms de espera dá tempo do kernel liberar recurso; depois
            // de muitas falhas seguidas o processo está mesmo quebrado,
            // então derruba o daemon (mesmo padrão do fix EADDRINUSE).
            static int consecutive_errors = 0;
            LOGE("accept() failed: %s", strerror(errno));
            if (++consecutive_errors > 20) {
                LOGE("accept() falhando persistentemente (%d erros seguidos) — daemon encerrando", consecutive_errors);
                _exit(1);
            }
            usleep(100 * 1000);
            continue;
        }

        // Backpressure: um cliente que conecta e não envia nada não pode
        // bloquear o accept loop inteiro (DoS). Timeout curto no recv/send.
        // read_command() trata EAGAIN/EWOULDBLOCK retornando logo.
        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct ucred cred;
        if (getpeercred(client, &cred) == 0) {
            LOGI("connection from UID=%d PID=%d", cred.uid, cred.pid);

            if (is_authorized_uid(cred.uid)) {
                bool adopted = handle_termux_request(client);
                if (!adopted) close(client);
                // adotado (comando "stream") — stream_add_client já é o
                // novo dono, fecha quando o cliente desconectar, não aqui.
            } else {
                LOGE("rejected connection from UID=%d (not Termux)", cred.uid);
                close(client);
            }
        } else {
            LOGE("getpeercred() failed: %s", strerror(errno));
            close(client);
        }
    }
    return nullptr;
}

// Achado real #2 (fonte: Magisk daemon.rs, native/src/core/zygisk/daemon.rs,
// função exec_zygiskd): o "companion process" NÃO é uma thread de um daemon
// persistente — é um processo efêmero, criado via fork+execl("magisk",
// "zygisk", "companion", fd) especificamente pra essa requisição, que sai
// (exit) depois de despachar as funções companion registradas. Por isso a
// thread detached (fix anterior) nunca sobrevivia: quando o processo pai
// (o "magisk zygisk companion" recém-exec'd) termina seu main(), TODAS as
// threads morrem junto — detached não protege contra o processo inteiro
// sair. Confirmado via /proc: processo companion visto e já morto na
// checagem seguinte.
//
// Fix real: daemonizar de verdade (double-fork + setsid), não só thread.
// O double-fork solta o processo neto do processo pai efêmero — ele vira
// órfão, reparented pro init, e sobrevive independente do "magisk zygisk
// companion" original sair.
// game_fd: fd do socket pro processo do jogo (zygisk_socket), ou -1 se não
// há canal de streaming disponível pra essa instância (companion_handler
// chamado sem propósito de stream, ou fd inválido). Passa por fork() como
// cópia normal de fd — sobrevive no processo neto/daemon.
static void daemonize_termux_server(int game_fd) {
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("fork() falhou: %s", strerror(errno));
        return;
    }
    if (pid > 0) {
        // processo companion efêmero segue seu fluxo normal (vai sair logo);
        // não esperamos o filho — o double-fork evita zumbi. game_fd foi
        // duplicado pro filho no fork(); esse lado não precisa mais dele.
        if (game_fd >= 0) close(game_fd);
        return;
    }
    // filho: sessão própria, desgruda do processo pai efêmero
    if (setsid() < 0) {
        LOGE("setsid() falhou: %s", strerror(errno));
        _exit(1);
    }
    pid_t pid2 = fork();
    if (pid2 < 0) {
        LOGE("segundo fork() falhou: %s", strerror(errno));
        _exit(1);
    }
    if (pid2 > 0) {
        // filho intermediário sai — neto vira órfão, reparented pro init,
        // garantindo que não fica preso à sessão/grupo do processo efêmero.
        if (game_fd >= 0) close(game_fd);
        _exit(0);
    }
    // neto: daemon real. Se há canal do jogo, sobe a thread leitora de
    // streaming ANTES do accept loop bloquear a thread principal.
    LOGI("daemon Termux destacado (pid=%d)", getpid());
    if (game_fd >= 0) {
        pthread_t reader;
        if (pthread_create(&reader, nullptr, stream_socket_reader,
                            (void *)(intptr_t)game_fd) == 0) {
            pthread_detach(reader);
        } else {
            LOGE("pthread_create(stream_socket_reader) falhou: %s", strerror(errno));
            close(game_fd);
        }
    }
    termux_accept_loop(nullptr);
    _exit(0);
}

// Abre o Termux mostrando stream de log ao vivo + REPL (comando digitado na
// mesma janela) — vai além do console do BepInEx no Windows, confirmado
// output-only no código-fonte (ConsoleManager.cs/WindowsConsoleDriver.cs,
// zero Read/ReadLine/Console.In). Dispara 1x por spawn do companion (= 1x
// por sessão do jogo), fire-and-forget via RUN_COMMAND (RunCommandService,
// pacote termux-app — não termux-api, confirmado no fonte do termux-app).
// Requer allow-external-apps=true em ~/.termux/termux.properties
// (documentado no README) — sem isso o RunCommandService recusa
// silenciosamente, o companion segue normal.
static void launch_termux_console() {
    const char *cmd =
        "am start -n com.termux/com.termux.app.TermuxActivity >/dev/null 2>&1; "
        "am startservice -n com.termux/com.termux.app.RunCommandService "
        "-a com.termux.RUN_COMMAND "
        "--es com.termux.RUN_COMMAND_PATH "
        "'/data/data/com.termux/files/home/battlecats-mods/zygisk-bc-poc/termux-console/bepin-console' "
        "--ez com.termux.RUN_COMMAND_BACKGROUND false >/dev/null 2>&1 &";
    int rc = system(cmd);
    if (rc != 0) {
        LOGW("launch_termux_console: system() rc=%d (Termux/RUN_COMMAND instalado e habilitado?)", rc);
    }
}

void companion_handler(int zygisk_socket) {
    LOGI("companion process started");
    launch_termux_console();

    // Achado real (device, 2026-09-15): /data/local/tmp é drwxrwx--x dono
    // shell:shell — "outros" (onde cai o UID do app do jogo) só tem --x
    // (atravessa, NÃO cria arquivo). write_patches_snapshot() no game
    // process usa mkstemp() nesse dir pra escrita atômica do snapshot do
    // list_patches — falhava silenciosamente (sem log) porque o processo
    // do jogo não tinha permissão de escrita ali. Companion roda como
    // root: relaxa a permissão 1x por spawn (idempotente, custo zero).
    chmod("/data/local/tmp", 0777);

    // zygisk_socket é o outro lado do fd que main.cpp guarda em
    // g_stream_fd (mesmo connectCompanion(), duas pontas). Não fazemos
    // handshake síncrono nele — ele vira o canal de streaming de eventos
    // do jogo, lido pela thread stream_socket_reader() dentro do daemon.
    daemonize_termux_server(zygisk_socket);
}

// ============================================================================
// Registro do Companion
// ============================================================================

REGISTER_ZYGISK_COMPANION(companion_handler)
