// bc_hook_overhead.h — média de overhead do dispatcher, lida pelo companion.
//
// Antes o jogo publicava isso em persist.bc_poc.hook_overhead_us a cada poll
// e o companion lia a property. Em Enforcing o set é negado
// (property_socket write), então o companion — que roda NO MESMO processo —
// lê os contadores diretamente por aqui.
#ifndef BC_HOOK_OVERHEAD_H
#define BC_HOOK_OVERHEAD_H

// Média em microssegundos (0 se nenhuma medição ainda).
double bc_hook_overhead_avg_us();

#endif // BC_HOOK_OVERHEAD_H
