#ifndef TNK_PATH_H
#define TNK_PATH_H

#include <stddef.h>
#include <mruby.h>

/**
 * Resolve a file path under TNK_PREFIX safely.
 *
 * @param mrb        MRuby state (for raising errors)
 * @param rel_path   Relative path under TNK_PREFIX (no leading '/')
 * @param mode       F_OK, R_OK, X_OK, etc. for access() check
 * @param out        Buffer to receive the resolved absolute path
 * @param out_size   Size of out buffer
 * @return 0 on success, -1 on error (mrb_raise called)
 */
int resolve_tnk_path(mrb_state *mrb,
                     const char *rel_path,
                     int mode,
                     char *out,
                     size_t out_size);

#endif
