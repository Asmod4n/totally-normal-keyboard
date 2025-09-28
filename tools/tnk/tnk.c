#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <errno.h>
#include <ftw.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/keyboard.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/msgpack.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#ifdef MRB_NO_PRESYM
#error "tnk cannot be build without presym"
#endif

static mrb_value
resolve_tnk_path(mrb_state *mrb, const char *rel_path, int mode)
{
  char *tmp = NULL;

  mrb_value buf_str = mrb_str_new_capa(mrb, PATH_MAX);

  ssize_t len = readlink("/proc/self/exe", RSTRING_PTR(buf_str), PATH_MAX);
  if (len < 0) {
    mrb_sys_fail(mrb, "readlink(/proc/self/exe)");
  }
  buf_str = mrb_str_resize(mrb, buf_str, len);

  dirname(RSTRING_PTR(buf_str));
  buf_str = mrb_str_resize(mrb, buf_str, strlen(RSTRING_PTR(buf_str)));

  if (!(tmp = realpath(RSTRING_CSTR(mrb, buf_str), NULL))) {
    mrb_sys_fail(mrb, "realpath(base_dir)");
  }
  buf_str = mrb_str_resize(mrb, buf_str, 0);
  buf_str = mrb_str_cat_cstr(mrb, buf_str, tmp);
  free(tmp);
  tmp = NULL;

  buf_str = mrb_str_cat_lit(mrb, buf_str, "/");
  buf_str = mrb_str_cat_cstr(mrb, buf_str, rel_path);

  if (!(tmp = realpath(RSTRING_CSTR(mrb, buf_str), NULL))) {
    mrb_sys_fail(mrb, "realpath(joined)");
  }
  buf_str = mrb_str_resize(mrb, buf_str, 0);
  buf_str = mrb_str_cat_cstr(mrb, buf_str, tmp);
  free(tmp);

  if (access(RSTRING_CSTR(mrb, buf_str), mode) != 0) {
    mrb_sys_fail(mrb, "access(resolved)");
  }

  return buf_str;
}

static uid_t target_uid;
static gid_t target_gid;

static int
chown_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  if (lchown(fpath, target_uid, target_gid) != 0) {
    return -1;
  }
  return 0;
}

static void
drop_privileges(mrb_state *mrb, const char *username)
{
  mrb_value target_dir = resolve_tnk_path(mrb, "../share/totally-normal-keyboard", F_OK);

  struct passwd pwd;
  struct passwd *result = NULL;
  long bufsize;
  int ret;

  bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize == -1) {
    bufsize = 16384;
  }

  mrb_value buf = mrb_str_new_capa(mrb, bufsize);

  while ((ret = getpwnam_r(username, &pwd, RSTRING_PTR(buf), RSTRING_CAPA(buf), &result)) == ERANGE) {
    buf = mrb_str_resize(mrb, buf, RSTRING_CAPA(buf) * 2);
  }
  if (result == NULL) {
    if (ret == 0) {
      mrb_raisef(mrb, E_RUNTIME_ERROR, "user '%S' not found", mrb_str_new_cstr(mrb, username));
    } else {
      errno = ret;
      mrb_sys_fail(mrb, "getpwnam_r");
    }
  }

  target_uid = pwd.pw_uid;
  target_gid = pwd.pw_gid;
  if (nftw(RSTRING_CSTR(mrb, target_dir), chown_cb, 16, FTW_PHYS) != 0) {
    mrb_sys_fail(mrb, "nftw");
  }

  if (initgroups(pwd.pw_name, pwd.pw_gid) != 0) {
    mrb_sys_fail(mrb, "initgroups");
  }
  if (setgid(pwd.pw_gid) != 0) {
    mrb_sys_fail(mrb, "setgid");
  }
  if (setuid(pwd.pw_uid) != 0) {
    mrb_sys_fail(mrb, "setuid");
  }
}

static unsigned short plain_map[NR_KEYS];

