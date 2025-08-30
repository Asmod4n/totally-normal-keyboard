#include <mruby.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <linux/input.h>      // für NR_KEYS
#include "keymap.h"           // unsigned int keymap_count; unsigned short *key_maps[];
#include "lookup_key_name.h"  // const char *lookup_key_name(int code);
#include <stdlib.h>
#include <mruby/error.h>
#include <linux/input.h>  // For EVIOCGRAB
#include <unistd.h>       // For usleep
#include <sys/ioctl.h>    // For ioctl()

static mrb_value grab(mrb_state *mrb, mrb_value self)
{
    mrb_int fd;
    mrb_get_args(mrb, "i", &fd);
    ioctl(fd, EVIOCGRAB, 0);
    usleep(5000);
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        mrb_sys_fail(mrb, "ioctl(EVIOCGRAB, 1)");
    }

    return self;
}

static mrb_value ungrab(mrb_state *mrb, mrb_value self)
{
    mrb_int fd;
    mrb_get_args(mrb, "i", &fd);
    if (ioctl(fd, EVIOCGRAB, 0) < 0) {
        mrb_sys_fail(mrb, "ioctl(EVIOCGRAB, 0)");
    }

    return self;
}

static mrb_value
decode_keysym(mrb_state *mrb, unsigned short sym, int code)
{
    if (sym == 0 || sym == 0xF200) {
        return mrb_nil_value();
    }

    unsigned char hi = sym >> 8;
    unsigned char lo = sym & 0xFF;

    if (hi == 0xF3 || hi == 0xF5 || hi == 0xF6 || hi == 0xF7 || hi == 0xF8) {
        const char *name = lookup_key_name(code);
        if (name) {
            return mrb_symbol_value(mrb_intern_cstr(mrb, name));
        }
        return mrb_nil_value();
    }

    if (lo < 0x20 || lo == 0x7F) {
        const char *name = lookup_key_name(code);
        if (name) {
            return mrb_symbol_value(mrb_intern_cstr(mrb, name));
        }
        return mrb_nil_value();
    }

    char buf[2];
    size_t len;
    if (lo < 0x80) {
        buf[0] = lo;
        len = 1;
    } else {
        buf[0] = 0xC0 | (lo >> 6);
        buf[1] = 0x80 | (lo & 0x3F);
        len = 2;
    }

    if (len == 1 && lo >= '0' && lo <= '9') {
        return mrb_int_value(mrb, lo - '0');
    }

    return mrb_str_new(mrb, buf, len);
}

void
mrb_totally_normal_keyboard_gem_init(mrb_state *mrb)
{
    struct RClass *tnk = mrb_define_class(mrb, "Tnk", mrb->object_class);
    mrb_define_module_function(mrb, tnk, "grab", grab, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, tnk, "ungrab", ungrab, MRB_ARGS_REQ(1));

    mrb_value keycode = mrb_hash_new(mrb);
    mrb_value keysym  = mrb_hash_new(mrb);
    mrb_value keymaps = mrb_ary_new_capa(mrb, keymap_count);

    for (size_t layer = 0; layer < keymap_count; layer++) {
        unsigned short *map = key_maps[layer];
        mrb_value ary = mrb_ary_new_capa(mrb, NR_KEYS);

        for (int code = 0; code < NR_KEYS; code++) {
            unsigned short sym = map ? map[code] : 0;
            mrb_value val = decode_keysym(mrb, sym, code);

            mrb_ary_set(mrb, ary, code, val);

            if (layer == 0 && !mrb_nil_p(val)) {
                mrb_hash_set(mrb, keycode, mrb_fixnum_value(code), val);
                mrb_hash_set(mrb, keysym,  val, mrb_fixnum_value(code));
            }
        }

        mrb_ary_push(mrb, keymaps, ary);
    }

    mrb_define_const(mrb, tnk, "KeyCode", keycode);
    mrb_define_const(mrb, tnk, "KeySym",  keysym);
    mrb_define_const(mrb, tnk, "KeyMaps", keymaps);
}

void mrb_totally_normal_keyboard_gem_final(mrb_state* mrb) {}
