#include <stdlib.h>
#include <stdio.h>

#include "./trace_local.h"

#define cilk_for for
#define cilk_spawn
#define cilk_sync

typedef struct {
  int size;
  int count;
  int* values;
} array_t;

void* new_array() {
  array_t* arr = (array_t*) malloc(sizeof(array_t));
  arr->size = 8;
  arr->count = 0;
  arr->values = malloc(sizeof(int)*8);;
  return (void*) arr;
}

void array_append(array_t* arr, int value) {
  if (arr->size == arr->count) {
    arr->size *= 2;
    arr->values = realloc(arr->values, arr->size*sizeof(int));
  }
  arr->values[arr->count++] = value;
}

void branchy_branchy(tlv_id id, int left, int right) {
  if (left == right) {
    array_t* arr = (array_t*) get_trace_local(id);
    array_append(arr, left);
    return;
  }
  int difference = right - left;
  int middle = left + difference/2;
  cilk_spawn branchy_branchy(id, left, middle);
  branchy_branchy(id, middle+1, right);
  cilk_sync;
}

void traverse_array(tlv_id id) {
  trace_collection collection = collect_trace_local(id);
  trace_view* trace = collection.head;
  while (trace != NULL) {
    array_t* arr = (array_t*) trace->view;
    for (int i = 0; i < arr->count; i++) {
      printf("%d\n", arr->values[i]);
    }
    free(arr->values);
    free(arr);
    trace = trace->next;
  }
}

#define COUNT ((1 << 20) - 1)

int main() {
  initialize_rt();
  tlv_id id = create_trace_local(&new_array);
  branchy_branchy(id, 0, COUNT);
  traverse_array(id);
  delete_trace_local(id);
  deinitialize_rt();
  return 0;
}
