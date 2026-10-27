/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx_link_file, a second name of a file (the Windows DLLs in each game's folder): never over a file that is there,
   nor from one that is not */
#include "common/utilities/common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

int main(void) {
    char *dir = snx_temp_dir(NULL), *a, *b, *other;
    unsigned char* data;
    size_t size;
    assert(dir);
    a = snx_path_join(dir, "runtime.dll");
    b = snx_path_join(dir, "game.dll");
    other = snx_path_join(dir, "other.dll");
    assert(!snx_write_file(a, "library", 7) && !snx_write_file(other, "older", 5));
    assert(!snx_link_file(a, b));
    data = snx_read_file(b, &size);
    assert(data && size == 7 && !memcmp(data, "library", 7));
    free(data);
    assert(snx_link_file(a, other)); /* there already: left as it was, for the caller to copy over */
    data = snx_read_file(other, &size);
    assert(data && size == 5 && !memcmp(data, "older", 5));
    free(data);
    assert(!snx_remove_tree(a));
    assert(snx_link_file(a, other) && snx_is_file(b)); /* none at a; b stays, a name of its own */
    { /* snx_link_tree: the tree's files linked (one volume here), in folders of the destination's own */
        char *src = snx_path_join(dir, "romfs"), *sub = snx_path_join(src, "sub"), *x = snx_path_join(src, "x.bin"),
             *y = snx_path_join(sub, "y.bin"), *dst = snx_path_join(dir, "copy"), *dx = snx_path_join(dst, "x.bin"),
             *dy = snx_format("%s/sub/y.bin", dst), *inside = snx_path_join(src, "inner");
        assert(!snx_mkdirs(sub) && !snx_write_file(x, "texture", 7) && !snx_write_file(y, "model", 5));
        assert(!snx_link_tree(src, dst));
        data = snx_read_file(dy, &size);
        assert(data && size == 5 && !memcmp(data, "model", 5));
        free(data);
        data = snx_read_file(dx, &size);
        assert(data && size == 7 && !memcmp(data, "texture", 7));
        free(data);
#ifndef _WIN32
        {
            struct stat sa, sb;
            assert(!stat(x, &sa) && !stat(dx, &sb) && sa.st_ino == sb.st_ino); /* a second name, no bytes copied */
        }
#endif
        assert(snx_link_tree(src, inside)); /* never into itself */
        free(src);
        free(sub);
        free(x);
        free(y);
        free(dst);
        free(dx);
        free(dy);
        free(inside);
    }
    assert(!snx_remove_tree(dir));
    free(a);
    free(b);
    free(other);
    free(dir);
    puts("link_file: a second name, never over a file, never from none; trees linked");
    return 0;
}
