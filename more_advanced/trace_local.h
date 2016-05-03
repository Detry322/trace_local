#include <stdint.h>

#ifndef _TRACE_LOCAL_H
#define _TRACE_LOCAL_H

#define MAX_TL_VARS 256

typedef uint64_t tlv_id;

typedef struct trace_view_struct trace_view;

struct trace_view_struct {
  void* view;
  trace_view* next;
  trace_view* prev;
};

typedef struct {
  trace_view* head;
  trace_view* tail;
  int length;
} trace_collection;

typedef void* (*trace_initializer)();

void initialize_rt();
void deinitialize_rt();

// Creates a trace local variable
tlv_id create_trace_local(trace_initializer initializer);

// Gets the local instance of a trace local variable
void* get_trace_local(tlv_id id);

// Collects all the instances of the variable in the execution strand
trace_collection collect_trace_local(tlv_id id);

// Removes a trace local variable so we don't have to keep track of it anymore
void delete_trace_local(tlv_id id);

#endif
