#define _XOPEN_SOURCE 700
#define _GNU_SOURCE
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/presym.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/error.h>
#include <ftw.h>
#include "tnk_common.h"

static mrb_state *mrb = NULL;
static mrb_state *user_mrb = NULL;

static void handle_signal(int sig) {
    if (mrb) {
        mrb_close(mrb);
        mrb = NULL;
    }
    if (user_mrb) {
        mrb_close(user_mrb);
        user_mrb = NULL;
    }
    exit(128 + sig);
}

static uid_t target_uid;
static gid_t target_gid;

static int chown_cb(const char *fpath, const struct stat *sb,
                    int typeflag, struct FTW *ftwbuf) {
    if (chown(fpath, target_uid, target_gid) != 0) {
        perror(fpath);
        return -1;
    }
    return 0;
}

static int
drop_privileges(const char *username) {
    char target_dir[PATH_MAX];
    int len = snprintf(target_dir, sizeof(target_dir),
             "%s/share/totally-normal-keyboard", TNK_PREFIX);
    if (len < 0 || (size_t)len >= sizeof(target_dir)) {
        fprintf(stderr, "path too long");
        return -1;
    }
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "User %s not found\n", username);
        return -1;
    }

    target_uid = pw->pw_uid;
    target_gid = pw->pw_gid;

    // Recursively chown the directory and its contents
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

static void
mrb_totally_normal_keyboard_user_init(void)
{
#ifdef MRB_DEBUG
    mrb_gv_set(user_mrb,
               mrb_intern_lit(user_mrb, "$DEBUG"),
               mrb_true_value());
#endif
    struct RClass *tnk = mrb_define_class_id(user_mrb, MRB_SYM(Tnk), mrb->object_class);
    struct RClass *hotkeys = mrb_define_module_under_id(user_mrb, tnk, MRB_SYM(Hotkeys));
    mrb_define_module_function_id(user_mrb, hotkeys, MRB_SYM(generate_hid_report), mrb_generate_hid_report, MRB_ARGS_ANY());
    if(user_mrb->exc) {
        mrb_print_error(user_mrb);
        _Exit(1);
    }
}

static void
mrb_tnk_load_hotkeys(void)
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
    mrb_load_nstring(user_mrb, hotkeys_rb, sizeof(hotkeys_rb) - 1);
    if (mrb_check_error(user_mrb)) {
        mrb_print_error(user_mrb);
        _Exit(1);
    }
}

static void
mrb_tnk_user_mrb_init(void)
{
    if (user_mrb) {
        abort();
    }
    user_mrb = mrb_open_core();
    if(!user_mrb) {
        perror("mrb_open_core()");
        _Exit(1);
    }
    mrb_totally_normal_keyboard_user_init();
    mrb_tnk_load_hotkeys();
}

static mrb_value
tnk_yield_block_protected(mrb_state *vm, void *userdata)
{
    mrb_value blk = *(mrb_value *)userdata;
    return mrb_yield_argv(vm, blk, 0, NULL);
}

static mrb_value
tnk_handle_hid_report_bridge(mrb_state *_mrb, mrb_value self)
{
    mrb_value buf;
    mrb_get_args(_mrb, "S", &buf);

    struct RClass *tnk_h     = mrb_class_get_id(user_mrb, MRB_SYM(Tnk));
    struct RClass *hotkeys_h = mrb_module_get_under_id(user_mrb, tnk_h, MRB_SYM(Hotkeys));
    mrb_value hotkeys_hash   = mrb_cv_get(user_mrb, mrb_obj_value(hotkeys_h),
                                          mrb_intern_lit(user_mrb, "@@hotkeys"));
    if (!mrb_hash_p(hotkeys_hash)) {
        mrb_raise(user_mrb, E_TYPE_ERROR, "not a hash");
    }

    mrb_value key_h = mrb_str_new(user_mrb, RSTRING_PTR(buf), RSTRING_LEN(buf));
    mrb_value blk = mrb_hash_get(user_mrb, hotkeys_hash, key_h);
    if (!mrb_obj_is_kind_of(user_mrb, blk, user_mrb->proc_class)) {
        return mrb_false_value();
    }

    mrb_bool err = FALSE;
    (void)mrb_protect_error(user_mrb, tnk_yield_block_protected, &blk, &err);
    mrb_gc_arena_restore(user_mrb, 0);

    if (err || mrb_check_error(user_mrb)) {
        mrb_print_error(user_mrb);
        return mrb_false_value();
    }
    return mrb_true_value();
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    errno = 0;
    mrb = mrb_open();
    if (!mrb) {
        perror("Could not initialize mruby\n");
        return 1;
    }

    struct RClass *tnk_cls = mrb_class_get_id(mrb, MRB_SYM(Tnk));
    mrb_value tnk = mrb_obj_value(tnk_cls);
    mrb_funcall_id(mrb, tnk, MRB_SYM(setup_root), 0);
    if (mrb_check_error(mrb)) {
        mrb_print_error(mrb);
        mrb_close(mrb);
        mrb = NULL;
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mrb_close(mrb);
        mrb = NULL;
        return 1;
    }

    if (pid == 0) {
        const char *drop_user = getenv("TNK_DROP_USER");
        if (!drop_user) drop_user = "nobody";
        if (drop_privileges(drop_user) != 0) {
            _Exit(1);
        }
        struct RClass *hotkeys = mrb_module_get_under_id(mrb, tnk_cls, MRB_SYM(Hotkeys));
        mrb_define_module_function_id(mrb, hotkeys, MRB_SYM(handle_hid_report), tnk_handle_hid_report_bridge, MRB_ARGS_REQ(1));
        mrb_funcall_id(mrb, tnk, MRB_SYM(setup_user), 0);
        if (mrb_check_error(mrb)) {
            mrb_print_error(mrb);
            _Exit(1);
        }
        mrb_tnk_user_mrb_init();

        char path[PATH_MAX];
        if (resolve_tnk_path(mrb,
                            "share/totally-normal-keyboard/user.rb",
                            R_OK,
                            path,
                            sizeof(path)) != 0) {
            _Exit(1);
        }

        FILE *fp = fopen(path, "r");
        if (!fp) {
            perror("user.rb");
            _Exit(1);
        }
        mrb_load_file(user_mrb, fp);
        fclose(fp);
        if (mrb_check_error(user_mrb)) {
            mrb_print_error(user_mrb);
            _Exit(1);
        }
        mrb_gc_arena_restore(user_mrb, 0);
        mrb_gc_arena_restore(mrb, 0);
        mrb_funcall_id(mrb, tnk, MRB_SYM(run), 0);

        if (mrb->exc && errno != EINTR) {
            mrb_print_error(mrb);
            _Exit(1);
        }
        _Exit(0);
    }

    int status;
    waitpid(pid, &status, 0);

    mrb_close(mrb);
    mrb = NULL;
    if (user_mrb) {
        mrb_close(user_mrb);
        user_mrb = NULL;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}
