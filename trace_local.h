#include <stdint.h>

#ifndef _TRACE_LOCAL_H
#define _TRACE_LOCAL_H

typedef uint64_t trace_local_id;
typedef struct {
  trace_local_id id;
  int length;
  void** items;
} trace_local_collection;

// Creates a trace local variable
trace_local_id create_trace_local();

// Gets the local instance of a trace local variable
void* get_trace_local(trace_local_id id);

// Sets the local instance of a trace local variable
void set_trace_local(trace_local_id id, void* value);

// Collects all the trace local variables and destroys the variable
trace_local_collection collect_trace_local(trace_local_id id);

#endif
