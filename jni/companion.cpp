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
#include <dirent.h>
#include <errno.h>
#include <sys/system_properties.h>
#include <stdio.h>
#include <pthread.h>
#include <atomic>
#include <cstdint>

#include "zygisk.hpp"
#include "bc_mods_conf.h"
#include "bc_mods_fd.h" // protocolo de entrega por FD (SCM_RIGHTS)
#include "bc_req_channel.h" // papel do socket (REQ/STREAM) na 1a linha
#include "bc_first_line.h" // a leitura da 1a linha: comprimento coerente com o buffer (bug do aparelho)
#include "bc_req_dispatch.h" // despacho do pedido (nucleo puro, testado no host)
#include "bc_peercred.h"  // amarra o pedido ao uid de quem conectou (SO_PEERCRED)
#include "bc_loader.h"
#include "bc_generic_allowlist.h"  // PATH <pkg>: a allowlist mora na árvore root-only, quem lê é o root
#include "bc_path_decide.h"  // bc_path_is_bc: o gate dos verbos BC (chamador É o jogo BC)
#include "bc_push_io.h"  // transporte do push_mod: exatamente SIZE + EOF (testado no host)
#include "bc_dir_list.h"  // listagem da árvore: filtro nome+lstat+S_ISREG em UM ponto (S2 do kilo)
#include "bc_launch_check.h"  // root só executa arquivo regular, do root, sem escrita de app
#include "bc_req_session.h"  // canal REQ como recurso limitado: gate na abertura + teto (P1)
#include "bc_signal.h"  // contrato dos sinais: property com seq (root escreve, jogo só lê)

// Contador de seq dos sinais: o jogo (app domain) não tem permissão_set, então
// o valor na property É o gatilho — precisa mudar a cada pedido, senão o mesmo
// comando reenviado não faria nada (ver bc_signal.h).
static unsigned bc_signal_seq() {
    static unsigned seq = 0;
    return ++seq;
}

#define LOG_TAG "BC_COMPANION"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

