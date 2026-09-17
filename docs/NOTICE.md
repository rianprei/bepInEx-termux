# Atribuição de terceiros

Este repositório redistribui um binário de terceiros pra facilitar o build:

- **`jni/lib/arm64-v8a/libdobby.a`** — [Dobby](https://github.com/jmpews/Dobby)
  (jmpews), biblioteca de inline-hook ARM64, licença MIT. Usada para
  interceptar os exports JNI do processo alvo em `jni/main.cpp`.

Todo o restante do código (módulo Zygisk, companion, cliente Termux, scripts)
é original deste projeto.
