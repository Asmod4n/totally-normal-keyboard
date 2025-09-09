#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
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
#include <ftw.h>
#include "tnk_path.h"

static mrb_state *mrb = NULL;

static void handle_signal(int sig) {
    if (mrb) {
        mrb_close(mrb);
        mrb = NULL;
    }
    exit(128 + sig);
}

static uid_t target_uid;
static gid_t target_gid;

static int chown_cb(const char *fpath, const struct stat *sb,
                    int typeflag, struct FTW *ftwbuf) {
    // Change owner/group of each file/dir
    if (chown(fpath, target_uid, target_gid) != 0) {
        perror(fpath);
        return -1; // stop on error
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

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    errno = 0;
    mrb = mrb_open();
    if (!mrb) {
        perror("Could not initialize mruby\n");
        return 1;
    }

    // Call Tnk.setup as root
    mrb_value tnk = mrb_obj_value(mrb_class_get_id(mrb, MRB_SYM(Tnk)));
    mrb_funcall_id(mrb, tnk, MRB_SYM(setup_root), 0);

    if (mrb->exc) {
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
        // --- Child: drop privileges and run ---
        if (drop_privileges(drop_user) != 0) {
            _Exit(1);
        }
        mrb_funcall_id(mrb, tnk, MRB_SYM(setup_user), 0);

        char path[PATH_MAX];
        if (resolve_tnk_path(mrb,
                            "share/totally-normal-keyboard/main.rb",
                            R_OK,
                            path,
                            sizeof(path)) != 0) {
            _Exit(1);
        }

        FILE *fp = fopen(path, "r");
        if (!fp) {
            perror("main.rb");
            _Exit(1);
        }
        mrb_load_file(mrb, fp);
        fclose(fp);
        if (mrb->exc) {
            mrb_print_error(mrb);
            _Exit(1);
        }
        mrb_funcall_id(mrb, tnk, MRB_SYM(after_setup), 0);
        if (mrb->exc) {
            mrb_print_error(mrb);
            _Exit(1);
        }
        mrb_funcall_id(mrb, tnk, MRB_SYM(run), 0);

        if (mrb->exc && errno != EINTR) {
            mrb_print_error(mrb);
            _Exit(1);
        }
        _Exit(0);
    }

    // --- Parent: still root, wait for child ---
    int status;
    waitpid(pid, &status, 0);

    // Now close VM as root (runs your finalizer/Tnk.close)
    mrb_close(mrb);
    mrb = NULL;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}