static const char *SOCKET_NAME = BC_COMPANION_SOCKET_NAME; // fonte única: bc_req_channel.h

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
// mensagem. O CORPO é o núcleo puro bc_first_line.h — o MESMO código que o
// teste de host exercita em socketpair (o bug do aparelho — "REQ\n" lido
// como 4 bytes com NUL no lugar do '\n', papel UNKNOWN, canal REQ morto —
// não era alcançável por teste nenhum com a lógica aqui dentro). O
// companion só acrescenta o log do erro.
static ssize_t read_command(int fd, char *buf, size_t cap) {
    ssize_t n = bc_read_first_line(fd, buf, cap);
    if (n < 0) LOGE("read() failed: %s", strerror(errno));
    return n;
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

// (KNOWN_HOOKS removido — a lista de chaves agora é o BC_SCHEMA, definição
// única em bc_mods_conf.h (SSOT — antes duplicado aqui e em main.cpp))

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
// do unpatch_mod: companion pede por property, o jogo escreve o snapshot e o
// companion correlaciona pela seq antes de responder (resposta velha de boot
// anterior é descartada).
// ============================================================================

// Snapshot publicado pelo game process: nome|estado|hits por linha, com
// header "seq=N" na primeira linha. Estado: active (backup != NULL) /
// unpatched / disabled (desligado por config) / no-target (RVA/símbolo/
// assinatura não resolveram). O arquivo persiste entre boots — por isso a
// resposta NUNCA é servida de snapshot órfão: só conteúdo com a seq do
// pedido atual (snapshot velho de boot anterior seria mentira).

// ACHADO REAL (device, 2026-09-15): 2 clientes list_patches concorrentes
// (thread-per-client) competem pelo MESMO canal de pedido (a property)
// — o game process só suporta 1 pedido em voo por vez (1 slot, não fila).
// Segundo cliente sobrescreve a seq do primeiro antes
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
    // Serializa o ciclo request/response inteiro — o canal de pedido suporta
    // só 1 em voo (ver comentário em g_patches_req_lock).
    pthread_mutex_lock(&g_patches_req_lock);

    // 1. sinaliza o poll do jogo via property (mesmo padrão do unpatch_mod)
    unsigned seq = ++g_patches_req_seq;
    char seqbuf[16];
    snprintf(seqbuf, sizeof(seqbuf), "%u", seq);
    __system_property_set(BC_PROP_PATCHES_REQ, seqbuf);

    // 2. espera o snapshot com essa seq (deadline ~2.5s; o game process
    //    pode estar em load pesado — o poll dele roda a cada 1s)
    char content[2048];
    bool got = false;
    for (int attempt = 0; attempt < 25 && !got; attempt++) {
        usleep(100 * 1000);
        int sfd = open(BC_PATCHES_FILE, O_RDONLY | O_CLOEXEC);
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
        // Nada a limpar: o pedido é isolado por seq (o jogo só reage a valor
        // novo), então um snapshot atrasado com seq velha nunca é servido.
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
    if (slot < 0) {
        // achado de review: retornava sem responder nada — cliente ficava
        // travado até o SO_RCVTIMEO/SO_SNDTIMEO de 3s estourar, em vez de
        // saber na hora que algo bateu no caminho "impossível".
        const char *e = "error: unknown key (schema mismatch)\n";
        write_all(fd, e, strlen(e));
        return;
    }
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
    if (slot < 0) {
        // mesmo achado de review do handle_toggle_mod: fail-closed sem
        // resposta deixava o cliente travado até o timeout de 3s.
        const char *e = "error: unknown key (schema mismatch)\n";
        write_all(fd, e, strlen(e));
        return;
    }
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
    // O diretorio tambem e criado pelo root a partir de um nome de cliente:
    // mkdir() em link pre-plantado precisa ser recusado tambem.
    if (pw <= 0 || (size_t)pw >= sizeof(path)) {
        const char *e = "error: path too long\n";
        write_all(fd, e, strlen(e));
        return;
    }

    // O_NOFOLLOW: esta e a ESCRITA do root na arvore root-only, e sem o flag
    // um link pre-plantado nesse nome faria o root TRUNCAR o alvo (que pode
    // estar fora da arvore). O nome ja vem validado por
    // bc_loader_is_mod_filename — sem "/" nem "..", entao o caminho fica na
    // arvore — mas validar o NOME nao impede que ja exista um LINK com esse
    // nome. O O_NOFOLLOW e o que impede.
    int out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (out < 0) {
        LOGE("push_mod: open(%s) failed: %s", path, strerror(errno));
        const char *e = "error: open failed\n";
        write_all(fd, e, strlen(e));
        return;
    }

    // O transporte (EXATAMENTE size bytes + EOF do half-close do emissor) é
    // bc_push_recv_exact — o mesmo código que o teste de host exercita no
    // socketpair. Antes: o laço inline lia size bytes e calava sobre o
    // resto — bytes a mais ficavam no socket e envenenavam o próximo
    // comando, e o "ok" confirmava uma transferência que o protocolo não
    // garantia completa.
    char twhy[192] = {0};
    bool ok = bc_push_recv_exact(fd, out, size, twhy, sizeof(twhy)) == 0;
    close(out);

    if (!ok) {
        LOGE("push_mod: recusado (%s)", twhy);
        unlink(path);  // arquivo parcial não deve ficar meio-carregado no diretório de mods
        char e[256];
        int en = snprintf(e, sizeof(e), "error: %s\n", twhy);
        if (en > 0) write_all(fd, e, (size_t)en);
        return;
    }

    char msg[64];
    int w = snprintf(msg, sizeof(msg), "ok: %ld bytes written\n", size);
    if (w > 0) write_all(fd, msg, (size_t)w);
    LOGI("push_mod: wrote %s (%ld bytes)", path, size);
    // Sinaliza o game process (main.cpp, event_thread) pra RE-EXECUTAR o
    // loader canônico load_dynamic_mods() (bc_loader.h + bc_mod_graph.h).
    // Sinaliza o poll do event_thread (main.cpp) pra RE-EXECUTAR o loader
    // canônico load_dynamic_mods(), que enxerga o arquivo novo em BC_MODS_DIR.
    // Property com seq nova a cada pedido: o jogo não pode limpar o sinal
    // (sem permission_set em Enforcing), quem deduplica é o valor.
    char seq[16];
    snprintf(seq, sizeof(seq), "%u", bc_signal_seq());
    __system_property_set(BC_PROP_RELOAD_MODS, seq);
}

// Retorna true se o fd foi "adotado" por outro dono (ex.: stream) e o
// chamador (accept loop) NÃO deve fechar o client_fd — false = fluxo normal
// request/response, chamador fecha como sempre.
// Entrega por FD — definidos mais abaixo, junto do resto do transporte.
static void handle_mod_fd(int fd, const char *pkg, const char *name);
static void handle_mod_txt(int fd, const char *pkg, const char *name);
static void handle_mod_list(int fd, const char *pkg);
static void handle_path_request(int fd, const char *pkg);
static void handle_bc_so(int fd, const char *name);
static void handle_bc_list(int fd);
static void handle_bc_conf(int fd, const char *name);

// first/firstlen: a PRIMEIRA LINHA da conexão, já lida pelo accept loop (é
// ela que decide o papel da conexão — REQ adota antes daqui; todo o resto
// vem para cá). NULL/n<=0 = nada lido ainda (comportamento legado).
bool handle_termux_request(int client_fd, const char *first, ssize_t firstlen) {
    char buf[4096];
    size_t blen = 0;
    if (first != NULL && firstlen > 0) {
        blen = (size_t)firstlen >= sizeof(buf) ? sizeof(buf) - 1 : (size_t)firstlen;
        memcpy(buf, first, blen);
        buf[blen] = '\0';
    }
    ssize_t n = (blen > 0) ? (ssize_t)blen : read_command(client_fd, buf, sizeof(buf));
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
            char seq[16];
            snprintf(seq, sizeof(seq), "%u", bc_signal_seq());
            __system_property_set(BC_PROP_RELOAD_CONFIG, seq);
            const char *response = "ok: reload signal sent";
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "hook_overhead") == 0) {
            // Resposta do jogo, no diretório C1 (o app não escreve property nem
            // /data/local/tmp em Enforcing; root lê o arquivo dele).
            char ov[64] = {0};
            int ofd = open(BC_OVERHEAD_FILE, O_RDONLY | O_CLOEXEC);
            if (ofd >= 0) {
                ssize_t on = read(ofd, ov, sizeof(ov) - 1);
                close(ofd);
                if (on > 0) ov[on] = '\0';
                char *nl = strchr(ov, '\n');
                if (nl) *nl = '\0';
            }
            if (ov[0] == '\0') snprintf(ov, sizeof(ov), "0");
            char response[128];
            snprintf(response, sizeof(response), "avg_dispatcher_overhead_us=%s", ov);
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "status") == 0) {
            const char *response = "companion_active";
            write_all(client_fd, response, strlen(response));
        } else if (strcmp(buf, "list_mods") == 0) {
            handle_list_mods(client_fd);
        } else if (strncmp(buf, "PATH ", 5) == 0) {
            // Decisão de caminho pré-specialize: o loader (ainda uid 0, filho
            // do zygote) pergunta os FATOS que não enxerga na árvore root-only
            // (existe mods/<pkg>? está na allowlist?); a DECISÃO continua no
            // loader (bc_decide_path, pura e testada no harness).
            handle_path_request(client_fd, buf + 5);
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
            // sinaliza por property (o companion tem permissão_set; o jogo não).
            const char *name = buf + 12;
            if (strlen(name) == 0 || strlen(name) >= PROP_VALUE_MAX) {
                const char *e = "error: usage: unpatch_mod <nome>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                char val[PROP_VALUE_MAX];
                snprintf(val, sizeof(val), "%u %s", bc_signal_seq(), name);
                __system_property_set(BC_PROP_UNPATCH, val);
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
            // Sinal pro poll do event_thread re-instalar um hook removido via
            // unpatch_mod: repatch_hook() → try_install(). Property com seq.
            const char *name = buf + 12;
            if (strlen(name) == 0 || strlen(name) >= PROP_VALUE_MAX) {
                const char *e = "error: usage: repatch_mod <nome>\n";
                write_all(client_fd, e, strlen(e));
            } else {
                char val[PROP_VALUE_MAX];
                snprintf(val, sizeof(val), "%u %s", bc_signal_seq(), name);
                __system_property_set(BC_PROP_REPATCH, val);
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
// ============================================================================
// Entrega por FD (revisao de seguranca do freebuff)
// ============================================================================
// O jogo nao tem acesso a /data/adb/bepinex (root:root 0700) — e nao deve ter.
// Quem abre o .so e o companion, como root; o que CRUZA a fronteira e o
// DESCRITOR.
//
//   mod_fd  <pkg> <nome>  -> o companion monta <raiz>/mods/<pkg>/<nome>, abre
//                            com O_RDONLY|O_NOFOLLOW|O_CLOEXEC e manda o FD por
//                            SCM_RIGHTS (o cliente NUNCA manda caminho)
//   mod_txt <pkg> <nome>  -> o conteudo (conf/allowlist, nao mapeaveis)
//
// O_NOFOLLOW e o que impede o root de abrir um link de dentro da arvore para
// fora dela. A arvore e root-only e o migrador nao segue link, mas o open() e a
// ultima linha: entre o lstat e o open() da um TOCTOU, e o alvo pode ter sido
// trocado nesse intervalo.
//
// O caminho SO pode estar sob a raiz: um cliente (o jogo, ou o Termux via su)
// nao ganha "abrir o que eu pedir" — ganha "abrir o que estiver na arvore de
// mods, com o nome validado".

// ============================================================================
// O PROTOCOLO NAO ACEITA CAMINHO. Aceita (pkg, nome) e o companion MONTA o
// caminho. (ACHADO CRITICO, revisao do OpenCode em c47f5e5)
// ============================================================================
// A versao anterior aceitava um CAMINHO e conferia so o PREFIXO TEXTUAL, que
// nao normaliza "..", e o O_NOFOLLOW do open() nao impede
// "..": ele barra LINK SIMBOLICO, nao travessia de diretorio. Entao o root
// abria e devolvia o FD/conteudo de um arquivo ARBITRARIO, e a listagem
// enumerava um diretorio qualquer.
//
// E o canal e o MESMO que o codigo do mod dentro do jogo usa, entao este e o
// modelo de ameaca: quem consegue falar com o companion e o proprio jogo.
//
// A correcao e estrutural, nao mais um filtro: o cliente NUNCA manda caminho.
// Manda um pacote e um nome; o companion valida os DOIS e concatena com a raiz
// fixa. Nao existe ".." a filtrar, porque o caminho nao vem do cliente.

// Pacote: o mesmo formato que o Manager valida (SuHelper.requirePkg) e que o
// loader ja usava. Sem "/" e sem ".." — o pacote e UM nome, nao um caminho.
static bool bc_mod_pkg_ok(const char *pkg) {
    if (pkg == nullptr) return false;
    const size_t n = strlen(pkg);
    if (n == 0 || n > 160) return false;
    if (pkg[0] == '.' || pkg[n - 1] == '.') return false;   // ".." nao entra
    for (size_t i = 0; i < n; i++) {
        const char c = pkg[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    // ".." no meio tambem: com ponto-permitido, "a..b" e nome invalido de
    // pacote, e um nome com ".." no meio e porta de traversia em outra base.
    if (strstr(pkg, "..") != nullptr) return false;
    return true;
}

// Nome do arquivo: o MESMO validador que o loader usa para nao carregar
// arquivo que nao e mod (bc_loader_is_mod_filename). Sem "/" e sem "..", o que
// ja barra a travessia de diretorio E o separador de caminho.
static bool bc_mod_name_ok(const char *name) {
    if (name == nullptr || *name == 0) return false;
    const size_t n = strlen(name);
    if (n >= 256) return false;
    if (strchr(name, '/') != nullptr) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    if (strstr(name, "..") != nullptr) return false;
    return bc_loader_is_mod_filename(name);
}


static void bc_fd_deny(int fd, const char *what) {
    char e[BC_FD_ERR_MAX];
    ssize_t n = bc_fd_build_error(e, sizeof(e), EACCES);
    if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    LOGE("mod_* recusado: %s", what);
}

// Recusa da família TEXTO/LISTA: prefixo 'E' (o "<errno>\n" puro é
// indistinguível do "<len>\n" de sucesso — ver bc_mods_fd.h).
static void bc_fd_deny_txt(int fd, const char *what) {
    char e[BC_FD_ERR_MAX];
    ssize_t n = bc_fd_build_txt_error(e, sizeof(e), EACCES);
    if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    LOGE("mod_* recusado: %s", what);
}

// ============================================================================
// DE QUEM E O PEDIDO (SO_PEERCRED)
// ============================================================================
// O formato do pacote e valido, mas isso nao diz DE QUEM o pedido e: um jogo A
// pode pedir os mods do jogo B, e o companion entregaria. O canal e o mesmo que
// o codigo do mod dentro do processo usa, entao "confia no processo" nao e
// resposta.
//
// getsockopt(SO_PEERCRED) no fd do cliente devolve o uid de quem CONECTOU, e o
// kernel preenche esse campo — o cliente nao consegue mentir sobre ele. O
// companion e root, entao le /data/system/packages.list e mapeia uid -> pacote.
//
// MULTIUSUARIO: o appId e uid % 100000. Na coluna do packages.list o que
// gravado e o appId, entao comparar com o appId e o que funciona em
// /data/user/10. Ler o uid cru nao.
//
// FAIL-CLOSED: packages.list ilegivel = pedido RECUSADO. Aceitar na duvida
// seria devolver a arvore de mods de qualquer jogo a qualquer processo, que e
// o oposto do que a mudanca de arvore root-only fez.
// Preenche a lista de PACOTES do appId do chamador. Plural porque varios
// pacotes compartilham o mesmo appId (sharedUserId): aceitar so o primeiro
// recusava o segundo ate os mods DELE MESMO.
static int bc_peer_is_caller(int client_fd,
                             char (*pkgs)[BC_PEERCRED_PKG_CAP], int max_pkgs) {
    if (pkgs == NULL || max_pkgs <= 0) return 0;
    for (int i = 0; i < max_pkgs; i++) pkgs[i][0] = '\0';
    struct ucred cred;
    socklen_t clen = sizeof(cred);
    memset(&cred, 0, sizeof(cred));
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen) != 0 ||
        clen != sizeof(cred)) {
        LOGE("peer: SO_PEERCRED falhou: %s (fail-closed)", strerror(errno));
        return 0;
    }
    int fd = open("/data/system/packages.list", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("peer: packages.list ilegivel: %s (fail-closed)", strerror(errno));
        return 0;
    }
    char list[16384];
    ssize_t n = read(fd, list, sizeof(list) - 1);
    close(fd);
    if (n <= 0) {
        LOGE("peer: packages.list vazio (fail-closed)");
        return 0;
    }
    list[n] = '\0';
    int got = bc_peercred_packages(list, (size_t)n, (int)cred.uid, pkgs, max_pkgs);
    if (got <= 0) {
        LOGE("peer: uid %d nao mapeia para pacote (formato desconhecido ou "
             "pacote ausente — fail-closed)", (int)cred.uid);
        return 0;
    }
    return got;
}

// O pedido (pkg, nome) so e servido se o pkg for um DOS pacotes do appId do
// chamador, e so eles.
static bool bc_peer_ok_for_pkg(int client_fd, const char *want_pkg, const char *what) {
    char (*caller)[BC_PEERCRED_PKG_CAP] =
        (char (*)[BC_PEERCRED_PKG_CAP])alloca(sizeof(*caller) * BC_PEERCRED_PKG_MAX);
    int n = bc_peer_is_caller(client_fd, caller, BC_PEERCRED_PKG_MAX);
    if (n <= 0) {
        bc_fd_deny(client_fd, "peer nao identificavel");
        LOGE("%s: recusado — nao deu para identificar o chamador", what);
        return false;
    }
    if (!bc_peercred_pkg_matches(caller, n, want_pkg)) {
        bc_fd_deny(client_fd, "pkg nao e do chamador");
        LOGE("%s: recusado — o chamador (appId %s) pediu %s", what,
             caller[0], want_pkg ? want_pkg : "(nulo)");
        return false;
    }
    return true;
}

// O mesmo gate para a família TEXTO/LISTA: a RECUSA sai no formato 'E'
// ("E<errno>\n") — na família texto, o "<errno>\n" puro é indistinguível
// do "<len>\n" de sucesso e o cliente parseava a recusa como comprimento
// (stall de 5s por recusa; ver bc_mods_fd.h).
static bool bc_peer_ok_for_pkg_txt(int client_fd, const char *want_pkg, const char *what) {
    char (*caller)[BC_PEERCRED_PKG_CAP] =
        (char (*)[BC_PEERCRED_PKG_CAP])alloca(sizeof(*caller) * BC_PEERCRED_PKG_MAX);
    int n = bc_peer_is_caller(client_fd, caller, BC_PEERCRED_PKG_MAX);
    if (n <= 0) {
        bc_fd_deny_txt(client_fd, "peer nao identificavel");
        LOGE("%s: recusado — nao deu para identificar o chamador", what);
        return false;
    }
    if (!bc_peercred_pkg_matches(caller, n, want_pkg)) {
        bc_fd_deny_txt(client_fd, "pkg nao e do chamador");
        LOGE("%s: recusado — o chamador (appId %s) pediu %s", what,
             caller[0], want_pkg ? want_pkg : "(nulo)");
        return false;
    }
    return true;
}

// PATH <pkg> — os DOIS FATOS da decisão de caminho (bc_decide_path), que o
// loader pergunta pré-specialize porque a árvore de mods é root-only e o stat
// do jogo sempre daria EACCES (o has_pkg_mods_dir do loader morreu com a
// mudança de árvore). Quem responde é o root, com os MESMOS critérios que o
// loader usava ao vivo:
//   - existe /data/adb/bepinex/mods/<pkg> como diretório?
//   - <pkg> está na allowlist do experimento Cocos2d-x?
// A DECISÃO não vem daqui: o loader recebe "<dir> <allow>\n" e chama
// bc_decide_path — a função pura, testada no harness, continua sendo o único
// ponto de decisão. O companion só entrega os fatos.
//
// Gate: esta conexão passa pelo is_authorized_uid do accept loop — e o
// chamador pré-specialize é o loader, ainda uid 0 (filho do zygote), que o
// gate já aceita como root. Código de mod não chega aqui: mods rodam depois
// do specialize com uid de app, fora da whitelist de UID.
static void handle_path_request(int fd, const char *pkg_arg) {
    char pkg[192];
    size_t pl = strlen(pkg_arg);
    if (pl == 0 || pl >= sizeof(pkg) || !bc_mod_pkg_ok(pkg_arg)) {
        const char *e = "error: pacote invalido\n";
        write_all(fd, e, strlen(e));
        return;
    }
    memcpy(pkg, pkg_arg, pl + 1);

    bool dir_exists = false;
    char dir[640];
    int pn = snprintf(dir, sizeof(dir), "%s/%s", BC_GENERIC_MODS_DIR, pkg);
    if (pn > 0 && (size_t)pn < sizeof(dir)) {
        struct stat st;
        dir_exists = (stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    }

    bool in_allowlist = false;
    FILE *f = fopen(BC_GENERIC_ALLOWLIST_FILE, "r");
    if (f != nullptr) {
        char buf[16384];
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[got < sizeof(buf) - 1 ? got : sizeof(buf) - 1] = '\0';
        in_allowlist = bc_generic_allowlist_contains_buf(buf, pkg);
    }
    // allowlist ausente = NÃO está na lista (mesma semântica do loader: sem
    // allowlist, só o caminho por pasta conta).

    char resp[64];
    int rn = snprintf(resp, sizeof(resp), "%d %d\n", dir_exists ? 1 : 0,
                      in_allowlist ? 1 : 0);
    if (rn > 0) write_all(fd, resp, (size_t)rn);
    LOGI("PATH %s -> dir=%d allow=%d", pkg, dir_exists ? 1 : 0, in_allowlist ? 1 : 0);
}

// ============================================================================
// Verbos da árvore Battle Cats: "BO"/"BL"/"BT".
//
// POR QUE VERBOS PRÓPRIOS (e não uma força do formato (pkg, nome)): a árvore
// BC é FLAT (BC_MODS_DIR sem subpasta por pacote) e os CONFS moram na RAIZ da
// árvore — montar isso como (pkg, nome) seria fingir um layout que não é o
// dela. O servidor monta o caminho em CADA verbo; o "BT" só aceita nome da
// LISTA FIXA (bc_req_root_conf_ok): nunca um caminho vindo do cliente.
//
// GATE (o mesmo modelo de ameaça dos outros verbos): o chamador tem de SER o
// jogo Battle Cats. SO_PEERCRED -> /data/system/packages.list -> a lista de
// pacotes do appId do chamador; serve se (e só se) algum deles é o pacote do
// BC (bc_path_is_bc é IGUALDADE EXATA com BC_BC_PKG; "pkg:svc" de subprocesso
// aceito, lookalike NÃO). Um mod dentro de OUTRO jogo pedindo a árvore BC
// leva EACCES — a árvore BC não é "mods de quem pede", é do jogo que a
// populate (push_mod).
// ============================================================================
static bool bc_peer_is_bc_game(int client_fd) {
    char (*caller)[BC_PEERCRED_PKG_CAP] =
        (char (*)[BC_PEERCRED_PKG_CAP])alloca(sizeof(*caller) * BC_PEERCRED_PKG_MAX);
    // userId != 0 (perfil 10/outro): RECUSADO fail-closed — a árvore de mods
    // é por pacote, sem dimensão de usuário (multiuser-audit F2); enquanto
    // não houver árvore por (userId, pacote), servir do perfil 0 é o oposto
    // de isolamento. O canal REQ já recusa na ABERTURA (bc_req_session.h);
    // aqui é a defesa dupla: o VERBO também recusa. A DECISÃO é a função
    // pura bc_peercred_bc_denied_user_id — ponto único, testada no host
    // (T3 do peercred_test).
    struct ucred uid_check;
    socklen_t uid_len = sizeof(uid_check);
    memset(&uid_check, 0, sizeof(uid_check));
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &uid_check, &uid_len) != 0 ||
        bc_peercred_bc_denied_user_id((int)uid_check.uid)) {
        LOGE("verbos BC recusados: userId != 0 (uid %d) — perfis de usuário "
             "ainda sem suporte (fail-closed)", (int)uid_check.uid);
        return false;
    }
    int n = bc_peer_is_caller(client_fd, caller, BC_PEERCRED_PKG_MAX);
    if (n <= 0) {
        // bc_peer_is_caller já logou o motivo (fail-closed)
        return false;
    }
    for (int i = 0; i < n; i++) {
        // Matcher REAL: igualdade exata com BC_BC_PKG (ou BC_BC_PKG:svc)
        if (bc_path_is_bc(caller[i])) return true;
    }
    LOGE("verbos BC recusados: o chamador (appId %s) nao e o jogo BC", caller[0]);
    return false;
}

// Gate dos verbos BC com o formato de RECUSA da família do verbo: "BO" é
// família do FD (erro em "<errno>\n", sem ambiguidade — o FD vem na
// ancillary data); "BL"/"BT" são família TEXTO/LISTA (erro em "E<errno>\n").
static bool bc_peer_bc_gate(int client_fd, bool txt_family) {
    if (bc_peer_is_bc_game(client_fd)) return true;
    if (txt_family) bc_fd_deny_txt(client_fd, "verbos BC so para o jogo BC");
    else bc_fd_deny(client_fd, "verbos BC so para o jogo BC");
    return false;
}

// "BO <nome>" -> FD do .so da árvore BC (BC_MODS_DIR é root-only desde a
// relocação: o jogo NÃO pode opendir/dlopen por caminho — quem abre é o
// root e o que cruza a fronteira é o descritor).
static void handle_bc_so(int fd, const char *name) {
    if (!bc_peer_bc_gate(fd, false)) return;
    if (!bc_mod_name_ok(name)) {
        bc_fd_deny(fd, "BO: nome invalido");
        return;
    }
    char path[512];
    int pn = snprintf(path, sizeof(path), "%s/%s", BC_MODS_DIR, name);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
        bc_fd_deny(fd, "BO: caminho montado grande demais");
        return;
    }
    int f = bc_fd_open_ro(path);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        LOGE("BO: open(%s) falhou: %s", path, strerror(errno));
        return;
    }
    static const char kAck = 'F';
    if (bc_fd_send(fd, f, &kAck, 1) < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    }
    close(f);
}

