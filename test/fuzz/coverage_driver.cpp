// coverage_driver.cpp — main() mínimo para a build de COBERTURA.
//
// O harness é escrito para o libFuzzer (é ele quem chama
// LLVMFuzzerTestOneInput). Para medir cobertura por gcov, que exige um
// executável normal, este driver reproduz o que o libFuzzer faz: chama
// LLVMFuzzerTestOneInput uma vez por arquivo do corpus. Nenhuma lógica de
// parse mora aqui — se este arquivo tivesse lógica, a cobertura_medida não
// seria a cobertura do parser.
//
// NÃO entra no build do gate (lá quem fornece o main é o libFuzzer).
// Uso:
//   clang++ -std=c++17 -g -O0 --coverage -fsanitize=address,undefined \
//       -I jni -c coverage_driver.cpp -o driver.o
//   clang++ -std=c++17 -g -O0 --coverage -fsanitize=address,undefined \
//       -I jni -c fuzz_<alvo>.cpp -o fuzz.o
//   clang++ --coverage -fsanitize=address,undefined driver.o fuzz.o -o cov_<alvo>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s <arquivo-de-entrada>...\n", argv[0]);
        return 2;
    }
    long long total = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (f == nullptr) {
            fprintf(stderr, "nao abri %s\n", argv[i]);
            continue;
        }
        std::vector<unsigned char> buf;
        unsigned char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
            buf.insert(buf.end(), chunk, chunk + n);
        fclose(f);
        if (buf.size() > 256u * 1024u) buf.resize(256u * 1024u);
        LLVMFuzzerTestOneInput(buf.data(), buf.size());
        total++;
    }
    fprintf(stderr, "coverage_driver: %lld entradas\n", total);
    return 0;
}
