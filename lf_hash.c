#include <stdlib.h> /* for malloc/free/etc */
#include <stdio.h>
#include <unistd.h> /* for getpagesize() */
#include <assert.h>
#include <inttypes.h> /* for PRI___ macros (printf) */

#include "./lf_hash.h"
/*
 * The hash table in this file is based on the work by Ori Shalev and Nir Shavit
 * Much of the code is similar to the example code they published in their
 * paper "Split-Ordered Lists: Lock-Free Extensible Hash Tables", but has been
 * modified to support additional semantics.
 *
 * NOTE: I did not implement the Dynamic-Sized Array extension, because I don't
 * expect to support large numbers of entries in the table at the same time,
 * and I'm worried about state creep (this algorithm cannot compact well).
 */

#if defined(__STDC__)
# if defined(__STDC_VERSION__)
#  if (__STDC_VERSION__ >= 199901L)
#   define C99
#  endif
# endif
#endif
#ifndef C99
# error +--------------------------------------------------------+
# error | This code requires C99 support (for gcc, use -std=c99) |
# error +--------------------------------------------------------+
#endif

#define MAX_LOAD     4
#define USE_HASHWORD 1

/* internal types */
typedef uint64_t key_t;
typedef uint64_t so_key_t;
typedef uintptr_t marked_ptr_t;

#define MARK_OF(x)           ((x) & 1)
#define PTR_MASK(x)          ((x) & ~(marked_ptr_t)1)
#define PTR_OF(x)            ((hash_entry *)PTR_MASK(x))
#define CONSTRUCT(mark, ptr) (PTR_MASK((uintptr_t)ptr) | (mark))
#define UNINITIALIZED ((marked_ptr_t)0)

/* These are GCC builtins; other compilers may require inline assembly */
#define CAS(ADDR, OLDV, NEWV) __sync_val_compare_and_swap((ADDR), (OLDV), (NEWV))
#define INCR(ADDR, INCVAL) __sync_fetch_and_add((ADDR), (INCVAL))

size_t hard_max_buckets = 0;

typedef struct hash_entry_s {
    so_key_t     key;
    void        *value;
    marked_ptr_t next;
} hash_entry;

struct qt_hash_s {
    marked_ptr_t   *B;  // Buckets
    volatile size_t count;
    volatile size_t size;
};

/* prototypes */
static void *qt_lf_list_find(marked_ptr_t  *head,
                             so_key_t       key,
                             marked_ptr_t **prev,
                             marked_ptr_t  *cur,
                             marked_ptr_t  *next);
static void initialize_bucket(qt_hash h,
                              size_t  bucket);

#define MSB (((uint64_t)1) << 63)
#define REVERSE_BYTE(x) ((so_key_t)((((((uint32_t)(x)) * 0x0802LU & 0x22110LU) | (((uint32_t)(x)) * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16) & 0xff))
#if 0
/* 32-bit reverse */
# define REVERSE(x) (REVERSE_BYTE((((so_key_t)(x))) & 0xff) << 24) | \
    (REVERSE_BYTE((((so_key_t)(x)) >> 8) & 0xff) << 16) |            \
    (REVERSE_BYTE((((so_key_t)(x)) >> 16) & 0xff) << 8) |            \
    (REVERSE_BYTE((((so_key_t)(x)) >> 24) & 0xff))
#else
# define REVERSE(x) ((REVERSE_BYTE((((so_key_t)(x))) & 0xff) << 56) |       \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 8) & 0xff) << 48) |  \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 16) & 0xff) << 40) | \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 24) & 0xff) << 32) | \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 32) & 0xff) << 24) | \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 40) & 0xff) << 16) | \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 48) & 0xff) << 8) |  \
                     (REVERSE_BYTE((((so_key_t)(x)) >> 56) & 0xff) << 0))
#endif /* if 0 */