static bool
parse_plain_map_stream(FILE *fp)
{
  char buf[4096];
  int found = 0;
  int idx = 0;

  while (fgets(buf, sizeof(buf), fp)) {
    if (!found) {
      if (strstr(buf, "unsigned short plain_map[NR_KEYS] = {")) {
        found = 1;
      }
      continue;
    }

    if (strchr(buf, '}')) {
      break;
    }

    char *p = buf;
    while (*p) {
      while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\n')
        p++;
      if (*p == '\0')
        break;

      char *end;
      unsigned long val = strtoul(p, &end, 0);
      if (p != end && idx < NR_KEYS) {
        plain_map[idx++] = (unsigned short)val;
      }
      p = end;
    }
  }

  return (idx == NR_KEYS);
}

static int char_to_scancode[NR_KEYS];

static void rebuild_char_lookup(void) {
    // Default to "not found"
    memset(char_to_scancode, 0xFF, sizeof(char_to_scancode));

    for (int i = 0; i < NR_KEYS; i++) {
        unsigned char ch = plain_map[i] & 0xFF;
        char_to_scancode[ch] = i;
    }
}

static mrb_value
gen_keymap(mrb_state *mrb, mrb_value self)
{
  /* Defaults: US PC105 keyboard */
  mrb_value xkbmodel = mrb_str_new_lit(mrb, "pc105");
  mrb_value xkblayout = mrb_str_new_lit(mrb, "us");
  mrb_value xkbvariant = mrb_str_new_lit(mrb, "");
  mrb_value xkboptions = mrb_str_new_lit(mrb, "");

  FILE *kf = fopen("/etc/default/keyboard", "r");
  if (kf) {
    char line[512];
    while (fgets(line, sizeof(line), kf)) {
      char *p = line;
      while (*p == ' ' || *p == '\t')
        p++;
      if (*p == '#' || *p == '\0' || *p == '\n') continue;

      char *eq = strchr(p, '=');
      if (!eq) continue;
      *eq = '\0';
      char *name = p;
      char *val = eq + 1;

      size_t len = strlen(val);
      while (len && (val[len - 1] == '\n' || val[len - 1] == '\r'))
        val[--len] = '\0';

      if (len >= 2 && ((val[0] == '"' && val[len - 1] == '"') ||
                       (val[0] == '\'' && val[len - 1] == '\''))) {
        val[len - 1] = '\0';
        val++;
        len -= 2;
      }

      mrb_value *target = NULL;
      if (strcmp(name, "XKBMODEL") == 0)        target = &xkbmodel;
      else if (strcmp(name, "XKBLAYOUT") == 0)  target = &xkblayout;
      else if (strcmp(name, "XKBVARIANT") == 0) target = &xkbvariant;
      else if (strcmp(name, "XKBOPTIONS") == 0) target = &xkboptions;

      if (target) {
        *target = mrb_str_resize(mrb, *target, len);
        memcpy(RSTRING_PTR(*target), val, len);
      }
    }
    fclose(kf);
  }

  /* Pipe: ckbcomp -> loadkeys */
  int pipefd[2];
  if (pipe(pipefd) == -1) mrb_sys_fail(mrb, "pipe(pipefd)");

  pid_t pid = fork();
  if (pid == -1) mrb_sys_fail(mrb, "fork");

  if (pid == 0) {
    int mid[2];
    if (pipe(mid) == -1) _Exit(127);

    pid_t pid_ckb = fork();
    if (pid_ckb == -1) _Exit(127);

    if (pid_ckb == 0) {
      close(mid[0]);
      dup2(mid[1], STDOUT_FILENO);
      close(mid[1]);

      const char *model_arg = RSTRING_CSTR(mrb, xkbmodel);
      const char *layout_arg = RSTRING_CSTR(mrb, xkblayout);
      const char *variant_arg = RSTRING_CSTR(mrb, xkbvariant);
      mrb_str_modify(mrb, RSTRING(xkboptions));
      const char *options_arg = RSTRING_CSTR(mrb, xkboptions);

      const char *optv[16];
      int optc = 0;
      if (options_arg[0] != '\0') {
        const char *p = options_arg;
        while (p && *p && optc < (int)(sizeof(optv) / sizeof(optv[0]) - 1)) {
          optv[optc++] = p;
          char *comma = strchr(p, ',');
          if (!comma) break;
          *comma = '\0';
          p = comma + 1;
          while (*p == ' ' || *p == '\t')
            p++;
        }
      }
      optv[optc] = NULL;

      const char *argv[64];
      int argc = 0;
#define ADD_ARG(s)                                                             \
  do {                                                                         \
    if (argc < (int)(sizeof(argv) / sizeof(argv[0]) - 1))                      \
      argv[argc++] = (s);                                                      \
  } while (0)

      ADD_ARG("ckbcomp");
      ADD_ARG("-compact");
      if (model_arg[0]) {
        ADD_ARG("-model");
        ADD_ARG(model_arg);
      }
      if (layout_arg[0]) {
        ADD_ARG("-layout");
        ADD_ARG(layout_arg);
      }
      if (variant_arg[0]) {
        ADD_ARG("-variant");
        ADD_ARG(variant_arg);
      }
      for (int i = 0; i < optc; i++) {
        ADD_ARG("-option");
        ADD_ARG(optv[i]);
      }
      argv[argc] = NULL;

      execv("/usr/bin/ckbcomp", (char *const *)argv);
      _exit(127);
#undef ADD_ARG
    }

    close(mid[1]);
    dup2(mid[0], STDIN_FILENO);
    close(mid[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    execl("/usr/bin/loadkeys", "loadkeys", "-u", "--mktable", (char *)NULL);
    _exit(127);
  }

  /* parent: capture output */
  close(pipefd[1]);
  FILE *fp = fdopen(pipefd[0], "r");
  if (!fp) mrb_sys_fail(mrb, "fdopen(pipefd[0], r)");

  bool parse_plain_success = parse_plain_map_stream(fp);
  fclose(fp);
  if (!parse_plain_success) { mrb_raise(mrb, E_RUNTIME_ERROR, "invalid keymap"); }

  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "pipeline failed (exit code %d)",
               WEXITSTATUS(status));
  }

  rebuild_char_lookup();
  return mrb_true_value();
}

