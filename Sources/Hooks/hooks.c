#include <stddef.h>
#include "Hooks.h"

typedef struct HeapObject_s   HeapObject;
typedef struct HeapMetadata_s HeapMetadata;

extern HeapObject * (*_swift_allocObject)(HeapMetadata const *, size_t, size_t);
extern HeapObject * (*_swift_retain)(HeapObject *);
extern HeapObject * (*_swift_release)(HeapObject *);

static HeapObject * (*orig_alloc)  (HeapMetadata const *, size_t, size_t);
static HeapObject * (*orig_retain) (HeapObject *);
static HeapObject * (*orig_release)(HeapObject *);

static HeapObject * my_alloc(HeapMetadata const *m, size_t s, size_t a) {
    return orig_alloc(m, s, a);
}

static HeapObject * my_retain(HeapObject *p) {
    return orig_retain(p);
}

static HeapObject * my_release(HeapObject *p) {
    return orig_release(p);
}

void install_hooks(void) {
    orig_alloc   = _swift_allocObject; _swift_allocObject = my_alloc;
    orig_retain  = _swift_retain;      _swift_retain      = my_retain;
    orig_release = _swift_release;     _swift_release     = my_release;
}

void remove_hooks(void) {
    _swift_allocObject = orig_alloc;
    _swift_retain      = orig_retain;
    _swift_release     = orig_release;
}
