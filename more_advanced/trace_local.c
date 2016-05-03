#include "./trace_local.h"
#include <cilk/reducer.h>
#include <cilk/cilk_api.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef CILK_C_DECLARE_REDUCER(trace_collection) trace_collection_reducer;

/*

PRIVATE TYPES

*/
void tc_reduce(void* key, void* l1, void* l2) {
  trace_collection* list1 = (trace_collection*) l1;
  trace_collection* list2 = (trace_collection*) l2;
  if (list1->tail != NULL)
    list1->tail->next = list2->head;
  else
    list1->head = list2->head;
  if (list2->tail != NULL)
    list1->tail = list2->tail;
  list2->tail = list2->head = NULL;
  list1->length += list2->length;
  list2->length = 0;
}

void tc_identity(void *key, void* value) {
  trace_collection* b = (trace_collection*) value;
  b->length = 0;
  b->head = NULL;
  b->tail = NULL;
}

void tc_destroy(void* key, void* value) {
  trace_view* cur_node = ((trace_collection*) value)->head;
  trace_view* next_node = NULL;
  while (cur_node != NULL) {
    next_node = cur_node->next;
    free(cur_node);
    cur_node = next_node;
  }
}

void tc_add(trace_collection* collection, void* view) {
  trace_view* new_node = malloc(sizeof(trace_view));
  new_node->view = view;
  new_node->next = NULL;
  if (collection->head == NULL) {
    collection->head = new_node;
  } else {
    collection->tail->next = new_node;
  }
  collection->tail = new_node;
  collection->length++;
}

typedef struct {
  bool occupied;
  trace_collection_reducer reducer;
  trace_initializer initializer;
} gtlv_info_t;

typedef struct {
  gtlv_info_t gtrace_info[MAX_TL_VARS];
  tlv_id next_id;
  pthread_mutex_t lock;
} global_table_t;

typedef struct {
  int steal_count;
  void* view;
} wtlv_info_t;

typedef struct {
  wtlv_info_t wtrace_info[MAX_TL_VARS];
} worker_table_t;
/*

STATIC VARIABLES

*/
static worker_table_t* worker_tables;
static global_table_t global_table;
/*

FUNCTIONS BELOW

*/
void initialize_rt() {
  int workers = __cilkrts_get_nworkers();
  worker_tables = calloc(workers*sizeof(worker_table_t), 1);
}

void deinitialize_rt() {
  free(worker_tables);
}

// Creates a trace local variable
tlv_id create_trace_local(trace_initializer initializer) {
  if (global_table.next_id == -1) {
    fprintf(stderr, "Error: No more room for another trace local variable\n");
    exit(1);
  }
  pthread_mutex_lock(&global_table.lock);
  tlv_id retval = global_table.next_id;
  global_table.gtrace_info[retval].occupied = true;
  do {
    global_table.next_id = (global_table.next_id + 1) % MAX_TL_VARS;
    if (!global_table.gtrace_info[global_table.next_id].occupied) {
      break;
    }
  } while (retval != global_table.next_id);
  if (retval == global_table.next_id) {
    global_table.next_id = -1;
  }
  trace_collection_reducer r = CILK_C_INIT_REDUCER(trace_collection, tc_reduce, tc_identity, tc_destroy, {0}/*(trace_collection) { .head = NULL, .tail = NULL, .length = 0}*/);
  CILK_C_REGISTER_REDUCER(r);
  global_table.gtrace_info[retval].reducer = r;
  global_table.gtrace_info[retval].initializer = initializer;
  pthread_mutex_unlock(&global_table.lock);
  return retval;
}

// Gets the local instance of a trace local variable
void* get_trace_local(tlv_id id) {
  int worker_id = 0;
  int steal_count = __cilkrts_get_steal_count(&worker_id);
  if (worker_id == 0) {
    printf("User thread???");
    exit(1);
  } else {
    // Shift back from [1, P] to [0, P-1]
    worker_id -= 1;
  }
  worker_table_t* our_table = &worker_tables[worker_id];
  if (our_table->wtrace_info[id].steal_count == steal_count &&
      our_table->wtrace_info[id].view != NULL) {
    return our_table->wtrace_info[id].view;
  }
  trace_initializer initializer = global_table.gtrace_info[id].initializer;
  trace_collection_reducer reducer = global_table.gtrace_info[id].reducer;
  void* new_view = (*initializer)();
  our_table->wtrace_info[id].view = new_view;
  tc_add(&REDUCER_VIEW(reducer), new_view);
  our_table->wtrace_info[id].steal_count = steal_count;
  return new_view;
}

// Collects all the instances of the variable in the execution strand
trace_collection collect_trace_local(tlv_id id) {
  return REDUCER_VIEW(global_table.gtrace_info[id].reducer);
}

// Removes a trace local variable so we don't have to keep track of it anymore
void delete_trace_local(tlv_id id) {
  pthread_mutex_lock(&global_table.lock);
  CILK_C_UNREGISTER_REDUCER(global_table.gtrace_info[id].reducer);
  global_table.gtrace_info[id].occupied = false;
  pthread_mutex_unlock(&global_table.lock);
}