static bool
utf8_next_cp(const char *s, size_t len, uint32_t *cp)
{
  if (len == 0) return false;
  const unsigned char c0 = (unsigned char)s[0];

  if (c0 < 0x80) { // 1-byte ASCII
    *cp = c0;
    return true;
  }

  if ((c0 & 0xE0) == 0xC0) { // 2-byte
    if (len < 2) return false;
    const unsigned char c1 = (unsigned char)s[1];
    if ((c1 & 0xC0) != 0x80) return false;
    uint32_t v = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    if (v < 0x80) return false; // overlong
    *cp = v;
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
    return true;
  }

  if ((c0 & 0xF8) == 0xF0) { // 4-byte
    if (len < 4) return false;
    const unsigned char c1 = (unsigned char)s[1];
    const unsigned char c2 = (unsigned char)s[2];
    const unsigned char c3 = (unsigned char)s[3];
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
    uint32_t v = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) |
                 ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    // Overlong or out-of-range
    if (v < 0x10000 || v > 0x10FFFF) return false;
    *cp = v;
    return true;
  }

  return false; // invalid leading byte
}

static const uint8_t scancode_to_hid[NR_KEYS] = {
    [1] = 0x29,  [2] = 0x1E,  [3] = 0x1F,  [4] = 0x20,  [5] = 0x21,
    [6] = 0x22,  [7] = 0x23,  [8] = 0x24,  [9] = 0x25,  [10] = 0x26,
    [11] = 0x27, [12] = 0x2D, [13] = 0x2E, [14] = 0x2A, [15] = 0x2B,
    [16] = 0x14, [17] = 0x1A, [18] = 0x08, [19] = 0x15, [20] = 0x17,
    [21] = 0x1C, [22] = 0x18, [23] = 0x0C, [24] = 0x12, [25] = 0x13,
    [26] = 0x2F, [27] = 0x30, [28] = 0x28, [29] = 0x04, [30] = 0x16,
    [31] = 0x07, [32] = 0x09, [33] = 0x0A, [34] = 0x0B, [35] = 0x0D,
    [36] = 0x0E, [37] = 0x0F, [38] = 0x33, [39] = 0x34, [40] = 0x35,
    [41] = 0xE1, [42] = 0x1D, [43] = 0x1B, [44] = 0x06, [45] = 0x19,
    [46] = 0x05, [47] = 0x11, [48] = 0x10, [49] = 0x36, [50] = 0x37,
    [51] = 0x38, [52] = 0xE5, [53] = 0x55, [54] = 0xE0, [55] = 0x2C,
    [56] = 0x39, [57] = 0x3A, [58] = 0x3B, [59] = 0x3C, [60] = 0x3D,
    [61] = 0x3E, [62] = 0x3F, [63] = 0x40, [64] = 0x41, [65] = 0x42,
    [66] = 0x43, [67] = 0x53, [68] = 0x47, [69] = 0x5F, [70] = 0x60,
    [71] = 0x61, [72] = 0x56, [73] = 0x5C, [74] = 0x5D, [75] = 0x5E,
    [76] = 0x57, [77] = 0x59, [78] = 0x5A, [79] = 0x5B, [80] = 0x62,
    [81] = 0x63};

