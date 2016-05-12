#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <cilk/cilk.h>
#include "./tl_log.h"

struct tl_log_struct {
  tlv_id id;
  const char* out_filename;
};

typedef struct {
  FILE* fp;
  const char* temp_filename;
} tl_log_trace;

static pthread_mutex_t rand_mutex;

static void* _initialize_trace() {
  char temp_filename[200];
  pthread_mutex_lock(&rand_mutex);
  sprintf(temp_filename, "tl_log_tmp_%d_%d_%d_%d_%d", rand(), rand(), rand(), rand(), rand());
  pthread_mutex_unlock(&rand_mutex);
  tl_log_trace* t = malloc(sizeof(tl_log_trace));
  t->temp_filename = strdup(temp_filename);
  t->fp = fopen(t->temp_filename, "w");
  return (void*) t;
};

tl_log tl_log_new(const char* log_filename) {
  tl_log x;
  x.id = create_trace_local(&_initialize_trace);
  x.out_filename = strdup(log_filename);
  return x;
}

void tl_log_writeline(tl_log l, const char* line) {
  FILE* outfile = ((tl_log_trace*) get_trace_local(l.id))->fp;
  fprintf(outfile, "%s\n", line);
}

static void append(FILE* dest, FILE* src) {
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    fwrite(buf, 1, n, dest);
  }
}

void tl_log_flush(tl_log l) {
  trace_collection collection = collect_trace_local(l.id);
  trace_view* trace = collection.head;
  FILE* outfile = fopen(l.out_filename, "a");
  while (trace != NULL) {
    tl_log_trace* view = (tl_log_trace*) trace->view;
    fclose(view->fp);
    FILE* readfile = fopen(view->temp_filename, "r");
    append(outfile, readfile);
    fclose(readfile);
    unlink(view->temp_filename);
    free((void*) view->temp_filename);
    free(view);
    trace = trace->next;
  }
  fclose(outfile);
  delete_trace_local(l.id);
}

#define COUNT ((1 << 20) - 1)

static void _branchy_branchy(tl_log l, int left, int right) {
  if (left == right) {
    char buf[16];
    sprintf(buf, "%d", left);
    tl_log_writeline(l, buf);
    return;
  }
  int difference = right - left;
  int middle = left + difference/2;
  cilk_spawn _branchy_branchy(l, left, middle);
  _branchy_branchy(l, middle+1, right);
  cilk_sync;
}


void test_tl_log() {
  tl_log l = tl_log_new("tl_log_test.txt");
  _branchy_branchy(l, 0, COUNT);
  tl_log_flush(l);
  FILE* output = fopen("tl_log_test.txt", "r");
  char* buffer = NULL;
  size_t bytes = 0;
  int i = 0;
  while (getline(&buffer, &bytes, output)) {
    int num;
    sscanf(buffer, "%d", &num);
    if (num != i) {
      printf("FILE IS OUT OF ORDER!\n");
      exit(1);
    }
    i += 1;
  }
  fclose(output);
  unlink("tl_log_test.txt");
  free(buffer);
};