// "BL" -> lista de .so da árvore BC. Mesmo formato de fio do mod_list
// (nomes, total no fim) — o loader do jogo ordena a lista dele (qsort por
// nome, para o grafo de dependências); o companion não inventa ordem.
// O FILTRO (nome + lstat + S_ISREG) é o núcleo bc_dir_list_regular_mods —
// o MESMO código do mod_list e do teste de host (S2 do kilo: o bloco
// duplicado não tinha cobertura e o S_ISREG saiu sem ninguém ver).
struct bc_list_ctx {
    int fd;
    int err;
};

static int bc_list_emit(void *p, const char *name) {
    struct bc_list_ctx *c = (struct bc_list_ctx *)p;
    char line[512];
    int n = snprintf(line, sizeof(line), "%s\n", name);
    if (n <= 0) return -1;
    if (bc_fd_send_data(c->fd, line, (size_t)n) < 0) {
        c->err = 1;
        return -1;  // cliente sumiu: para de enumerar
    }
    return 0;
}

static void handle_bc_list(int fd) {
    if (!bc_peer_bc_gate(fd, true)) return;
    struct bc_list_ctx ctx = { fd, 0 };
    int total = bc_dir_list_regular_mods(BC_MODS_DIR, bc_list_emit, &ctx);
    if (total < 0) {
        // Sem árvore BC não é erro: o jogo só não carrega nada.
        bc_fd_send_data(fd, "0\n", 2);
        return;
    }
    char line[32];
    snprintf(line, sizeof(line), "%d\n", total);
    bc_fd_send_data(fd, line, strlen(line));
}