#ifdef USE_HASHWORD
# define HASH_KEY(key) key = qt_hashword(key)
/* this function based on http://burtleburtle.net/bob/hash/evahash.html */
# define rot(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
static uint64_t qt_hashword(uint64_t key)
{   /*{{{*/
    uint32_t a, b, c;

    const union {
        uint64_t key;
        uint8_t  b[sizeof(uint64_t)];
    } k = {
        key
    };

    a  = b = c = 0x32533d0c + sizeof(uint64_t); // an arbitrary value, randomly selected
    c += 47;

    b += k.b[7] << 24;
    b += k.b[6] << 16;
    b += k.b[5] << 8;
    b += k.b[4];
    a += k.b[3] << 24;
    a += k.b[2] << 16;
    a += k.b[1] << 8;
    a += k.b[0];

    c ^= b;
    c -= rot(b, 14);
    a ^= c;
    a -= rot(c, 11);
    b ^= a;
    b -= rot(a, 25);
    c ^= b;
    c -= rot(b, 16);
    a ^= c;
    a -= rot(c, 4);
    b ^= a;
    b -= rot(a, 14);
    c ^= b;
    c -= rot(b, 24);
    return ((uint64_t)c + (((uint64_t)b) << 32)) & (~MSB);
} /*}}}*/

#else /* ifdef USE_HASHWORD */
# define HASH_KEY(key)
#endif /* ifdef USE_HASHWORD */

static int qt_lf_list_insert(marked_ptr_t *head,
                             hash_entry   *node,
                             marked_ptr_t *ocur)
{
    so_key_t key = node->key;

    while (1) {
        marked_ptr_t *lprev;
        marked_ptr_t  cur;

        if (qt_lf_list_find(head, key, &lprev, &cur, NULL) != NULL) {                       // needs to set cur/prev
            if (ocur) { *ocur = cur; }
            return 0;
        }
        node->next = CONSTRUCT(0, cur);
        if (CAS(lprev, node->next, CONSTRUCT(0, node)) == CONSTRUCT(0, cur)) {
            if (ocur) { *ocur = cur; }
            return 1;
        }
    }
}

static int qt_lf_list_delete(marked_ptr_t *head,
                             so_key_t      key)
{
    while (1) {
        marked_ptr_t *lprev;
        marked_ptr_t  lcur;
        marked_ptr_t  lnext;

        if (qt_lf_list_find(head, key, &lprev, &lcur, &lnext) == NULL) { return 0; }
        if (CAS(&PTR_OF(lcur)->next, CONSTRUCT(0, lnext), CONSTRUCT(1, lnext)) != CONSTRUCT(0, lnext)) { continue; }
        if (CAS(lprev, CONSTRUCT(0, lcur), CONSTRUCT(0, lnext)) == CONSTRUCT(0, lcur)) {
            free(PTR_OF(lcur));
        } else {
            qt_lf_list_find(head, key, NULL, NULL, NULL);                       // needs to set cur/prev/next
        }
        return 1;
    }
}

static void *qt_lf_list_find(marked_ptr_t  *head,
                             so_key_t       key,
                             marked_ptr_t **oprev,
                             marked_ptr_t  *ocur,
                             marked_ptr_t  *onext)
{
    so_key_t      ckey;
    void         *cval;
    marked_ptr_t *prev = NULL;
    marked_ptr_t  cur  = UNINITIALIZED;
    marked_ptr_t  next = UNINITIALIZED;

    while (1) {
        prev = head;
        cur  = *prev;
        while (1) {
            if (PTR_OF(cur) == NULL) {
                if (oprev) { *oprev = prev; }
                if (ocur) { *ocur = cur; }
                if (onext) { *onext = next; }
                return 0;
            }
            next = PTR_OF(cur)->next;
            ckey = PTR_OF(cur)->key;
            cval = PTR_OF(cur)->value;
            if (*prev != CONSTRUCT(0, cur)) {
                break; // this means someone mucked with the list; start over
            }
            if (!MARK_OF(next)) {  // if next pointer is not marked
                if (ckey >= key) { // if current key > key, the key isn't in the list; if current key == key, the key IS in the list
                    if (oprev) { *oprev = prev; }
                    if (ocur) { *ocur = cur; }
                    if (onext) { *onext = next; }
                    return (ckey == key) ? cval : NULL;
                }
                // but if current key < key, the we don't know yet, keep looking
                prev = &(PTR_OF(cur)->next);
            } else {
                if (CAS(prev, CONSTRUCT(0, cur), CONSTRUCT(0, next)) == CONSTRUCT(0, cur)) {
                    free(PTR_OF(cur));
                } else {
                    break;
                }
            }
            cur = next;
        }
    }
}

