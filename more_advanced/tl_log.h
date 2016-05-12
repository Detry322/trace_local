#include <stdint.h>
#include "./trace_local.h"

#ifndef _TL_LOG_H
#define _TL_LOG_H

typedef struct tl_log_struct tl_log;

tl_log tl_log_new(const char* log_filename);
void tl_log_writeline(tl_log log, const char* line);
void tl_log_flush(tl_log log);

void test_tl_log();

#endif
