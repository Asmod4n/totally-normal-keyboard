#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <mruby.h>
#include <mruby/compile.h>
#include <errno.h>
#include <mruby/presym.h>

static mrb_state *mrb = NULL;

static void handle_signal(int sig) {
    if (mrb) {
        mrb_close(mrb);
        mrb = NULL;
    }
    exit(128 + sig); // conventional exit code for signals
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    mrb = mrb_open();
    if (!mrb) {
        fprintf(stderr, "Could not initialize mruby\n");
        return 1;
    }

    FILE *fp = fopen("main.rb", "r");
    if (!fp) {
        perror("main.rb");
        mrb_close(mrb);
        return 1;
    }

    mrb_load_file(mrb, fp);
    fclose(fp);
    mrb_value tnk = mrb_obj_value(mrb_class_get_id(mrb, MRB_SYM(Tnk)));
    mrb_funcall_id(mrb, tnk, MRB_SYM(run), 0);

    if (mrb->exc && errno != EINTR) {
        mrb_print_error(mrb);
        mrb_close(mrb);
        return 1;
    }

    mrb_close(mrb);
    mrb = NULL;
    return 0;
}
