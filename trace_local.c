#include "./cilk_api.h"

#include "./trace_local.h"
#include "./lf_hash.h"

// Creates a trace local variable
trace_local_id create_trace_local() {
  return 0;
}

// Gets the local instance of a trace local variable
void** get_trace_local(trace_local_id id) {
  return NULL;
}

// Collects all the trace local variables and destroys the variable
trace_local_collection collect_trace_local(trace_local_id id) {
  trace_local_collection c;
  return c;
}
