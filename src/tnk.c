#include <mruby.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <unistd.h>
#include <mruby/error.h>
#include <mruby/variable.h>
#include <mruby/presym.h>

static mrb_value grab(mrb_state *mrb, mrb_value self)
{
    mrb_value fd_val;
    mrb_get_args(mrb, "o", &fd_val);
    int fd = (int) mrb_integer(mrb_type_convert(mrb, fd_val, MRB_TT_INTEGER, MRB_SYM(fileno)));
    ioctl(fd, EVIOCGRAB, 0);
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        mrb_sys_fail(mrb, "ioctl(EVIOCGRAB, 1)");
    }

    return self;
}

static mrb_value ungrab(mrb_state *mrb, mrb_value self)
{
    mrb_value fd;
    mrb_get_args(mrb, "o", &fd);
    if (ioctl((int) mrb_integer(mrb_type_convert(mrb, fd, MRB_TT_INTEGER, MRB_SYM(fileno))), EVIOCGRAB, 0) < 0) {
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
}

void mrb_totally_normal_keyboard_gem_final(mrb_state* mrb)
{
    if (mrb_nil_p(mrb_gv_get(mrb, MRB_GVSYM(USER_MRB)))) {
        mrb_value tnk = mrb_obj_value(mrb_class_get_id(mrb, MRB_SYM(Tnk)));
        mrb_value instance = mrb_cv_get(mrb, tnk, MRB_IVSYM(instance));
        if (!mrb_nil_p(instance))
            mrb_funcall_id(mrb, tnk, MRB_SYM(close), 0);
    }
}
