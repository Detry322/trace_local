#include "./cilk_api.h"
#include <pthread.h>

#include "./trace_local.h"
#include "./lf_hash.h"

typedef uint64_t trace_id;

qt_hash tl_map = NULL;
pthread_mutex_t tl_map_mutex;
trace_local_id counter = 1;

static inline trace_id get_trace_id() {
  int worker_id;
  int steal_count = __cilkrts_get_steal_count(&worker_id);
  return (((uint64_t) worker_id) << 32L) | steal_count;
}

// Creates a trace local variable
trace_local_id create_trace_local() {
  if (tl_map == NULL) {
    pthread_mutex_lock(&tl_map_mutex);
    if (tl_map == NULL)
      tl_map = qt_hash_create(0);
    pthread_mutex_unlock(&tl_map_mutex);
  }
  trace_local_id new_id = __sync_fetch_and_add(&counter, 1);
  qt_hash_put(tl_map, (qt_key_t) new_id, qt_hash_create(0));
  return new_id;
}

// Gets the local instance of a trace local variable
void* get_trace_local(trace_local_id id) {
  trace_id tid = get_trace_id();
  qt_hash trace_local_map = (qt_hash) qt_hash_get(tl_map, (qt_key_t) id);
  return qt_hash_get(trace_local_map, (qt_key_t) tid);
}

// Sets the local instance of a trace local variable
void set_trace_local(trace_local_id id, void* value) {
  trace_id tid = get_trace_id();
  qt_hash trace_local_map = (qt_hash) qt_hash_get(tl_map, (qt_key_t) id);
  qt_hash_put(trace_local_map, (qt_key_t) tid, value);
}

// Collects all the trace local variables and destroys the variable
trace_local_collection collect_trace_local(trace_local_id id) {
  trace_local_collection c;
  qt_hash trace_local_map = (qt_hash) qt_hash_get(tl_map, (qt_key_t) id);
  qt_hash_remove(tl_map, (qt_key_t) id);
  c.id = id;
  c.length = (int) qt_hash_count(trace_local_map);
  c.items = qt_hash_values(trace_local_map);
  qt_hash_destroy(trace_local_map);
  return c;
}