// "BT <nome>" -> conteudo de um conf da RAIZ da árvore. Lista fixa
// (bc_req_root_conf_ok): bc_mods.conf (config dos 4 hooks estáticos, que
// toggle_mod/set_mod escrevem) e bc_generic_allowlist.conf (allowlist do
// experimento Cocos). Qualquer outro nome: EACCES — o cliente nunca ganha
// "abra o arquivo que eu pedir".
static void handle_bc_conf(int fd, const char *name) {
    if (!bc_peer_bc_gate(fd, true)) return;
    if (!bc_req_root_conf_ok(name)) {
        bc_fd_deny_txt(fd, "BT: conf fora da lista fixa");
        return;
    }
    char path[512];
    int pn = snprintf(path, sizeof(path), "%s/%s", BC_MODS_ROOT, name);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
        bc_fd_deny_txt(fd, "BT: caminho montado grande demais");
        return;
    }
    int f = bc_fd_open_ro(path);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    char head[32];
    off_t sz = lseek(f, 0, SEEK_END);
    if (sz < 0 || sz > BC_FD_TXT_MAX) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), sz < 0 ? errno : EFBIG);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        close(f);
        return;
    }
    int n2 = snprintf(head, sizeof(head), "%lld\n", (long long)sz);
    if (n2 > 0) bc_fd_send_data(fd, head, (size_t)n2);
    if (sz > 0 && lseek(f, 0, SEEK_SET) == (off_t)0) {
        char buf[4096];
        ssize_t got;
        while ((got = read(f, buf, sizeof(buf))) > 0) {
            if (bc_fd_send_data(fd, buf, (size_t)got) < 0) break;
        }
    }
    close(f);
}



