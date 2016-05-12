#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <cilk/cilk.h>

#include "./trace_local.h"
#include "./tl_array.h"
#include "./tl_log.h"

int main() {
  srand(time(NULL));
  initialize_rt();
  test_tl_array();
//  test_tl_log();
  deinitialize_rt();
  return 0;
}
