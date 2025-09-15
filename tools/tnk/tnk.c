#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <mruby.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <mruby/error.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <ftw.h>
#include <grp.h>
#include <sys/wait.h>
#include <linux/keyboard.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/presym.h>
#include <mruby/compile.h>
#include <mruby/msgpack.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <libgen.h>
#include <mruby/array.h>

static uid_t target_uid;
static gid_t target_gid;

static int chown_cb(const char *fpath, const struct stat *sb,
                    int typeflag, struct FTW *ftwbuf) {
    if (lchown(fpath, target_uid, target_gid) != 0) {
        perror(fpath);
        return -1;
    }
    return 0;
}

static int
resolve_tnk_path(mrb_state *mrb,
                 const char *rel_path,
                 int mode,
                 char *out,
                 size_t out_size)
{
    char exe_path[PATH_MAX] = {0};
    char base_dir[PATH_MAX] = {0};
    char joined[PATH_MAX] = {0};
    char resolved[PATH_MAX] = {0};

    // Get absolute path to current executable
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        mrb_sys_fail(mrb, "readlink(/proc/self/exe)");
        return -1;
    }
    exe_path[len] = '\0';

    // Copy to base_dir because dirname() may modify its argument
    char *end = stpncpy(base_dir, exe_path, sizeof(base_dir));
    if (end == base_dir + sizeof(base_dir)) {
        // No NUL written — source length >= buffer size
        base_dir[sizeof(base_dir) - 1] = '\0';
        // Handle truncation here if needed
    }



    if (!realpath(dirname(base_dir), base_dir)) {
        mrb_sys_fail(mrb, "realpath(base_dir)");
        return -1;
    }

    // Join base_dir and rel_path
    if (snprintf(joined, sizeof(joined), "%s/%s", base_dir, rel_path) >= (int)sizeof(joined)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "path too long");
        return -1;
    }

    if (!realpath(joined, resolved)) {
        mrb_sys_fail(mrb, "realpath(joined)");
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

static int
drop_privileges(mrb_state *mrb, const char *username) {
    char target_dir[PATH_MAX];
    if (resolve_tnk_path(mrb, "../share/totally-normal-keyboard", F_OK, target_dir, sizeof(target_dir)) != 0) {
        return -1;
    }

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "User %s not found\n", username);
        return -1;
    }

    target_uid = pw->pw_uid;
    target_gid = pw->pw_gid;

    if (nftw(target_dir, chown_cb, 16, FTW_PHYS) != 0) {
        fprintf(stderr, "Failed to chown %s recursively\n", target_dir);
        return -1;
    }

    if (initgroups(pw->pw_name, pw->pw_gid) != 0) { perror("initgroups"); return -1; }
    if (setgid(pw->pw_gid) != 0) { perror("setgid"); return -1; }
    if (setuid(pw->pw_uid) != 0) { perror("setuid"); return -1; }

    if (setuid(0) != -1) {
        fprintf(stderr, "Privilege drop failed: still root!\n");
        return -1;
    }

    return 0;
}

#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

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

static bool utf8_next_cp(const char *s, size_t len, uint32_t *cp, size_t *consumed) {
  if (len == 0) return false;
  const unsigned char c0 = (unsigned char)s[0];

  if (c0 < 0x80) { // 1-byte ASCII
    *cp = c0;
    *consumed = 1;
    return true;
  }

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

static unsigned short plain_map[NR_KEYS];
static bool keymap_loaded = false;

static void
parse_plain_map_stream(FILE *fp) {
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
            while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\n') p++;
            if (*p == '\0') break;

            char *end;
            unsigned long val = strtoul(p, &end, 0);
            if (p != end && idx < NR_KEYS) {
                plain_map[idx++] = (unsigned short)val;
            }
            p = end;
        }
    }

    if (idx != NR_KEYS) {
        fprintf(stderr, "Warning: expected %d keys, got %d\n", NR_KEYS, idx);
    }
}

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

