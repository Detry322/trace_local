#include <stdlib.h>
#include <stdio.h>
#include <cilk/cilk.h>

#include "./trace_local.h"

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
  int val = 0;
  while (trace != NULL) {
    array_t* arr = (array_t*) trace->view;
    for (int i = 0; i < arr->count; i++) {
      if (arr->values[i] != val) {
        printf("EXECUTION NOT IN ORDER! (%d, %d)\n", arr->values[i], val);
        exit(1);
      }
      val += 1;
    }
    free(arr->values);
    free(arr);
    trace = trace->next;
  }
}

#define COUNT ((1 << 20) - 1)

void test1() {
  tlv_id id = create_trace_local(&new_array);
  branchy_branchy(id, 0, COUNT);
  traverse_array(id);
  delete_trace_local(id);
}

void test2() {
  tlv_id id = create_trace_local(&new_array);
  for (int i = 0; i < 100; i++) {
    cilk_for (int j = 0; j < 10000; j++) {
      array_t* arr = (array_t*) get_trace_local(id);
      array_append(arr, i*10000+j);
    }
  }
}

void test3() {
  tlv_id id = create_trace_local(&new_array);
  cilk_for (int i = 0; i < 100; i++) {
    cilk_for (int j = 0; j < 10000; j++) {
      array_t* arr = (array_t*) get_trace_local(id);
      array_append(arr, i*10000+j);
    }
  }
}

int main() {
  initialize_rt();
  test1();
  test2();
  test3();
  deinitialize_rt();
  return 0;
}