static inline so_key_t so_regularkey(const key_t key)
{
    return REVERSE(key | MSB);
}

static inline so_key_t so_dummykey(const key_t key)
{
    return REVERSE(key);
}

int qt_hash_put(qt_hash  h,
                qt_key_t key,
                void    *value)
{
    hash_entry *node = malloc(sizeof(hash_entry)); // XXX: should pull out of a memory pool
    size_t      bucket;
    uint64_t    lkey = (uint64_t)(uintptr_t)key;

    HASH_KEY(lkey);
    bucket = lkey % h->size;

    assert(node);
    assert((lkey & MSB) == 0);
    node->key   = so_regularkey(lkey);
    node->value = value;
    node->next  = UNINITIALIZED;

    if (h->B[bucket] == UNINITIALIZED) {
        initialize_bucket(h, bucket);
    }
    if (!qt_lf_list_insert(&(h->B[bucket]), node, NULL)) {
        free(node);
        return 0;
    }
    size_t csize = h->size;
    if (INCR(&h->count, 1) / csize > MAX_LOAD) {
        if (2 * csize <= hard_max_buckets) { // this caps the size of the hash
            CAS(&h->size, csize, 2 * csize);
        }
    }
    return 1;
}

void *qt_hash_get(qt_hash        h,
                  const qt_key_t key)
{
    size_t   bucket;
    uint64_t lkey = (uint64_t)(uintptr_t)key;

    HASH_KEY(lkey);
    bucket = lkey % h->size;

    if (h->B[bucket] == UNINITIALIZED) {
        // You'd think returning NULL at this point would be a good idea; but
        // if we do that, we risk losing key/value pairs (incorrectly reporting
        // them as absent) when the hash table resizes
        initialize_bucket(h, bucket);
    }
    return qt_lf_list_find(&(h->B[bucket]), so_regularkey(lkey), NULL, NULL, NULL);
}

int qt_hash_remove(qt_hash        h,
                   const qt_key_t key)
{
    size_t   bucket;
    uint64_t lkey = (uint64_t)(uintptr_t)key;

    HASH_KEY(lkey);
    bucket = lkey % h->size;

    if (h->B[bucket] == UNINITIALIZED) {
        initialize_bucket(h, bucket);
    }
    if (!qt_lf_list_delete(&(h->B[bucket]), so_regularkey(lkey))) {
        return 0;
    }
    INCR(&h->count, -1);
    return 1;
}

static inline size_t GET_PARENT(uint64_t bucket)
{
    uint64_t t = bucket;

    t |= t >> 1;
    t |= t >> 2;
    t |= t >> 4;
    t |= t >> 8;
    t |= t >> 16;
    t |= t >> 32;     // creates a mask
    return bucket & (t >> 1);
}

