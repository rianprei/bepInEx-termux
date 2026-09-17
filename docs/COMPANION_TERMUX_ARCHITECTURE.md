# Arquitetura: Zygisk Companion Process ↔ Termux via Unix Domain Socket

**Data:** 2026-09-13
**Objetivo:** Documentar arquitetura para conectar companion process (root) ao Termux (non-root) via Unix socket

---

## Contexto Atual

**Zygisk BC POC:**
- Mod-loader roda como .so dentro do processo do Battle Cats (sandboxed)
- Sem terminal ou integração externa
- Companion process (Kilo) roda como root fora do sandbox
- Termux é app separado no mesmo device (não root)

**Problema:**
- Como expor comandos do companion process para Termux?
- Companion process tem privilégios root, Termux não
- SELinux bloqueia comunicação direta entre processos root e untrusted_app

---

## Arquitetura Recomendada: Abstract Unix Domain Socket

### Diagrama

```
┌─────────────────┐         ┌──────────────────┐
│  Battle Cats    │         │   Companion      │
│  (Zygisk mod)   │         │   (Root)         │
│                 │         │                  │
│  .so in process │◄───────►│  Daemon          │
│  connects via   │  Zygisk │  Listens on      │
│  Api::connect   │  IPC    │  @bc_companion   │
└─────────────────┘         └────────▲─────────┘
                                      │
                                      │ Abstract Socket
                            ┌─────────┴─────────┐
                            │     Termux       │
                            │  (untrusted_app) │
                            │  Connects to     │
                            │  @bc_companion   │
                            └──────────────────┘
```

### Por que Abstract Socket?

**Vantagens:**
- Funciona sem filesystem compartilhado
- Sem problemas de permissão de arquivo
- Proven pattern no Termux (Termux:GUI usa)
- Simples de implementar
- Funciona across Android/Termux boundary

**Desvantagens:**
- Sem access control built-in (qualquer processo pode conectar)
- Requer autenticação application-level (SO_PEERCRED)
- Nomes visíveis em `/proc/net/unix`

---

## Implementação

### 1. Companion Process - Criar Socket

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "BC_COMPANION"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Socket name (abstract namespace - começa com null byte)
static const char *SOCKET_NAME = "bc_companion";

// Cria servidor de socket abstract
int setup_abstract_socket(const char *name) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';  // Abstract namespace
    strncpy(addr.sun_path + 1, name, sizeof(addr.sun_path) - 2);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
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
```

### 2. Companion Process - Autenticar Conexões

```c
#include <sys/socket.h>
#include <sys/un.h>

// Obtém credenciais do peer (UID, PID, GID)
int getpeercred(int fd, struct ucred *cred) {
    socklen_t len = sizeof(*cred);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, cred, &len);
}

// Descobre UID do Termux dinamicamente
uid_t get_termux_uid() {
    struct stat st;
    if (stat("/data/data/com.termux/", &st) == 0) {
        return st.st_uid;
    }
    LOGE("failed to stat /data/data/com.termux/");
    return -1;
}

// Verifica se UID é do Termux
bool is_termux_uid(uid_t uid) {
    uid_t termux_uid = get_termux_uid();
    if (termux_uid < 0) {
        // Fallback: permite qualquer UID com challenge-response
        return true;
    }
    return uid == termux_uid;
}
```

### 3. Companion Process - Loop de Aceitação

```c
void companion_handler(int zygisk_socket) {
    // Lida com requisições do Zygisk module
    // ...

    // Setup socket para Termux
    int termux_server = setup_abstract_socket(SOCKET_NAME);
    if (termux_server < 0) {
        LOGE("failed to setup Termux socket");
        return;
    }

    LOGI("companion ready for Termux connections");

    while (1) {
        int client = accept(termux_server, NULL, NULL);
        if (client < 0) {
            LOGE("accept() failed: %s", strerror(errno));
            continue;
        }

        // Autentica conexão
        struct ucred cred;
        if (getpeercred(client, &cred) == 0) {
            LOGI("connection from UID=%d PID=%d", cred.uid, cred.pid);

            if (is_termux_uid(cred.uid)) {
                handle_termux_request(client);
            } else {
                LOGE("rejected connection from UID=%d (not Termux)", cred.uid);
            }
        } else {
            LOGE("getpeercred() failed: %s", strerror(errno));
        }

        close(client);
    }
}
```

### 4. Termux - Conectar via Python

```python
import socket
import os