static mrb_value
gen_keymap(mrb_state *mrb, mrb_value self)
{
    if (keymap_loaded) return mrb_true_value();

    mrb_value xkbmodel   = mrb_nil_value();
    mrb_value xkblayout  = mrb_nil_value();
    mrb_value xkbvariant = mrb_nil_value();
    mrb_value xkboptions = mrb_nil_value();

    // Parse /etc/default/keyboard
    FILE *kf = fopen("/etc/default/keyboard", "r");
    if (!kf) mrb_sys_fail(mrb, "fopen /etc/default/keyboard");

    char line[512];
    while (fgets(line, sizeof(line), kf)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *name = p;
        char *val = eq + 1;

        // strip newline
        size_t len = strlen(val);
        while (len && (val[len-1] == '\n' || val[len-1] == '\r')) val[--len] = '\0';

        // strip quotes
        if ((val[0] == '"' && val[len-1] == '"') || (val[0] == '\'' && val[len-1] == '\'')) {
            val[len-1] = '\0';
            val++;
        }

        if (strcmp(name, "XKBMODEL") == 0)   xkbmodel   = mrb_str_new_cstr(mrb, val);
        else if (strcmp(name, "XKBLAYOUT") == 0)  xkblayout  = mrb_str_new_cstr(mrb, val);
        else if (strcmp(name, "XKBVARIANT") == 0) xkbvariant = mrb_str_new_cstr(mrb, val);
        else if (strcmp(name, "XKBOPTIONS") == 0) xkboptions = mrb_str_new_cstr(mrb, val);
    }
    fclose(kf);

    // Fail fast if missing
    if (mrb_nil_p(xkbmodel) || mrb_nil_p(xkblayout) ||
        mrb_nil_p(xkbvariant) || mrb_nil_p(xkboptions)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "missing required keyboard config");
    }

    // Pipe: ckbcomp -> loadkeys
    int pipefd[2];
    if (pipe(pipefd) == -1) mrb_sys_fail(mrb, "pipe");

    pid_t pid = fork();
    if (pid == -1) mrb_sys_fail(mrb, "fork");

    if (pid == 0) {
        // child: run ckbcomp | loadkeys, output to stdout (pipefd[1])
        int mid[2];
        if (pipe(mid) == -1) _exit(127);

        pid_t pid_ckb = fork();
        if (pid_ckb == -1) _exit(127);

        if (pid_ckb == 0) {
            // ckbcomp
            close(mid[0]);
            dup2(mid[1], STDOUT_FILENO);
            close(mid[1]);
            execl("/usr/bin/ckbcomp", "ckbcomp", "-compact",
                  RSTRING_CSTR(mrb, xkblayout),
                  RSTRING_CSTR(mrb, xkbvariant),
                  RSTRING_CSTR(mrb, xkbmodel),
                  RSTRING_CSTR(mrb, xkboptions),
                  (char *)NULL);
            _exit(127);
        }

        // loadkeys
        close(mid[1]);
        dup2(mid[0], STDIN_FILENO);
        close(mid[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execl("/usr/bin/loadkeys", "loadkeys", "-u", "--mktable", (char *)NULL);
        _exit(127);
    }

    // parent: capture output
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) mrb_sys_fail(mrb, "fdopen");

    parse_plain_map_stream(fp);
    fclose(fp);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "pipeline failed (exit code %d)", WEXITSTATUS(status));
    }

    keymap_loaded = true;
    return mrb_true_value();
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
mrb_generate_hid_report(mrb_state *mrb, mrb_value self) {
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

static bool
mrb_totally_normal_keyboard_user_init(mrb_state *user_mrb)
{
#ifdef MRB_DEBUG
    mrb_gv_set(user_mrb,
               MRB_GVSYM(DEBUG),
               mrb_true_value());
#endif
    struct RClass *tnk = mrb_define_class_id(user_mrb, MRB_SYM(Tnk), user_mrb->object_class);
    struct RClass *hotkeys = mrb_define_module_under_id(user_mrb, tnk, MRB_SYM(Hotkeys));
    mrb_define_module_function_id(user_mrb, hotkeys, MRB_SYM(generate_hid_report), mrb_generate_hid_report, MRB_ARGS_ANY());
    if(user_mrb->exc) {
        mrb_print_error(user_mrb);
        mrb_clear_error(user_mrb);
        return false;
    }
    return true;
}

static bool
mrb_tnk_load_hotkeys(mrb_state *user_mrb)
{
    static const char hotkeys_rb[] =
    "class Tnk\n"
    "  module Hotkeys\n"
    "    @@hotkeys = {}\n"
    "    def self.on_hotkey(*args, &blk)\n"
    "      raise \"no block given\" unless blk\n"
    "      report = generate_hid_report(*args)\n"
    "      @@hotkeys[report] = blk\n"
    "    end\n"
    "  end\n"
    "end\n";
    mrb_load_nstring(user_mrb, hotkeys_rb, sizeof(hotkeys_rb));
    if (user_mrb->exc) {
        mrb_print_error(user_mrb);
        mrb_clear_error(user_mrb);
        return false;
    }
    return true;
}

static mrb_state*
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

static mrb_value
tnk_yield_block_protected(mrb_state *vm, void *userdata)
{
    mrb_value blk = *(mrb_value *)userdata;
    return mrb_yield_argv(vm, blk, 0, NULL);
}

static mrb_value
tnk_handle_hid_report_bridge(mrb_state *mrb, mrb_value self)
{
    mrb_value buf;
    mrb_get_args(mrb, "S", &buf);

    mrb_state *user_mrb = (mrb_state *) mrb->ud;

    struct RClass *tnk_h     = mrb_class_get_id(user_mrb, MRB_SYM(Tnk));
    struct RClass *hotkeys_h = mrb_module_get_under_id(user_mrb, tnk_h, MRB_SYM(Hotkeys));
    mrb_value hotkeys_hash   = mrb_cv_get(user_mrb, mrb_obj_value(hotkeys_h), MRB_CVSYM(hotkeys));
    if (!mrb_hash_p(hotkeys_hash)) {
        mrb_raise(mrb, E_TYPE_ERROR, "not a hash");
    }

    mrb_value key_h = mrb_str_new(user_mrb, RSTRING_PTR(buf), RSTRING_LEN(buf));
    mrb_value blk = mrb_hash_get(user_mrb, hotkeys_hash, key_h);
    if (mrb_type(blk) != MRB_TT_PROC) {
        return mrb_false_value();
    }

    mrb_bool err = FALSE;
    mrb_value ret = mrb_protect_error(user_mrb, tnk_yield_block_protected, &blk, &err);
    if (user_mrb->exc || err) {
        mrb_print_error(user_mrb);
        mrb_clear_error(user_mrb);
        mrb_gc_arena_restore(user_mrb, 0);
        return mrb_false_value();
    }

    ret = mrb_msgpack_unpack(mrb, mrb_msgpack_pack(user_mrb, ret));
    mrb_gc_arena_restore(user_mrb, 0);
    return ret;
}

static void block_signals(sigset_t *mask) {
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGTERM);
    sigaddset(mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, mask, NULL) == -1) {
        perror("sigprocmask");
        exit(1);
    }
}

