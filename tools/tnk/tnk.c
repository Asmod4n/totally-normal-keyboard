#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/class.h>
#include <mruby/string.h>

int main(int argc, char **argv) {
  mrb_state *mrb = mrb_open();
  if (!mrb) {
    fprintf(stderr, "Could not initialize mruby\n");
    return 1;
  }

  // Define Ruby code to initialize Tnk and call .run
  const char *ruby_code =
    "tnk = Tnk.new\n"
    "tnk.run\n";

  mrb_load_string(mrb, ruby_code);

  if (mrb->exc) {
    mrb_print_error(mrb);
    mrb_close(mrb);
    return 1;
  }

  mrb_close(mrb);
  return 0;
}