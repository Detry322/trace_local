#include <stdlib.h>
#include <assert.h>
#include <cilk/cilk.h>
#include "./tl_array.h"

typedef struct {
  int size;
  int count;
  int* values;
} internal_array;

static void* _new_array() {
  internal_array* arr = (internal_array*) malloc(sizeof(internal_array));
  arr->size = 8;
  arr->count = 0;
  arr->values = malloc(sizeof(int)*8);
  return (void*) arr;
}

static void _array_append(internal_array* arr, int value) {
  if (arr->size == arr->count) {
    arr->size *= 2;
    arr->values = realloc(arr->values, arr->size*sizeof(int));
  }
  arr->values[arr->count++] = value;
}

tl_array tl_array_new() {
  return create_trace_local(&_new_array);
}

void tl_array_append(tl_array id, int value) {
  internal_array* arr = (internal_array*) get_trace_local(id);
  _array_append(arr, value);
}

int* tl_array_to_array(tl_array id, int* length_ptr) {
  int current_length = 0;
  int size = 8;
  int* result = malloc(sizeof(int)*8);
  trace_collection collection = collect_trace_local(id);
  trace_view* trace = collection.head;
  while (trace != NULL) {
    internal_array* view = (internal_array*) trace->view;
    while (current_length + view->count > size) {
      size *= 2;
      result = realloc(result, size*sizeof(int));
    }
    memcpy(result+current_length, view->values, view->count*sizeof(int));
    current_length += view->count;
    trace = trace->next;
  }
  *length_ptr = current_length;
  result = realloc(result, current_length*sizeof(int));
  return result;
}

void tl_array_destroy(tl_array id) {
  trace_collection collection = collect_trace_local(id);
  trace_view* trace = collection.head;
  while (trace != NULL) {
    internal_array* view = (internal_array*) trace->view;
    free(view->values);
    free(view);
    trace = trace->next;
  }
  delete_trace_local(id);
}

#define COUNT ((1 << 20) - 1)

static void _branchy_branchy(tl_array arr, int left, int right) {
  if (left == right) {
    tl_array_append(arr, left);
    return;
  }
  int difference = right - left;
  int middle = left + difference/2;
  cilk_spawn branchy_branchy(arr, left, middle);
  branchy_branchy(arr, middle+1, right);
  cilk_sync;
}

void test_tl_array() {
  tl_array arr = tl_array_new();
  branchy_branchy(arr, 0, COUNT);
  int length;
  int* result = tl_array_to_array(arr, &length);
  tl_array_destroy(arr);
  for (int i = 0; i < length; i++) {
    assert(result[i] == i);
  }
  assert(length == COUNT);
  free(result);
  tl_array_destroy(arr);
};
