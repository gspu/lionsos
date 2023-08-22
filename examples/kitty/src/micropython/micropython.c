#include <sel4cp.h>
#include "micropython.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_HEAP_SIZE];
cothread_t t_event, t_mp;

int active_events = mp_event_source_none;
int mp_blocking_events = mp_event_source_none;

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    // @ivanv: improve/fix, use printf?
    sel4cp_dbg_puts("MICROPYTHON|ERROR: Assertion failed!\n");
    while (true) {}
}
#endif

void t_mp_entrypoint(void) {
    sel4cp_dbg_puts("MICROPYTHON|INFO: initialising!\n");

    // Initialise the MicroPython runtime.
    mp_stack_ctrl_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    // Start a normal REPL; will exit when ctrl-D is entered on a blank line.
    pyexec_friendly_repl();

    // Deinitialise the runtime.
    gc_sweep_all();
    mp_deinit();

    sel4cp_dbg_puts("MICROPYTHON|INFO: exited!\n");
    co_switch(t_event);
}

void init(void) {
    t_event = co_active();
    t_mp = co_derive((void *)mp_stack, MICROPY_HEAP_SIZE, t_mp_entrypoint);
    co_switch(t_mp);
}

void notified(sel4cp_channel ch) {
    switch (ch) {
    case TIMER_CH:
        active_events |= mp_event_source_timer;
        break;
    }
    if (active_events & mp_blocking_events) {
        co_switch(t_mp);
    }
}

// Handle uncaught exceptions (should never be reached in a correct C implementation).
void nlr_jump_fail(void *val) {
    for (;;) {
    }
}

// Do a garbage collection cycle.
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

// There is no filesystem so stat'ing returns nothing.
mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}

// There is no filesystem so opening a file raises an exception.
mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    mp_raise_OSError(MP_ENOENT);
}