static mrb_value
mrb_generate_hid_report(mrb_state *mrb, mrb_value self)
{
  mrb_value *argv;
  mrb_int argc;
  mrb_get_args(mrb, "*", &argv, &argc);

  uint8_t report[8] = {0};
  uint8_t modifier = 0;
  int key_slot = 0;

  for (int i = 0; i < argc && key_slot < 6; i++) {
    if (mrb_symbol_p(argv[i])) {
      switch (mrb_symbol(argv[i])) {
        /* Modifiers */
        case MRB_SYM(lctrl):  modifier |= 0x01; break;
        case MRB_SYM(lshift): modifier |= 0x02; break;
        case MRB_SYM(lalt):   modifier |= 0x04; break;
        case MRB_SYM(lgui):   modifier |= 0x08; break;
        case MRB_SYM(rctrl):  modifier |= 0x10; break;
        case MRB_SYM(rshift): modifier |= 0x20; break;
        case MRB_SYM(ralt):   modifier |= 0x40; break;
        case MRB_SYM(rgui):   modifier |= 0x80; break;

        /* Non‑printing keys */
        case MRB_SYM(enter):      report[2 + key_slot++] = 0x28; break;
        case MRB_SYM(esc):        report[2 + key_slot++] = 0x29; break;
        case MRB_SYM(backspace):  report[2 + key_slot++] = 0x2A; break;
        case MRB_SYM(tab):        report[2 + key_slot++] = 0x2B; break;
        case MRB_SYM(capslock):   report[2 + key_slot++] = 0x39; break;

        /* Function keys */
        case MRB_SYM(f1):  report[2 + key_slot++] = 0x3A; break;
        case MRB_SYM(f2):  report[2 + key_slot++] = 0x3B; break;
        case MRB_SYM(f3):  report[2 + key_slot++] = 0x3C; break;
        case MRB_SYM(f4):  report[2 + key_slot++] = 0x3D; break;
        case MRB_SYM(f5):  report[2 + key_slot++] = 0x3E; break;
        case MRB_SYM(f6):  report[2 + key_slot++] = 0x3F; break;
        case MRB_SYM(f7):  report[2 + key_slot++] = 0x40; break;
        case MRB_SYM(f8):  report[2 + key_slot++] = 0x41; break;
        case MRB_SYM(f9):  report[2 + key_slot++] = 0x42; break;
        case MRB_SYM(f10): report[2 + key_slot++] = 0x43; break;
        case MRB_SYM(f11): report[2 + key_slot++] = 0x44; break;
        case MRB_SYM(f12): report[2 + key_slot++] = 0x45; break;

        /* System keys */
        case MRB_SYM(printscreen): report[2 + key_slot++] = 0x46; break;
        case MRB_SYM(scrolllock):  report[2 + key_slot++] = 0x47; break;
        case MRB_SYM(pause):       report[2 + key_slot++] = 0x48; break;

        /* Navigation */
        case MRB_SYM(insert):   report[2 + key_slot++] = 0x49; break;
        case MRB_SYM(home):     report[2 + key_slot++] = 0x4A; break;
        case MRB_SYM(pageup):   report[2 + key_slot++] = 0x4B; break;
        case MRB_SYM(delete):   report[2 + key_slot++] = 0x4C; break;
        case MRB_SYM(end):      report[2 + key_slot++] = 0x4D; break;
        case MRB_SYM(pagedown): report[2 + key_slot++] = 0x4E; break;
        case MRB_SYM(right):    report[2 + key_slot++] = 0x4F; break;
        case MRB_SYM(left):     report[2 + key_slot++] = 0x50; break;
        case MRB_SYM(down):     report[2 + key_slot++] = 0x51; break;
        case MRB_SYM(up):       report[2 + key_slot++] = 0x52; break;

        /* Keypad */
        case MRB_SYM(kp_numlock): report[2 + key_slot++] = 0x53; break;
        case MRB_SYM(kp_enter):   report[2 + key_slot++] = 0x58; break;

        default:
          mrb_raisef(mrb, E_ARGUMENT_ERROR,
                     "unknown key symbol: %S", argv[i]);
      }
    } else if (mrb_string_p(argv[i])) {
      /* Printable string handling */
      const char *s = RSTRING_PTR(argv[i]);
      size_t len = (size_t)RSTRING_LEN(argv[i]);
      if (!len)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "empty string key");

      uint32_t cp;
      if (!utf8_next_cp(s, len, &cp))
        mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid UTF-8 sequence");

      int sc = char_to_scancode[(unsigned char)cp];
      if (sc < 0)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "character not in keymap");

      uint8_t hid = scancode_to_hid[sc];
      if (hid == 0x00)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "character has no HID mapping");

      report[2 + key_slot++] = hid;
    } else {
      mrb_raisef(mrb, E_ARGUMENT_ERROR,
                 "unsupported key arg type: %S", argv[i]);
    }
  }

  report[0] = modifier;
  return mrb_str_new(mrb, (const char *)report, sizeof(report));
}


