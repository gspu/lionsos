// Automatically generated by makemoduledefs.py.

extern const struct _mp_obj_module_t mp_module_io;
#undef MODULE_DEF_IO
#define MODULE_DEF_IO { MP_ROM_QSTR(MP_QSTR_io), MP_ROM_PTR(&mp_module_io) },

extern const struct _mp_obj_module_t mp_module_os;
#undef MODULE_DEF_OS
#define MODULE_DEF_OS { MP_ROM_QSTR(MP_QSTR_os), MP_ROM_PTR(&mp_module_os) },

extern const struct _mp_obj_module_t mp_module_time;
#undef MODULE_DEF_TIME
#define MODULE_DEF_TIME { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_module_time) },

extern const struct _mp_obj_module_t mp_module___main__;
#undef MODULE_DEF___MAIN__
#define MODULE_DEF___MAIN__ { MP_ROM_QSTR(MP_QSTR___main__), MP_ROM_PTR(&mp_module___main__) },

extern const struct _mp_obj_module_t mp_module_builtins;
#undef MODULE_DEF_BUILTINS
#define MODULE_DEF_BUILTINS { MP_ROM_QSTR(MP_QSTR_builtins), MP_ROM_PTR(&mp_module_builtins) },

extern const struct _mp_obj_module_t fb_module;
#undef MODULE_DEF_FB
#define MODULE_DEF_FB { MP_ROM_QSTR(MP_QSTR_fb), MP_ROM_PTR(&fb_module) },

extern const struct _mp_obj_module_t myport_module;
#undef MODULE_DEF_MYPORT
#define MODULE_DEF_MYPORT { MP_ROM_QSTR(MP_QSTR_myport), MP_ROM_PTR(&myport_module) },

extern const struct _mp_obj_module_t mp_module_sys;
#undef MODULE_DEF_SYS
#define MODULE_DEF_SYS { MP_ROM_QSTR(MP_QSTR_sys), MP_ROM_PTR(&mp_module_sys) },


#define MICROPY_REGISTERED_MODULES \
    MODULE_DEF_BUILTINS \
    MODULE_DEF_FB \
    MODULE_DEF_MYPORT \
    MODULE_DEF_SYS \
    MODULE_DEF___MAIN__ \
// MICROPY_REGISTERED_MODULES

#define MICROPY_REGISTERED_EXTENSIBLE_MODULES \
    MODULE_DEF_IO \
    MODULE_DEF_OS \
    MODULE_DEF_TIME \
// MICROPY_REGISTERED_EXTENSIBLE_MODULES

extern void mp_module_sys_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest);
#define MICROPY_MODULE_DELEGATIONS \
    { MP_ROM_PTR(&mp_module_sys), mp_module_sys_attr }, \
// MICROPY_MODULE_DELEGATIONS
