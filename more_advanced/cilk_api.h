int __cilkrts_get_steal_count(int* wid) {
  *wid = 1;
  return 0;
}

int __cilkrts_get_nworkers() {
  return 1;
}

#define REDUCER_VIEW(reducer) (*(reducer))
#define CILK_C_DECLARE_REDUCER(type) type*
#define CILK_C_INIT_REDUCER(type, reduce, ident, destroy, init) ({ \
  void* initial = malloc(sizeof(type)); \
  ident(NULL, initial); \
  (type*) initial; \
})
#define CILK_C_REGISTER_REDUCER(reducer);
#define CILK_C_UNREGISTER_REDUCER(reducer);
