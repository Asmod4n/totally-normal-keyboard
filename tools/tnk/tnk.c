#include <stdio.h>
#include <stdlib.h>
#include <mruby.h>
#include <mruby/compile.h>

int main(int argc, char **argv) {
    mrb_state *mrb = mrb_open();
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

    /* parse and run the file */
    mrb_load_file(mrb, fp);
    fclose(fp);

    if (mrb->exc) {
        mrb_print_error(mrb);
        mrb_close(mrb);
        return 1;
    }

    mrb_close(mrb);
    return 0;
}