// A LISTA tambem vem pelo socket. O jogo nao pode scandir() a arvore
// root-only (root:root 0700, sem search para appdomain) — e nao deve: quem
// enumera e o root. O formato e um nome por linha, ja filtrado para o que o
// loader carrega (.so e nada mais), ordenado no ROOT (o readdir do root e
// imprevisivel e um mod pode depender de outro).
// mod_list <pkg> -> lista os .so de <BC_GENERIC_MODS_DIR>/<pkg>. Sem caminho do
// cliente: o companion monta a partir do pacote validado.
static void handle_mod_list(int fd, const char *pkg) {
    if (!bc_mod_pkg_ok(pkg)) {
        bc_fd_deny_txt(fd, "mod_list: pacote invalido");
        return;
    }
    if (!bc_peer_ok_for_pkg_txt(fd, pkg, "mod_list")) return;
    char path[640];
    int pn = snprintf(path, sizeof(path), "%s/%s", BC_GENERIC_MODS_DIR, pkg);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
        bc_fd_deny_txt(fd, "mod_list: caminho montado grande demais");
        return;
    }
    // O FILTRO é o núcleo bc_dir_list_regular_mods — o MESMO código do BL e
    // do teste de host (S2 do kilo: o bloco duplicado não tinha cobertura).
    // lstat pelo root: um link dentro da arvore nao e seguido, e um item que
    // nao for arquivo regular NAO entra na lista (o jogo nao tem como abrir).
    struct bc_list_ctx ctx = { fd, 0 };
    int total = bc_dir_list_regular_mods(path, bc_list_emit, &ctx);
    if (total < 0) {
        // Sem mods nao e erro: o jogo so nao carrega nada.
        bc_fd_send_data(fd, "0\n", 2);
        return;
    }
    char line[32];
    snprintf(line, sizeof(line), "%d\n", total);
    bc_fd_send_data(fd, line, strlen(line));
}

