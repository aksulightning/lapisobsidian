#ifndef H_PLATE_CONTEXTS
#define H_PLATE_CONTEXTS
#include <stddef.h>
/* Caller allocates zeroed, suitably aligned memory; NULL selects legacy state. */
size_t signs_context_size (void);
void signs_context_select (void *memory);
size_t doors_context_size (void);
void doors_context_select (void *memory);
size_t circuits_context_size (void);
void circuits_context_select (void *memory);
size_t farming_context_size (void);
void farming_context_select (void *memory);
size_t fluids_context_size (void);
void fluids_context_select (void *memory);
size_t items_context_size (void);
void items_context_select (void *memory);
size_t musicbox_context_size (void);
void musicbox_context_select (void *memory);
size_t worldgen_context_size (void);
void worldgen_context_select (void *memory);
size_t mobs_context_size (void);
void mobs_context_select (void *memory);
#endif