static bool
mrb_totally_normal_keyboard_user_init(mrb_state *user_mrb)
{
#ifdef MRB_DEBUG
  mrb_gv_set(user_mrb, MRB_GVSYM(DEBUG), mrb_true_value());
#endif
  struct RClass *tnk = mrb_define_class_id(user_mrb, MRB_SYM_2(user_mrb, Tnk), user_mrb->object_class);
  struct RClass *hotkeys = mrb_define_module_under_id(user_mrb, tnk, MRB_SYM_2(user_mrb, Hotkeys));
  mrb_define_module_function_id(user_mrb, hotkeys, MRB_SYM_2(user_mbr, generate_hid_report),
                                mrb_generate_hid_report, MRB_ARGS_ANY());
  return !user_mrb->exc;
}

static bool
mrb_tnk_load_hotkeys(mrb_state *user_mrb)
{
  static const char hotkeys_rb[] = "class Tnk\n"
                                   "  module Hotkeys\n"
                                   "    @@hotkeys = {}\n"
                                   "    def self.on(*args, &blk)\n"
                                   "      raise \"no block given\" unless blk\n"
                                   "      report = generate_hid_report(*args)\n"
                                   "      @@hotkeys[report] = blk\n"
                                   "    end\n"
                                   "  end\n"
                                   "end\n";
  mrb_load_nstring(user_mrb, hotkeys_rb, sizeof(hotkeys_rb) - 1);
  return !user_mrb->exc;
}