# Conecta ao socket abstract
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('\0bc_companion')

# Envia comando
sock.send(b'ping')

# Recebe resposta
response = sock.recv(4096)
print(f"Response: {response.decode()}")

sock.close()
```

### 5. Termux - Conectar via nc (netcat)

**NOTA:** Termux nc padrão NÃO suporta Unix sockets. Usar Python ou compilar socat com suporte Unix.

```bash
# Via Python (recomendado)
python3 -c "
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('\0bc_companion')
s.send(b'ping')
print(s.recv(4096).decode())
s.close()
"
```

---

## Segurança

### Riscos

1. **Qualquer app pode conectar** ao socket abstract
2. **App malicioso** pode enviar comandos ao processo root
3. **Enumeração de socket** via `/proc/net/unix`

### Mitigações

#### 1. SO_PEERCRED Authentication

```c
// Já implementado acima
struct ucred cred;
getpeercred(client_fd, &cred);
if (!is_termux_uid(cred.uid)) {
    close(client_fd);
    return;
}
```

#### 2. Challenge-Response Protocol

```c
// Companion envia challenge aleatório
unsigned char challenge[16];
read(urandom, challenge, sizeof(challenge));
write(client_fd, challenge, sizeof(challenge));

// Espera resposta (hash do challenge + secret)
unsigned char response[32];
read(client_fd, response, sizeof(response));

// Verifica resposta
if (!verify_response(challenge, response)) {
    LOGE("challenge-response failed");
    close(client_fd);
    return;
}
```

```python
# Termux implementa challenge-response
challenge = sock.recv(16)
response = hmac_sha256(challenge, SHARED_SECRET)
sock.send(response)
```

#### 3. Command Whitelist

```c
// Companion só aceita comandos seguros
const char *ALLOWED_COMMANDS[] = {
    "ping",
    "get_status",
    "list_hooks",
    "set_hook",
    // NÃO incluir comandos perigosos como "exec", "shell"
    NULL
};

bool is_command_allowed(const char *cmd) {
    for (int i = 0; ALLOWED_COMMANDS[i]; i++) {
        if (strcmp(cmd, ALLOWED_COMMANDS[i]) == 0) {
            return true;
        }
    }
    return false;
}
```

#### 4. Rate Limiting

```c
// Limita conexões por UID
#define MAX_CONNECTIONS_PER_MINUTE 10

struct rate_limit {
    uid_t uid;
    int count;
    time_t last_reset;
};

// Implementar rate limiting por UID
```

#### 5. Audit Logging

```c
// Log todas as conexões e comandos
LOGI("audit: UID=%d PID=%d cmd=%s", cred.uid, cred.pid, command);
```

#### 6. Socket Name Obfuscation

```c
// Usa nome não óbvio
static const char *SOCKET_NAME = "bc_internal_" RANDOM_SUFFIX;
```

---

## Alternativas Consideradas

### Opção 1: Filesystem Socket em /data/local/tmp

**Problema:** SELinux bloqueia untrusted_app acessando `/data/local/tmp`

```
avc: denied { connectto } for scontext=u:r:untrusted_app:s0 tcontext=u:r:shell_data_file:s0
```

**Solução:** Modificar SELinux policy (NÃO recomendado)

```bash
magiskpolicy --live "allow untrusted_app_all magisk unix_stream_socket { connectto }"
```

**Por que NÃO:**
- Quebra modelo de segurança do Android
- Permite QUALQUER app untrusted conectar ao processo root
- Viola neverallow rules
- Pode quebrar CTS compliance

### Opção 2: TCP Localhost (127.0.0.1)

**Problema:** `CONFIG_ANDROID_PARANOID_NETWORK` restringe networking por grupo

**Solução:** Adicionar usuário ao grupo `aid_inet`

```bash
usermod -a -G aid_inet termux
```

**Por que NÃO:**
- Mais complexo que Unix socket
- Menos eficiente (overhead TCP)
- Problemas de firewall
- Requer configuração adicional

### Opção 3: Reverse Connection (Termux cria socket)

**Arquitetura:**
1. Termux cria socket listening
2. Termux envia path para companion via algum mecanismo
3. Companion conecta ao socket do Termux

**Desafio:** Como Termux comunica o path do socket ao companion?

**Possíveis soluções:**
- Arquivo compartilhado em local acessível
- Broadcast Intent (requer componente app do Termux)
- Companion polla local conhecido

**Por que NÃO:**
- Mais complexo
- Requer mecanismo adicional de descoberta
- Menos direto que companion criando socket

---

## Checklist de Implementação

### Companion Process

- [ ] Criar socket abstract com nome único
- [ ] Implementar `getpeercred()` para autenticação
- [ ] Descobrir UID do Termux dinamicamente
- [ ] Implementar challenge-response protocol
- [ ] Implementar command whitelist
- [ ] Implementar rate limiting
- [ ] Implementar audit logging
- [ ] Testar conexão do Termux
- [ ] Testar rejeição de conexões não autorizadas

### Termux

- [ ] Script Python para conectar ao socket
- [ ] Implementar challenge-response no cliente
- [ ] Testar envio de comandos
- [ ] Testar recebimento de respostas
- [ ] Documentar comandos disponíveis

---

## Código de Exemplo Completo

### companion.cpp

```cpp
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <android/log.h>
#include <fcntl.h>
#include <stdlib.h>

