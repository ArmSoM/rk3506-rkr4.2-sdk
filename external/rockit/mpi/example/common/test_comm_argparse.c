/**
 * Copyright (C) 2012-2015 Yecheng Fu <cofyc.jackson at gmail dot com>
 * All rights reserved.
 *
 * Use of this source code is governed by a MIT-style license that can be found
 * in the LICENSE file.
 *
 *   module: argarse, developped by cofyc.jackson
 *  project: https://github.com/cofyc/argparse
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "test_comm_argparse.h"
#include "rk_osal.h"

#define OPT_UNSET 1
#define OPT_LONG  (1 << 1)

static const char* prefix_skip(const char *str, const char *prefix) {
    size_t len = strlen(prefix);
    return strncmp(str, prefix, len) ? NULL : str + len;
}

static int prefix_cmp(const char *str, const char *prefix) {
    for (;; str++, prefix++) {
        if (!*prefix) {
            return 0;
        } else if (*str != *prefix) {
            return (unsigned char)*prefix - (unsigned char)*str;
        }
    }
}

static void argparse_error(struct argparse *self, const struct argparse_option *opt,
                           const char *reason, int flags) {
    (void)self;
    if (flags & OPT_LONG) {
        fprintf(stderr, "error: option `--%s` %s\n", opt->long_name, reason);
    } else {
        fprintf(stderr, "error: option `-%c` %s\n", opt->short_name, reason);
    }
    return;
}

static int argparse_getvalue(struct argparse *self, const struct argparse_option *opt, int flags) {
    const char *s = NULL;
    if (!opt->value)
        goto skipped;
    switch (opt->type) {
      case ARGPARSE_OPT_BOOLEAN:
        if (flags & OPT_UNSET) {
            *(int *)opt->value = *(int *)opt->value - 1;  // NOLINT
        } else {
            *(int *)opt->value = *(int *)opt->value + 1;  // NOLINT
        }
        if (*(int *)opt->value < 0) {  // NOLINT
            *(int *)opt->value = 0;    // NOLINT
        }
        break;
      case ARGPARSE_OPT_BIT:
        if (flags & OPT_UNSET) {
            *(int *)opt->value &= ~opt->data;  // NOLINT
        } else {
            *(int *)opt->value |= opt->data;   // NOLINT
        }
        break;
      case ARGPARSE_OPT_STRING:
        if (self->optvalue) {
            *(const char **)opt->value = self->optvalue;
            self->optvalue             = NULL;
        } else if (self->argc > 1) {
            self->argc--;
            *(const char **)opt->value = *++self->argv;
        } else {
            argparse_error(self, opt, "requires a value", flags);
        }
        break;
      case ARGPARSE_OPT_INTEGER:
        errno = 0;
        if (self->optvalue) {
            *(int *)opt->value = strtol(self->optvalue, (char **)&s, 0);  // NOLINT
            self->optvalue     = NULL;
        } else if (self->argc > 1) {
            self->argc--;
            *(int *)opt->value = strtol(*++self->argv, (char **)&s, 0);   // NOLINT
        } else {
            argparse_error(self, opt, "requires a value", flags);
        }
        if (errno)
            argparse_error(self, opt, strerror(errno), flags);
        if (s[0] != '\0')
            argparse_error(self, opt, "expects an integer value", flags);
        break;
      case ARGPARSE_OPT_FLOAT:
        errno = 0;
        if (self->optvalue) {
            *(float *)opt->value = strtof(self->optvalue, (char **)&s);  // NOLINT
            self->optvalue       = NULL;
        } else if (self->argc > 1) {
            self->argc--;
            *(float *)opt->value = strtof(*++self->argv, (char **)&s);   // NOLINT
        } else {
            argparse_error(self, opt, "requires a value", flags);
        }
        if (errno)
            argparse_error(self, opt, strerror(errno), flags);
        if (s[0] != '\0')
            argparse_error(self, opt, "expects a numerical value", flags);
        break;
      default:
        assert(0);
        break;
    }

skipped:
    if (opt->callback) {
        return opt->callback(self, opt);
    }

    return 0;
}

static void argparse_options_check(const struct argparse_option *options) {
    for (; options->type != ARGPARSE_OPT_END; options++) {
      switch (options->type) {
        case ARGPARSE_OPT_END:
        case ARGPARSE_OPT_BOOLEAN:
        case ARGPARSE_OPT_BIT:
        case ARGPARSE_OPT_INTEGER:
        case ARGPARSE_OPT_FLOAT:
        case ARGPARSE_OPT_STRING:
        case ARGPARSE_OPT_GROUP:
            continue;
        default:
            fprintf(stderr, "wrong option type: %d", options->type);
            break;
        }
    }
}

static int argparse_short_opt(struct argparse *self, const struct argparse_option *options) {
    for (; options->type != ARGPARSE_OPT_END; options++) {
        if (options->short_name == *self->optvalue) {
            self->optvalue = self->optvalue[1] ? self->optvalue + 1 : NULL;
            return argparse_getvalue(self, options, 0);
        }
    }
    return -2;
}

static int argparse_long_opt(struct argparse *self, const struct argparse_option *options) {
    for (; options->type != ARGPARSE_OPT_END; options++) {
        const char *rest;
        int opt_flags = 0;
        if (!options->long_name)
            continue;

        rest = prefix_skip(self->argv[0] + 2, options->long_name);
        if (!rest) {
            // negation disabled?
            if (options->flags & OPT_NONEG) {
                continue;
            }
            // only OPT_BOOLEAN/OPT_BIT supports negation
            if (options->type != ARGPARSE_OPT_BOOLEAN && options->type !=
                ARGPARSE_OPT_BIT) {
                continue;
            }

            if (prefix_cmp(self->argv[0] + 2, "no-")) {
                continue;
            }
            rest = prefix_skip(self->argv[0] + 2 + 3, options->long_name);
            if (!rest)
                continue;
            opt_flags |= OPT_UNSET;
        }
        if (*rest) {
            if (*rest != '=')
                continue;
            self->optvalue = rest + 1;
        }
        return argparse_getvalue(self, options, opt_flags | OPT_LONG);
    }
    return -2;
}

int argparse_init(struct argparse *self, struct argparse_option *options,
              const char *const *usages, int flags) {
    memset(self, 0, sizeof(*self));
    self->options     = options;
    self->usages      = usages;
    self->flags       = flags;
    self->description = NULL;
    self->epilog      = NULL;
    return 0;
}

int argparse_describe(struct argparse *self, const char *description, const char *epilog) {
    self->description = description;
    self->epilog      = epilog;
    return 0;
}

int argparse_parse(struct argparse *self, int argc, const char **argv) {
    self->argc = argc - 1;
    self->argv = argv + 1;
    self->out  = argv;

    argparse_options_check(self->options);

    for (; self->argc > 1; self->argc--, self->argv++) {
        const char *arg = self->argv[0];
        if (arg[0] != '-' || !arg[1]) {
            if (self->flags & ARGPARSE_STOP_AT_NON_OPTION) {
                goto end;
            }
            // if it's not option or is a single char '-', copy verbatim
            self->out[self->cpidx++] = self->argv[0];
            continue;
        }
        // short option
        if (arg[1] != '-') {
            self->optvalue = arg + 1;
            switch (argparse_short_opt(self, self->options)) {
            case -1:
                break;
            case -2:
                goto unknown;
            case -3:
                return -1;
            }
            while (self->optvalue) {
                switch (argparse_short_opt(self, self->options)) {
                case -1:
                    break;
                case -2:
                    goto unknown;
                case -3:
                    return -3;
                }
            }
            continue;
        }
        // if '--' presents
        if (!arg[2]) {
            self->argc--;
            self->argv++;
            break;
        }
        // long option
        switch (argparse_long_opt(self, self->options)) {
        case -1:
            break;
        case -2:
            goto unknown;
        case -3:
            return -3;
        }
        continue;

unknown:
        fprintf(stderr, "error: unknown option `%s`\n", self->argv[0]);
        argparse_usage(self);
        return -1;
    }

end:
    memmove(self->out + self->cpidx, self->argv,
            self->argc * sizeof(*self->out));
    self->out[self->cpidx + self->argc] = NULL;

    return self->cpidx + self->argc;
}

int argparse_usage(struct argparse *self) {
    const char *const *usages = self->usages;

    if (usages) {
        fprintf(stdout, "Usage: %s\n", *usages++);
        while (*usages && **usages)
            fprintf(stdout, "   or: %s\n", *usages++);
    } else {
        fprintf(stdout, "Usage:\n");
    }

    // print description
    if (self->description)
        fprintf(stdout, "%s\n", self->description);

    fputc('\n', stdout);

    const struct argparse_option *options;

    // figure out best width
    size_t usage_opts_width = 0;
    size_t len;
    options = self->options;
    for (; options->type != ARGPARSE_OPT_END; options++) {
        len = 0;
        if ((options)->short_name) {
            len += 2;
        }
        if ((options)->short_name && (options)->long_name) {
            len += 2;           // separator ", "
        }
        if ((options)->long_name) {
            len += strlen((options)->long_name) + 2;
        }
        if (options->type == ARGPARSE_OPT_INTEGER) {
            len += strlen("=<int>");
        }
        if (options->type == ARGPARSE_OPT_FLOAT) {
            len += strlen("=<flt>");
        } else if (options->type == ARGPARSE_OPT_STRING) {
            len += strlen("=<str>");
        }
        len = (len + 3) - ((len + 3) & 3);
        if (usage_opts_width < len) {
            usage_opts_width = len;
        }
    }
    usage_opts_width += 4;      // 4 spaces prefix

    options = self->options;
    for (; options->type != ARGPARSE_OPT_END; options++) {
        size_t pos = 0;
        int pad    = 0;
        if (options->type == ARGPARSE_OPT_GROUP) {
            fputc('\n', stdout);
            fprintf(stdout, "%s", options->help);
            fputc('\n', stdout);
            continue;
        }
        pos = fprintf(stdout, "    ");
        if (options->short_name) {
            pos += fprintf(stdout, "-%c", options->short_name);
        }
        if (options->long_name && options->short_name) {
            pos += fprintf(stdout, ", ");
        }
        if (options->long_name) {
            pos += fprintf(stdout, "--%s", options->long_name);
        }
        if (options->type == ARGPARSE_OPT_INTEGER) {
            pos += fprintf(stdout, "=<int>");
        } else if (options->type == ARGPARSE_OPT_FLOAT) {
            pos += fprintf(stdout, "=<flt>");
        } else if (options->type == ARGPARSE_OPT_STRING) {
            pos += fprintf(stdout, "=<str>");
        }
        if (pos <= usage_opts_width) {
            pad = usage_opts_width - pos;
        } else {
            fputc('\n', stdout);
            pad = usage_opts_width;
        }
        fprintf(stdout, "%*s%s\n", pad + 2, "", options->help);
    }

    // print epilog
    if (self->epilog)
        fprintf(stdout, "%s\n", self->epilog);

    return 0;
}

int argparse_help_cb(struct argparse *self, const struct argparse_option *option) {
    (void)option;
    argparse_usage(self);
    return -3;
}

#ifdef OS_RTT
int thread_excute_main(void *arg)
{
    struct _main_param *p;
    p = (struct _main_param *)arg;
    rkos_kthread_set_name(p->argv[0]);
    p->pmain(p->argc, p->argv);
    for (int i = 1; i < p->argc; i++)
        rkos_vfree(p->argv[i]);
    rkos_vfree(p->argv);
    rkos_vfree(p);
    return 0;
}

int argparse_excute_main(int argc, char **argv, int (* pMain)(int argc, char **argv))
{
    if (argv[argc - 1][0] != '&')
    {
        if (pMain)
            return pMain(argc, argv);
    }
    else
    {
        char **argv_tmp = rkos_vcalloc(1, sizeof(char *) * argc);
        for (int i = 0; i < (argc - 1); i++) {
            argv_tmp[i] = rkos_vcalloc(1, strlen(argv[i]) + 1);
            memcpy(argv_tmp[i], argv[i], strlen(argv[i]));
            argv_tmp[i][strlen(argv[i])] = 0;
        }
        struct _main_param *p = rkos_vcalloc(1, sizeof(struct _main_param));
        p->argc = argc;
        p->argv = argv_tmp;
        p->pmain = pMain;
        rkos_kthread_create(thread_excute_main, p, argv_tmp[0], 81920, 0);
    }
    return 0;
}
#endif