static mrb_state *
mrb_tnk_user_mrb_init(mrb_state *mrb)
{
  mrb_state *user_mrb = mrb_open_core();
  if (!user_mrb) {
    perror("mrb_open_core()");
    return NULL;
  }
  if (!mrb_totally_normal_keyboard_user_init(user_mrb)) {
    mrb_close(user_mrb);
    return NULL;
  }
  if (!mrb_tnk_load_hotkeys(user_mrb)) {
    mrb_close(user_mrb);
    return NULL;
  }
  mrb->ud = user_mrb;
  return user_mrb;
}

static mrb_value tnk_yield_block_protected(mrb_state *vm, void *block) {
  return mrb_yield_argv(vm, *(mrb_value *)block, 0, NULL);
}

static mrb_value
tnk_handle_hid_report_bridge(mrb_state *mrb, mrb_value self)
{
  mrb_value buf;
  mrb_get_args(mrb, "S", &buf);

  mrb_state *user_mrb = (mrb_state *)mrb->ud;

  struct RClass *tnk_h      = mrb_class_get_id(user_mrb, MRB_SYM_2(user_mrb, Tnk));
  struct RClass *hotkeys_h  = mrb_module_get_under_id(user_mrb, tnk_h, MRB_SYM_2(user_mrb, Hotkeys));
  mrb_value hotkeys_hash    = mrb_cv_get(user_mrb, mrb_obj_value(hotkeys_h), MRB_CVSYM_2(user_mrb, hotkeys));
  if (!mrb_hash_p(hotkeys_hash)) {
    mrb_raise(mrb, E_TYPE_ERROR, "not a hash");
  }

  mrb_value key_h = mrb_str_new(user_mrb, RSTRING_PTR(buf), RSTRING_LEN(buf));
  mrb_value blk   = mrb_hash_get(user_mrb, hotkeys_hash, key_h);
  if (mrb_type(blk) != MRB_TT_PROC) {
    mrb_gc_arena_restore(user_mrb, 0);
    return mrb_nil_value();
  }

  mrb_bool err  = FALSE;
  mrb_value ret = mrb_protect_error(user_mrb, tnk_yield_block_protected, &blk, &err);
  if (user_mrb->exc || err) {
    mrb_print_error(user_mrb);
    mrb_clear_error(user_mrb);
    mrb_gc_arena_restore(user_mrb, 0);
    mrb_raise(mrb, E_RUNTIME_ERROR, "user mode vm error");
    return mrb_false_value();
  }

  ret = mrb_msgpack_unpack(mrb, mrb_msgpack_pack(user_mrb, ret));
  mrb_gc_arena_restore(user_mrb, 0);
  return ret;
}

static void
block_signals(sigset_t *mask)
{
  sigemptyset(mask);
  sigaddset(mask, SIGINT);
  sigaddset(mask, SIGTERM);
  sigaddset(mask, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, mask, NULL) == -1) {
    perror("sigprocmask");
    exit(1);
  }
}

