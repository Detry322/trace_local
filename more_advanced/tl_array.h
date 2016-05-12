#include <stdint.h>
#include "./trace_local.h"

#ifndef _TL_ARRAY_H
#define _TL_ARRAY_H

typedef tlv_id tl_array;

tl_array tl_array_new();
void tl_array_append(tl_array id, int value);
int* tl_array_to_array(tl_array id, int* length_ptr);

void test_tl_array();

#endif
