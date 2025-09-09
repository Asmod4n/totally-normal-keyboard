#include <mruby.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <linux/input.h>  // For EVIOCGRAB
#include <unistd.h>       // For usleep
#include <sys/ioctl.h>    // For ioctl()
#include <mruby/presym.h>
#include <stdbool.h>
#include <mruby/array.h>
#include <mruby/string.h>
#include <dlfcn.h>
#include <limits.h>
#include <linux/keyboard.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <libgen.h>   // for dirname()
#include <unistd.h>   // for access(), execv()
#undef P_ALL
#undef P_PID
#undef P_PGID
#undef P_PIDFD
#include <sys/wait.h>

int resolve_tnk_path(mrb_state *mrb,
                     const char *rel_path,
                     int mode,
                     char *out,
                     size_t out_size)
{
    char base_dir[PATH_MAX];
    char joined[PATH_MAX];
    char resolved[PATH_MAX];

    if (!realpath(TNK_PREFIX, base_dir)) {
        mrb_sys_fail(mrb, "realpath(TNK_PREFIX)");
        return -1;
    }

    if (snprintf(joined, sizeof(joined), "%s/%s", base_dir, rel_path) >= (int)sizeof(joined)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "path too long");
        return -1;
    }

    if (!realpath(joined, resolved)) {
        mrb_sys_fail(mrb, "realpath(joined)");
        return -1;
    }

    size_t base_len = strlen(base_dir);
    if (strncmp(resolved, base_dir, base_len) != 0 ||
        (resolved[base_len] != '/' && resolved[base_len] != '\0')) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "path escapes TNK_PREFIX");
        return -1;
    }

    if (access(resolved, mode) != 0) {
        mrb_sys_fail(mrb, "access(resolved)");
        return -1;
    }

    if (strlen(resolved) >= out_size) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "output buffer too small");
        return -1;
    }
    strcpy(out, resolved);
    return 0;
}

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

// Modifier Bits
#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

// Vollständige US-HID Usage IDs für Linux-Scancodes (nicht belegte = 0x00)
static const uint8_t scancode_to_hid[NR_KEYS] = {
  0x00,0x29,0x1E,0x1F,0x20,0x21,0x22,0x23,
  0x24,0x25,0x26,0x27,0x2D,0x2E,0x2A,0x2B,
  0x14,0x1A,0x08,0x15,0x17,0x1C,0x18,0x0C,
  0x12,0x13,0x2F,0x30,0x28,0x04,0x16,0x07,
  0x09,0x0A,0x0B,0x0D,0x0E,0x0F,0x33,0x34,
  0x35,0xE1,0x1D,0x1B,0x06,0x19,0x05,0x11,
  0x10,0x36,0x37,0x38,0xE5,0x55,0xE0,0x2C,
  0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,
  0x41,0x42,0x43,0x53,0x47,0x5F,0x60,0x61,
  0x56,0x5C,0x5D,0x5E,0x57,0x59,0x5A,0x5B,
  0x62,0x63,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// Returns true on success, false on invalid/incomplete UTF-8.
// Consumes exactly one codepoint from (s, len) and sets *cp and *consumed.
static bool utf8_next_cp(const char *s, size_t len, uint32_t *cp, size_t *consumed) {
  if (len == 0) return false;
  const unsigned char c0 = (unsigned char)s[0];

  if (c0 < 0x80) { // 1-byte ASCII
    *cp = c0;
    *consumed = 1;
    return true;
  }

  // Determine expected length and masks; validate continuation bytes and overlongs.
  if ((c0 & 0xE0) == 0xC0) { // 2-byte
    if (len < 2) return false;
    const unsigned char c1 = (unsigned char)s[1];
    if ((c1 & 0xC0) != 0x80) return false;
    uint32_t v = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    if (v < 0x80) return false; // overlong
    *cp = v;
    *consumed = 2;
    return true;
  }

  if ((c0 & 0xF0) == 0xE0) { // 3-byte
    if (len < 3) return false;
    const unsigned char c1 = (unsigned char)s[1];
    const unsigned char c2 = (unsigned char)s[2];
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
    uint32_t v = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    // Overlong and surrogate checks
    if (v < 0x800) return false;
    if (v >= 0xD800 && v <= 0xDFFF) return false;
    *cp = v;
    *consumed = 3;
    return true;
  }

  if ((c0 & 0xF8) == 0xF0) { // 4-byte
    if (len < 4) return false;
    const unsigned char c1 = (unsigned char)s[1];
    const unsigned char c2 = (unsigned char)s[2];
    const unsigned char c3 = (unsigned char)s[3];
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
    uint32_t v = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    // Overlong or out-of-range
    if (v < 0x10000 || v > 0x10FFFF) return false;
    *cp = v;
    *consumed = 4;
    return true;
  }

  return false; // invalid leading byte
}

static mrb_value
gen_keymap(mrb_state *mrb, mrb_value self)
{
    char resolved[PATH_MAX];
    if (resolve_tnk_path(mrb,
                        "share/totally-normal-keyboard/gen_keymap_h.sh",
                        R_OK,
                        resolved,
                        sizeof(resolved)) != 0) {
        return mrb_false_value();
    }

    pid_t pid = fork();
    if (pid < 0) {
        mrb_sys_fail(mrb, "fork");
    } else if (pid == 0) {
        char *argv[] = { resolved, NULL };
        execv(resolved, argv);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        mrb_sys_fail(mrb, "waitpid");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "gen_keymap_h.sh failed (exit code %d)",
                   WEXITSTATUS(status));
    }

    return mrb_true_value();
}


