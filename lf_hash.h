#ifndef _LF_HASH_H
#define _LF_HASH_H

#include <stdlib.h> /* for malloc/free/etc */
#include <stdio.h>
#include <unistd.h> /* for getpagesize() */
#include <assert.h>
#include <inttypes.h> /* for PRI___ macros (printf) */

typedef const void *qt_key_t;
typedef struct qt_hash_s *qt_hash;
typedef void (*qt_hash_callback_fn)(const qt_key_t, void *, void *);
typedef void (*qt_hash_deallocator_fn)(void *);

int qt_hash_put(qt_hash  h,
                qt_key_t key,
                void    *value);

void *qt_hash_get(qt_hash        h,
                  const qt_key_t key);

int qt_hash_remove(qt_hash        h,
                   const qt_key_t key);

qt_hash qt_hash_create(int needSync);

size_t qt_hash_count(qt_hash h);

void qt_hash_callback(qt_hash             h,
                      qt_hash_callback_fn f,
                      void               *arg);

void qt_hash_callback(qt_hash             h,
                      qt_hash_callback_fn f,
                      void               *arg);

void qt_hash_destroy_deallocate(qt_hash                h,
                                qt_hash_deallocator_fn f);

void qt_hash_destroy(qt_hash h);
#endif