// mod_fd <pkg> <nome> -> o companion monta <BC_GENERIC_MODS_DIR>/<pkg>/<nome> e
// devolve o FD. O cliente nao manda caminho (ver o bloco dos validadores acima).
static void handle_mod_fd(int fd, const char *pkg, const char *name) {
    if (!bc_mod_pkg_ok(pkg) || !bc_mod_name_ok(name)) {
        bc_fd_deny(fd, "mod_fd: pacote ou nome invalido");
        return;
    }
    if (!bc_peer_ok_for_pkg(fd, pkg, "mod_fd")) return;
    char path[640];
    int pn = snprintf(path, sizeof(path), "%s/%s/%s", BC_GENERIC_MODS_DIR, pkg, name);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
        bc_fd_deny(fd, "mod_fd: caminho montado grande demais");
        return;
    }
    int f = bc_fd_open_ro(path);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        LOGE("mod_fd: open(%s) falhou: %s", path, strerror(errno));
        return;
    }
    // 1 byte de payload, NAO 0: num SOCK_STREAM um send de 0 byte chega como
    // EOF no outro lado, e o recvmsg do jogo acorda por dados que nao existem.
    // O protocolo le o payload como ack e o FD vem na ancillary data.
    static const char kAck = 'F';
    if (bc_fd_send(fd, f, &kAck, 1) < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    }
    close(f);
}

// mod_txt <pkg> <nome> -> conteudo do .conf/.bpatch do mod. Mesmo modelo do
// mod_fd: o cliente manda (pkg, nome) e o companion monta o caminho.
static void handle_mod_txt(int fd, const char *pkg, const char *name) {
    if (!bc_mod_pkg_ok(pkg) || !bc_mod_name_ok(name)) {
        bc_fd_deny_txt(fd, "mod_txt: pacote ou nome invalido");
        return;
    }
    if (!bc_peer_ok_for_pkg_txt(fd, pkg, "mod_txt")) return;
    char path[640];
    int pn = snprintf(path, sizeof(path), "%s/%s/%s", BC_GENERIC_MODS_DIR, pkg, name);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
        bc_fd_deny_txt(fd, "mod_txt: caminho montado grande demais");
        return;
    }
    int f = bc_fd_open_ro(path);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    // Conteudo: primeiro o tamanho, depois os bytes. O jogo aloca e le. Acima
    // de BC_FD_TXT_MAX o cliente tem que pedir mod_fd.
    char head[32];
    off_t sz = lseek(f, 0, SEEK_END);
    if (sz < 0 || sz > BC_FD_TXT_MAX) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), sz < 0 ? errno : EFBIG);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        close(f);
        return;
    }
    int n2 = snprintf(head, sizeof(head), "%lld\n", (long long)sz);
    if (n2 > 0) bc_fd_send_data(fd, head, (size_t)n2);
    if (sz > 0 && lseek(f, 0, SEEK_SET) == (off_t)0) {
        char buf[4096];
        ssize_t got;
        while ((got = read(f, buf, sizeof(buf))) > 0) {
            if (bc_fd_send_data(fd, buf, (size_t)got) < 0) break;
        }
    }
    close(f);
}

