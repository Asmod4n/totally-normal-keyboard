#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <mruby.h>
#include <mruby/compile.h>
#include <errno.h>

static mrb_state *global_mrb = NULL;

static void handle_signal(int sig) {
    if (global_mrb) {
        mrb_close(global_mrb);
        global_mrb = NULL;
    }
    exit(128 + sig); // conventional exit code for signals
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    global_mrb= mrb_open();
    if (!global_mrb) {
        fprintf(stderr, "Could not initialize mruby\n");
        return 1;
    }

    FILE *fp = fopen("main.rb", "r");
    if (!fp) {
        perror("main.rb");
        mrb_close(global_mrb);
        return 1;
    }

    mrb_load_file(global_mrb, fp);
    fclose(fp);

    if (global_mrb->exc && errno != EINTR) {
        mrb_print_error(global_mrb);
        mrb_close(global_mrb);
        return 1;
    }

    mrb_close(global_mrb);
    global_mrb = NULL;
    return 0;
}