static void initialize_bucket(qt_hash h,
                              size_t  bucket)
{
    size_t       parent = GET_PARENT(bucket);
    marked_ptr_t cur;

    if (h->B[parent] == UNINITIALIZED) {
        initialize_bucket(h, parent);
    }
    hash_entry *dummy = malloc(sizeof(hash_entry)); // XXX: should pull out of a memory pool
    assert(dummy);
    dummy->key   = so_dummykey(bucket);
    dummy->value = NULL;
    dummy->next  = UNINITIALIZED;
    if (!qt_lf_list_insert(&(h->B[parent]), dummy, &cur)) {
        free(dummy);
        dummy = PTR_OF(cur);
        while (h->B[bucket] != CONSTRUCT(0, dummy)) ;
    } else {
        h->B[bucket] = CONSTRUCT(0, dummy);
    }
}

qt_hash qt_hash_create(int needSync)
{
    qt_hash tmp = malloc(sizeof(struct qt_hash_s));

    assert(tmp);
    if (hard_max_buckets == 0) {
        hard_max_buckets = getpagesize()/sizeof(marked_ptr_t);
    }
    tmp->B = calloc(hard_max_buckets, sizeof(marked_ptr_t));
    assert(tmp->B);
    tmp->size  = 2;
    tmp->count = 0;
    {
        hash_entry *dummy = calloc(1, sizeof(hash_entry)); // XXX: should pull out of a memory pool
        assert(dummy);
        tmp->B[0] = CONSTRUCT(0, dummy);
    }
    return tmp;
}

void qt_hash_destroy(qt_hash h)
{
    marked_ptr_t cursor;

    assert(h);
    assert(h->B);
    cursor = h->B[0];
    while (PTR_OF(cursor) != NULL) {
        marked_ptr_t tmp = cursor;
        assert(MARK_OF(tmp) == 0);
        cursor = PTR_OF(cursor)->next;
        free(PTR_OF(tmp));
    }
    free(h->B);
    free(h);
}

void qt_hash_destroy_deallocate(qt_hash                h,
                                qt_hash_deallocator_fn f)
{
    marked_ptr_t cursor;

    assert(h);
    assert(h->B);
    cursor = h->B[0];
    while (PTR_OF(cursor) != NULL) {
        marked_ptr_t tmp = cursor;
        assert(MARK_OF(tmp) == 0);
        cursor = PTR_OF(cursor)->next;
        f(PTR_OF(cursor));
        free(PTR_OF(tmp));
    }
    free(h->B);
    free(h);
}

size_t qt_hash_count(qt_hash h)
{
    assert(h);
    return h->count;
}

void* qt_hash_values(qt_hash h) {
    int total = qt_hash_count(h);
    void** values_array = malloc(sizeof(void*) * total);
    int index = 0;
    marked_ptr_t cursor;
    cursor = h->B[0];
    while (index < total) {
        cursor = PTR_OF(cursor)->next;
        values_array[index++] = PTR_OF(cursor)->value;
    }
    return values_array;
};

void qt_hash_callback(qt_hash             h,
                      qt_hash_callback_fn f,
                      void               *arg)
{
    marked_ptr_t cursor;

    assert(h);
    assert(h->B);
    cursor = h->B[0];
    while (PTR_OF(cursor) != NULL) {
        so_key_t     key = PTR_OF(cursor)->key;
        assert(MARK_OF(tmp) == 0);
        // printf("%p = ", PTR_OF(cursor));
        if (key & 1) {
            f((qt_key_t)(uintptr_t)REVERSE(key ^ 1), PTR_OF(cursor)->value, arg);
        } else {
            // f((qt_key_t)REVERSE(key), PTR_OF(cursor)->value, (void *)1);
        }
        cursor = PTR_OF(cursor)->next;
    }
    /*
     * for (size_t i=0; i<h->size; ++i) {
     *  if (h->B[i] != UNINITIALIZED) {
     *      printf("%lu => %p\n", i, (void*)h->B[i]);
     *  }
     * }
     */
}


#ifndef C99
# error +--------------------------------------------------------+
# error | This code requires C99 support (for gcc, use -std=c99) |
# error +--------------------------------------------------------+
#endif

/* vim:set expandtab: */