int main(int argc, char **argv) {
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
        mrb_ary_push(mrb, argv_ary, mrb_str_new_cstr(mrb, argv[i]));
    }
    mrb_define_global_const(mrb, "ARGV", argv_ary);

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
        if (drop_privileges(mrb, drop_user) != 0) {
            rc = 1;
            goto child_cleanup;
        }
        struct RClass *hotkeys = mrb_module_get_under_id(mrb, tnk_cls, MRB_SYM(Hotkeys));
        mrb_define_module_function_id(mrb, hotkeys, MRB_SYM(handle_hid_report), tnk_handle_hid_report_bridge, MRB_ARGS_REQ(1));
        mrb_define_module_function_id(mrb, tnk_cls, MRB_SYM(gen_keymap), gen_keymap, MRB_ARGS_NONE());
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

        char path[PATH_MAX];
        if (resolve_tnk_path(mrb,
                            "../share/totally-normal-keyboard/user.rb",
                            R_OK,
                            path,
                            sizeof(path)) != 0) {
            rc = 1;
            goto child_cleanup;
        }

        FILE *fp = fopen(path, "r");
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
            break;
        }

        if (si.ssi_signo == SIGCHLD) {
            int status;
            pid_t wpid;
            // Reap all dead children
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
            // Forward signal to child
            kill(pid, si.ssi_signo);
        }

        if (child_exited)
            break;
    }

    mrb_close(mrb);
    mrb = NULL;

    return exit_code;
}