// ============================================================================
// O CANAL DE PEDIDOS (achado BLOQUEANTE do OpenCode em c242d5f)
// ============================================================================
// O canal de pedidos do JOGO: conexão ao @bc_companion, pós-specialize, com
// SO_PEERCRED REAL do app (uid do jogo -> /data/system/packages.list -> os
// pacotes do próprio chamador). Achado do OpenCode em c242d5f: os pedidos iam
// pelo socket de STREAMING, cujo único leitor faz broadcast, e nenhum mod
// carregava — com a suite toda verde, porque media as PEÇAS e não o FIO.
//
// O DESPACHO é o núcleo puro de bc_req_dispatch.h — o MESMO que o teste de
// host exercita no socketpair. Aqui só entram os handlers REAIS de root, e
// cada um já é gateado por bc_peer_ok_for_pkg (SO_PEERCRED -> packages.list ->
// "o pedido só é servido se o pkg for do chamador"): um mod malicioso dentro
// do jogo A pede os mods do jogo B e leva EACCES.
//
// Contrato do loop: bc_req_dispatch_one devolve 1=atendido, 0=recusado COM
// resposta (linha de errno — a conexão segue útil), -1=fim (read <= 0). O
// -1 é o que fecha: a versão WIP ignorava o retorno e o loop virava SPIN
// de CPU 100% no EOF (read de socket fechado retorna 0 na hora, para sempre).
static const struct bc_req_handlers req_root_handlers = {
    handle_mod_fd,   // "SO"  -> FD do .so (SCM_RIGHTS)
    handle_mod_txt,  // "TX"  -> conteúdo (conf/allowlist)
    handle_mod_list, // "LS"  -> lista de .so da pasta do pacote
    handle_bc_so,    // "BO"  -> FD do .so da árvore BC (gate: é o jogo BC)
    handle_bc_list,  // "BL"  -> lista da árvore BC
    handle_bc_conf,  // "BT"  -> conf da raiz, nome da LISTA FIXA
};

// ---- a ponta injetável da sessão REQ (bc_req_session.h) ---------------------
//
// P1 da pré-revisão (freebuff, ALTA — DoS por qualquer app): o ramo REQ
// criava uma pthread por conexão sem teto e sem gate de abertura. A camada
// pura (bc_req_session.h) decide TUDO — gate (peer servável + userId==0),
// teto (global 16 / por-uid 4) e contagem em cada saída — e é a MESMA que
// o teste de host exercita. Aqui só entram as pontas que precisam de root:
// packages.list, stat da árvore root-only e o pthread de verdade.

static int session_peer_uid(int fd, uid_t *out) {
    struct ucred cred;
    socklen_t len = sizeof(cred);
    memset(&cred, 0, sizeof(cred));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || len != sizeof(cred)) {
        return -1;
    }
    *out = cred.uid;
    return 0;
}

static int session_resolve_pkgs(int fd, uid_t uid,
                                char pkgs[][BC_REQ_SESSION_PKG_CAP], int max) {
    (void)uid;  // bc_peer_is_caller lê o uid do SO_PEERCRED do próprio fd
    return bc_peer_is_caller(fd, (char (*)[BC_PEERCRED_PKG_CAP])pkgs, max);
}

static bool session_pkg_has_dir(const char *pkg) {
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", BC_GENERIC_MODS_DIR, pkg);
    if (n <= 0 || (size_t)n >= sizeof(path)) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool session_pkg_in_allowlist(const char *pkg) {
    // Mesma leitura do handle_path_request: o root lê a allowlist da raiz
    // root-only; o jogo nunca a abre por caminho.
    FILE *f = fopen(BC_GENERIC_ALLOWLIST_FILE, "r");
    if (f == nullptr) return false;
    char buf[16384];
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got < sizeof(buf) - 1 ? got : sizeof(buf) - 1] = '\0';
    return bc_generic_allowlist_contains_buf(buf, pkg);
}

static int session_spawn(void *(*body)(void *), void *arg) {
    pthread_t t;
    if (pthread_create(&t, nullptr, body, arg) != 0) return -1;
    pthread_detach(t);
    return 0;
}

static struct bc_req_slots g_req_slots;   // teto global — um por daemon