int main(int argc, char *argv[])
{
  sigset_t mask;
  block_signals(&mask);

  errno = 0;
  int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
  if (sfd == -1) {
    perror("signalfd");
    return 1;
  }

  mrb_state *mrb = mrb_open();
  if (!mrb) {
    close(sfd);
    perror("mrb_open()");
    return 1;
  }
  mrb_value argv_ary = mrb_ary_new_capa(mrb, argc);
  for (int i = 0; i < argc; i++) {
    mrb_value arg_str = mrb_str_new_static(mrb, argv[i], strlen(argv[i]));
    mrb_obj_freeze(mrb, arg_str);
    mrb_ary_push(mrb, argv_ary, arg_str);
  }
  mrb_obj_freeze(mrb, argv_ary);
  mrb_define_const_id(mrb, mrb->object_class, MRB_SYM(ARGV), argv_ary);

  struct RClass *tnk_cls = mrb_class_get_id(mrb, MRB_SYM(Tnk));
  mrb_value tnk = mrb_obj_value(tnk_cls);
  mrb_funcall_id(mrb, tnk, MRB_SYM(setup_root), 0);
  if (mrb->exc) {
    mrb_print_error(mrb);
    mrb_clear_error(mrb);
    close(sfd);
    mrb_close(mrb);
    return 1;
  }
  mrb_gc_arena_restore(mrb, 0);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(sfd);
    mrb_close(mrb);
    mrb = NULL;
    return 1;
  }

  if (pid == 0) {
    int rc = 0;
    mrb_state *user_mrb = NULL;
    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    const char *drop_user = getenv("TNK_DROP_USER");
    if (!drop_user) drop_user = "nobody";
    drop_privileges(mrb, drop_user);
    if (mrb->exc) {
      rc = 1;
      goto child_cleanup;
    }
    mrb_gv_set(mrb, MRB_GVSYM(USER_MRB), mrb_true_value());
    struct RClass *hotkeys = mrb_define_module_under_id(mrb, tnk_cls, MRB_SYM(Hotkeys));
    mrb_define_module_function_id(mrb, hotkeys, MRB_SYM(handle_hid_report),
                                  tnk_handle_hid_report_bridge,
                                  MRB_ARGS_REQ(1));
    mrb_define_module_function_id(mrb, tnk_cls, MRB_SYM(gen_keymap), gen_keymap,
                                  MRB_ARGS_NONE());
    mrb_funcall_id(mrb, tnk, MRB_SYM(setup_user), 0);
    if (mrb->exc) {
      rc = 1;
      goto child_cleanup;
    }
    user_mrb = mrb_tnk_user_mrb_init(mrb);
    if (user_mrb == NULL) {
      rc = 1;
      goto child_cleanup;
    }

    mrb_value path = resolve_tnk_path(mrb, "../share/totally-normal-keyboard/user.rb", R_OK);
    if (mrb->exc) {
      rc = 1;
      goto child_cleanup;
    }

    FILE *fp = fopen(RSTRING_CSTR(mrb, path), "r");
    if (!fp) {
      perror("user.rb");
      rc = 1;
      goto child_cleanup;
    }
    mrb_load_file(user_mrb, fp);
    fclose(fp);
    if (user_mrb->exc) {
      rc = 1;
      goto child_cleanup;
    }
    mrb_gc_arena_restore(user_mrb, 0);
    mrb_gc_arena_restore(mrb, 0);
    mrb_funcall_id(mrb, tnk, MRB_SYM(run), 0);

  child_cleanup:
    if (mrb->exc && errno != EINTR) {
      rc = 1;
      mrb_print_error(mrb);
    }
    mrb_clear_error(mrb);
    mrb_close(mrb);
    mrb = NULL;
    if (user_mrb) {
      if (user_mrb->exc) {
        mrb_print_error(user_mrb);
        mrb_clear_error(user_mrb);
      }
      mrb_close(user_mrb);
      user_mrb = NULL;
    }
    _Exit(rc);
  }

  int child_exited = 0;
  int exit_code = 0;

  for (;;) {
    struct signalfd_siginfo si;
    ssize_t res = read(sfd, &si, sizeof(si));
    if (res != sizeof(si)) {
      if (errno == EINTR) continue;
      perror("read signalfd");
      exit_code = 1;
      break;
    }

    if (si.ssi_signo == SIGCHLD) {
      int status;
      pid_t wpid;
      while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (wpid == pid) {
          child_exited = 1;
          if (WIFEXITED(status))
            exit_code = WEXITSTATUS(status);
          else if (WIFSIGNALED(status))
            exit_code = 128 + WTERMSIG(status);
        }
      }
    } else if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM) {
      kill(pid, si.ssi_signo);
    }

    if (child_exited)
      break;
  }

  mrb_close(mrb);
  mrb = NULL;

  return exit_code;
}
