// smoke.js — F11: prova de que o gadget executou o script DENTRO do processo
// do jogo. Grava uma marca no dir C1 (files/bepinex/) e loga no logcat.
const M = "/data/data/com.hyperdotstudios.swampattack2/files/bepinex/frida-smoke.txt";
try {
    const f = new File(M, "w");
    f.write("frida smoke ok " + new Date().toISOString() + "\n");
    f.flush();
    f.close();
    console.log("frida smoke: ARQUIVO GRAVADO em " + M);
} catch (e) {
    console.error("smoke falhou: " + e);
}