static void session_init_once(void) {
    static bool done = false;
    if (!done) {
        bc_req_slots_init(&g_req_slots);
        done = true;
    }
}

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
        // static aqui fora (não só dentro do if de erro) porque precisa
        // ser zerado no caminho de SUCESSO logo abaixo — achado de review:
        // antes só incrementava e nunca resetava, então não contava falhas
        // CONSECUTIVAS de verdade, e sim o total na vida inteira do daemon,
        // matando-o depois de 21 falhas transitórias espalhadas em dias.
        static int consecutive_errors = 0;
        int client = accept4(termux_server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            // Achado real (kilo): erro não-EINTR (ex: EMFILE/ENFILE por
            // exaustão de fd) antes virava loop apertado sem backoff —
            // spin de CPU 100% sem chance de o sistema se recuperar.
            // 100ms de espera dá tempo do kernel liberar recurso; depois
            // de muitas falhas seguidas o processo está mesmo quebrado,
            // então derruba o daemon (mesmo padrão do fix EADDRINUSE).
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

        // Achado de review (freebuff): reset tinha que estar aqui, fora de
        // qualquer branch -- accept() com sucesso já quebra a sequência de
        // falhas, mesmo que o cliente seja rejeitado depois por UID (isso é
        // operação normal do accept loop, não falha dele).
        consecutive_errors = 0;

        struct ucred cred;
        if (getpeercred(client, &cred) != 0) {
            LOGE("getpeercred() failed: %s", strerror(errno));
            close(client);
            continue;
        }
        LOGI("connection from UID=%d PID=%d", cred.uid, cred.pid);

        // A PRIMEIRA LINHA decide o papel desta conexão (bc_req_channel.h —
        // o MESMO parser do teste de host). O accept já lê com o timeout de
        // 3s armado acima: quem conecta e não fala nada morre no timeout sem
        // prender o loop (DoS).
        char first[4096];
        ssize_t fn = read_command(client, first, sizeof(first));
        if (fn <= 0) { close(client); continue; }

        if (bc_req_role_from_line(first, fn) == BC_ROLE_REQ) {
            // Canal de pedidos do JOGO — o ramo INTEIRO agora é a camada pura
            // de sessão (bc_req_session.h): gate na abertura (peer servável +
            // userId==0), teto global/por-uid ANTES do spawn e contagem em
            // cada saída. Os VERBOS de dentro seguem gateados por pacote
            // (bc_peer_ok_for_pkg) — defesa dupla: abrir o canal não garante
            // nada além do canal. Sem timeout de idle: o canal legítimo pode
            // ficar ocioso à vontade DENTRO do teto (era o trade-off do WIP,
            // e é isso que o teto protege em vez do timeout cego).
            static const struct bc_req_server_ops session_ops = {
                session_peer_uid,
                session_resolve_pkgs,
                session_pkg_has_dir,
                session_pkg_in_allowlist,
                &req_root_handlers,
                session_spawn,
            };
            session_init_once();
            struct timeval notimeo = { .tv_sec = 0, .tv_usec = 0 };
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &notimeo, sizeof(notimeo));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &notimeo, sizeof(notimeo));
            bc_req_serve_connection(client, first, fn, &session_ops, &g_req_slots);
            continue;
        }

        if (!is_authorized_uid(cred.uid)) {
            LOGE("rejected connection from UID=%d (not Termux)", cred.uid);
            close(client);
            continue;
        }
        bool adopted = handle_termux_request(client, first, fn);
        if (!adopted) close(client);
        // adotado (comando "stream") — stream_add_client já é o
        // novo dono, fecha quando o cliente desconectar, não aqui.
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
        // O socket do connectCompanion() e SÓ DE STREAMING: quem lê é o
        // stream_socket_reader, que faz broadcast das linhas de log do jogo
        // pros clientes Termux. O canal de PEDIDOS (mod_fd/mod_txt/mod_list)
        // NÃO vem por aqui — vem por uma conexão do jogo ao @bc_companion,
        // pós-specialize, com SO_PEERCRED real do app (ver bc_req_channel.h:
        // no connectCompanion pré-specialize o peer é o uid 0 do zygote, e o
        // gate por pacote recusaria o próprio jogo; e uma 2ª conexão zygisk
        // viraria um 2º daemon que morre no bind EADDRINUSE levando o canal
        // junto). A versão WIP lia uma "primeira linha de papel" AQUI —
        // engolia a 1ª linha de log do stream e nunca re-broadcastava.
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
// O console mora NO MÓDULO (id=bc-poc, o mesmo do build_module.sh:114-118 —
// trocar o id do módulo troca este caminho junto): empacotado pelo build,
// reproduzível de um clone, root:root. O caminho ANTIGO apontava para dentro
// do dado de outro app (/data/data/com.termux/files/home/battlecats-mods/...)
// — irrecuperável de um clone e arquivo gravável por app, que é escalada
// quando o root executa.
#define BC_CONSOLE_PATH "/data/adb/modules/bc-poc/termux-console/bepin-console"

static void launch_termux_console() {
    // Barreira ANTES de executar: o companion é ROOT, e root só executa o
    // que o root controla (arquivo regular, dono root, nada gravável por
    // grupo/outros). bc_root_script_ok é o MESMO código que o teste de host
    // exercita — se a checagem divergir, o teste cai.
    struct stat st;
    char why[192] = {0};
    if (lstat(BC_CONSOLE_PATH, &st) != 0) {
        LOGW("launch_termux_console: %s ausente (modulo antigo?) — console não sobe",
             BC_CONSOLE_PATH);
        return;
    }
    if (bc_root_script_ok(&st, why, sizeof(why)) != 0) {
        LOGW("launch_termux_console: recusado (%s): %s — root não executa", why,
             BC_CONSOLE_PATH);
        return;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "am start -n com.termux/com.termux.app.TermuxActivity >/dev/null 2>&1; "
        "am startservice -n com.termux/com.termux.app.RunCommandService "
        "-a com.termux.RUN_COMMAND "
        "--es com.termux.RUN_COMMAND_PATH "
        "'%s' "
        "--ez com.termux.RUN_COMMAND_BACKGROUND false >/dev/null 2>&1 &",
        BC_CONSOLE_PATH);
    int rc = system(cmd);
    if (rc != 0) {
        LOGW("launch_termux_console: system() rc=%d (Termux/RUN_COMMAND instalado e habilitado?)", rc);
    }
}

void companion_handler(int zygisk_socket) {
    LOGI("companion process started");
    launch_termux_console();

    // O chmod("/data/local/tmp", 0777) QUE ESTAVA AQUI FOI REMOVIDO.
    //
    // Ele existia por um motivo real (device, 2026-09-15): o processo do jogo
    // escrevia o snapshot bc_patches.txt com mkstemp() nesse diretório, e o
    // AOSP cria /data/local/tmp como 0771 shell:shell — o uid do jogo só tem
    // --x, não escreve. O companion rodava como root e relaxava 0771 -> 0777
    // uma vez por spawn.
    //
    // Esse relaxamento ERA o achado de seguranca, nao um detalhe: 0777 em
    // /data/local/tmp significa que QUALQUER appuid do aparelho — inclusive o
    // jogo — escreve, cria e TROCA o diretorio. Foi o que permitia a arvore de
    // mods ser desviada por link simbolico antes de o root tocar nela.
    //
    // Com a arvore de mods em /data/adb/bepinex (root:root 0700) e o jogo
    // recebendo FD em vez de abrir caminho, nao ha mais nada que o jogo
    // precise escrever em /data/local/tmp. Se o snapshot voltar a faltar, o
    // sintoma e log do lado do companion, e nao um chmod que abre o
    // diretorio para o aparelho inteiro.

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

// (bc_serve_request_channel foi removido: o canal de pedidos chega pelo
// @bc_companion — o accept loop adota a conexão REQ direto em req_channel_thread.)
