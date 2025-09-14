#include <mruby.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <unistd.h>
#include <mruby/error.h>
#include <mruby/variable.h>
#include <mruby/presym.h>

static mrb_value grab(mrb_state *mrb, mrb_value self)
{
    mrb_int fd;
    mrb_get_args(mrb, "i", &fd);
    ioctl(fd, EVIOCGRAB, 0);
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

void
mrb_totally_normal_keyboard_gem_init(mrb_state *mrb)
{
#ifdef MRB_DEBUG
    mrb_gv_set(mrb,
               MRB_GVSYM(DEBUG),
               mrb_true_value());
#endif
    struct RClass *tnk = mrb_define_class_id(mrb, MRB_SYM(Tnk), mrb->object_class);
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(grab), grab, MRB_ARGS_REQ(1));
    mrb_define_module_function_id(mrb, tnk, MRB_SYM(ungrab), ungrab, MRB_ARGS_REQ(1));
    mrb_define_const_id(mrb, tnk, MRB_SYM(PREFIX), mrb_str_new_lit(mrb, TNK_PREFIX));
    mrb_define_module_under_id(mrb, tnk, MRB_SYM(Hotkeys));
}

void mrb_totally_normal_keyboard_gem_final(mrb_state* mrb)
{
    mrb_value tnk = mrb_obj_value(mrb_class_get_id(mrb, MRB_SYM(Tnk)));
    mrb_value instance = mrb_cv_get(mrb, tnk, MRB_IVSYM(instance));
    if (!mrb_nil_p(instance))
        mrb_funcall_id(mrb, tnk, MRB_SYM(close), 0);
}
