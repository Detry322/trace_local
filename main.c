#include <stdlib.h>
#include <stdio.h>

#include "./trace_local.h"
// #include <cilk/cilk.h>
// #include <cilk/cilk_api.h>

#define cilk_for for
#define cilk_spawn

int branchy_decline(int depth) {
  if (depth < 0)
    return 0;
  trace_local_id var_id = create_trace_local();
  cilk_for (int i = 0; i < 8; i++) {
    int* var = get_trace_local(var_id);
    if (var == NULL) {
      var = malloc(sizeof(int));
      *var = 0;
      set_trace_local(var_id, var);
    }
    for (int c = 0; c < 1000; c++) {
      *var += c;
    }
    *var += branchy_decline(depth - 1);
  }
  trace_local_collection collect_results = collect_trace_local(var_id);
  int total = 0;
  for (int i = 0; i < collect_results.length; i++) {
    total += * (int*) collect_results.items[i];
    free((void*) collect_results.items[i]);
  }
  return total;
}

int main() {
  printf("%d\n", branchy_decline(5));
  return 0;
}