static unsigned short plain_map[NR_KEYS];
static int keymap_loaded = 0;

static int parse_plain_map_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }

    char buf[1024];
    int found = 0;
    int idx = 0;

    while (fgets(buf, sizeof(buf), fp)) {
        if (!found) {
            if (strstr(buf, "unsigned short plain_map")) {
                found = 1;
            }
            continue;
        }

        if (strchr(buf, '}')) {
            break; // end of array
        }

        char *p = buf;
        while (*p) {
            // skip whitespace and commas
            while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\n') p++;
            if (*p == '\0') break;

            // parse number (hex or decimal)
            char *end;
            unsigned long val = strtoul(p, &end, 0);
            if (p != end && idx < NR_KEYS) {
                plain_map[idx++] = (unsigned short)val;
            }
            p = end;
        }
    }

    fclose(fp);

    if (idx != NR_KEYS) {
        fprintf(stderr, "Warning: expected %d keys, got %d\n", NR_KEYS, idx);
    }

    return 0;
}

static int find_scancode_for_char(unsigned short ch) {
    unsigned char target = (unsigned char)ch;
    for (int i = 0; i < NR_KEYS; i++) {
        if ((plain_map[i] & 0xFF) == target) {
            return i;
        }
    }
    return -1;
}

static mrb_value
load_keymap(mrb_state *mrb, mrb_value self)
{
    if (keymap_loaded) {
        return mrb_true_value();
    }

    char resolved[PATH_MAX];

    if (resolve_tnk_path(mrb,
                        "share/totally-normal-keyboard/keymap.h",
                        R_OK,
                        resolved,
                        sizeof(resolved)) != 0) {
        return mrb_false_value();
    }

    if (parse_plain_map_file(resolved) != 0) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "failed to parse %S", mrb_str_new_cstr(mrb, resolved));
    }

    keymap_loaded = 1;
    return mrb_true_value();
}

static mrb_value mrb_generate_hid_report(mrb_state *mrb, mrb_value self) {
  mrb_value *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);

  uint8_t report[8] = {0}; // [0]=modifiers, [1]=reserved, [2..7]=keys
  uint8_t modifier = 0;

  int arg_index = 0;

  for (; arg_index < argc; arg_index++) {
    if (!mrb_symbol_p(argv[arg_index])) break;

    mrb_sym sym = mrb_symbol(argv[arg_index]);
    switch (sym) {
      case MRB_SYM(lctrl):  modifier |= MOD_LCTRL;  break;
      case MRB_SYM(lshift): modifier |= MOD_LSHIFT; break;
      case MRB_SYM(lalt):   modifier |= MOD_LALT;   break;
      case MRB_SYM(lgui):   modifier |= MOD_LGUI;   break;
      case MRB_SYM(rctrl):  modifier |= MOD_RCTRL;  break;
      case MRB_SYM(rshift): modifier |= MOD_RSHIFT; break;
      case MRB_SYM(ralt):   modifier |= MOD_RALT;   break;
      case MRB_SYM(rgui):   modifier |= MOD_RGUI;   break;
      default: /* ignore unknown symbols */ break;
    }
  }

  // Fill up to 6 key slots
  int key_slot = 0;
  for (; arg_index < argc && key_slot < 6; arg_index++) {
    if (!mrb_string_p(argv[arg_index])) {
      continue;
    }

    const char *buf = RSTRING_PTR(argv[arg_index]);
    size_t len = (size_t)RSTRING_LEN(argv[arg_index]);

    if (len == 0) continue;

    uint32_t cp;
    size_t consumed;
    if (!utf8_next_cp(buf, len, &cp, &consumed)) {
      mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid UTF-8 sequence");
    }

    int sc = find_scancode_for_char((unsigned short)cp);
    if (sc >= 0) {
      uint8_t hid = scancode_to_hid[sc];
      report[2 + key_slot] = hid;
      key_slot++;
    }
  }


  report[0] = modifier;
  return mrb_str_new(mrb, (const char *)report, sizeof(report));
}

void
mrb_totally_normal_keyboard_gem_init(mrb_state *mrb)
{
#ifdef MRB_DEBUG
    // Set $DEBUG = true
    mrb_gv_set(mrb,
               mrb_intern_lit(mrb, "$DEBUG"),
               mrb_true_value());
#endif
    struct RClass *tnk = mrb_define_class_id(mrb, MRB_SYM(Tnk), mrb->object_class);
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(grab), grab, MRB_ARGS_REQ(1));
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(ungrab), ungrab, MRB_ARGS_REQ(1));
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(gen_keymap), gen_keymap, MRB_ARGS_NONE());
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(load_keymap), load_keymap, MRB_ARGS_NONE());
    mrb_define_const_id(mrb, tnk, MRB_SYM(PREFIX), mrb_str_new_lit(mrb, TNK_PREFIX));
    struct RClass *hotkeys = mrb_define_module_under_id(mrb, tnk, MRB_SYM(Hotkeys));
    mrb_define_module_function_id(mrb, hotkeys, MRB_SYM(build_report), mrb_generate_hid_report, MRB_ARGS_ANY());
}

void mrb_totally_normal_keyboard_gem_final(mrb_state* mrb)
{
    mrb_value tnk = mrb_obj_value(mrb_class_get_id(mrb, MRB_SYM(Tnk)));
    mrb_value instance = mrb_cv_get(mrb, tnk, MRB_IVSYM(instance));
    if (!mrb_nil_p(instance))
        mrb_funcall_id(mrb, tnk, MRB_SYM(close), 0);
}