#define LOG_TAG "BC_COMPANION"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char *SOCKET_NAME = "bc_companion";

int setup_abstract_socket(const char *name) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, name, sizeof(addr.sun_path) - 2);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
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

bool is_termux_uid(uid_t uid) {
    uid_t termux_uid = get_termux_uid();
    if (termux_uid < 0) {
        // Fallback: permite qualquer UID (com challenge-response)
        return true;
    }
    return uid == termux_uid;
}

void handle_termux_request(int client_fd) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        LOGI("received command: %s", buf);

        // Processa comando
        const char *response = "OK";
        write(client_fd, response, strlen(response));
    }
}

void companion_handler(int zygisk_socket) {
    // Lida com Zygisk module (se necessário)
    // ...

    // Setup socket para Termux
    int termux_server = setup_abstract_socket(SOCKET_NAME);
    if (termux_server < 0) {
        LOGE("failed to setup Termux socket");
        return;
    }

    LOGI("companion ready for Termux connections");

    while (1) {
        int client = accept(termux_server, NULL, NULL);
        if (client < 0) {
            LOGE("accept() failed: %s", strerror(errno));
            continue;
        }

        struct ucred cred;
        if (getpeercred(client, &cred) == 0) {
            LOGI("connection from UID=%d PID=%d", cred.uid, cred.pid);

            if (is_termux_uid(cred.uid)) {
                handle_termux_request(client);
            } else {
                LOGE("rejected connection from UID=%d (not Termux)", cred.uid);
            }
        } else {
            LOGE("getpeercred() failed: %s", strerror(errno));
        }

        close(client);
    }
}

REGISTER_ZYGISK_COMPANION(companion_handler)
```

### termux_client.py

```python
#!/usr/bin/env python3
import socket
import sys

SOCKET_NAME = '\0bc_companion'

def send_command(cmd):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(SOCKET_NAME)
        sock.send(cmd.encode())
        response = sock.recv(4096)
        return response.decode()
    finally:
        sock.close()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 termux_client.py <command>")
        sys.exit(1)

    cmd = ' '.join(sys.argv[1:])
    response = send_command(cmd)
    print(response)
```

---

## Referências

**Fontes:**
- Magisk Zygisk API: https://github.com/topjohnwu/Magisk/blob/master/native/src/core/zygisk/api.hpp
- Android Unix Socket Documentation: https://developer.android.com/reference/android/net/LocalSocketAddress.Namespace
- Termux:GUI (abstract socket example): https://github.com/tareksander/termux-gui-python-bindings
- Magisk Issue #3697 (SELinux blocking): https://github.com/topjohnwu/Magisk/issues/3697
- SAUSAGE Paper (Unix socket security): https://arxiv.org/pdf/2204.01516
- SO_PEERCRED Documentation: https://man7.org/linux/man-pages/man7/unix.7.html

**Padrões Proven:**
- Termux:GUI usa abstract sockets para comunicação plugin ↔ Termux
- Termux:API mudou de abstract para filesystem sockets por segurança
- Magisk usa Unix sockets para comunicação daemon ↔ module

---

## Próximos Passos

1. **Implementar companion process** com socket abstract
2. **Testar conexão do Termux** via Python
3. **Implementar autenticação** (SO_PEERCRED + challenge-response)
4. **Implementar command whitelist** para segurança
5. **Documentar comandos disponíveis** para Termux
6. **Testar rate limiting** e logging
7. **Considerar adicionais** (cifra de socket, assinatura de comandos)
