// parse.c

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include "parse.h"

/* ------------------------------------------------------- split_pipes() -- */
static char **
split_pipes(const char *cmdline, int *count)
{
	int cap = 4;
	int n = 0;
	char **list = malloc(cap * sizeof(char *));

	const char *start = cmdline;
	const char *p = cmdline;

	while (1) {
		if (*p == '|' || *p == '\0') {
			int len = p - start;
			char *seg = malloc(len + 1);
			memcpy(seg, start, len);
			seg[len] = '\0';

			if (n + 1 >= cap) {
				cap *= 2;
				list = realloc(list, cap * sizeof(char *));
			}
			list[n++] = seg;

			if (*p == '\0')
				break;

			p++;            // skip '|'
			start = p;
			continue;
		}
		p++;
	}

	*count = n;
	return list;
}

/* ----------------------------------------------------- expand_tildes() -- */
static char *
expand_tilde(const char *arg)
{
    if (arg[0] != '~')
        return strdup(arg);

    const char *home = getenv("HOME");
    if (!home)
        return strdup(arg);

    size_t len_home = strlen(home);
    size_t len_arg  = strlen(arg);

    char *out = malloc(len_home + len_arg);
    strcpy(out, home);
    strcat(out, arg + 1);

    return out;
}

/* -------------------------------------------------------- expand_env() -- */
static char *
expand_env(const char *arg)
{
	int buflen = strlen(arg) * 4 + 1;
    char *out = malloc(buflen);				// big enough buffer?
    int oi = 0;

    for (int i = 0; arg[i]; i++) {
        if (arg[i] == '$') {
            const char *start = arg + i + 1;
            char var[256];
            int vi = 0;

            if (*start == '{') {
                start++;
                while (*start && *start != '}' && vi < 255)
                    var[vi++] = *start++;
                if (*start == '}')
                    i += vi + 2;
                else
                    i += vi + 1;
            } else {
                while (isalnum((unsigned char)*start) || *start == '_') {
                    var[vi++] = *start++;
                }
                i += vi;
            }

            var[vi] = '\0';
            const char *val = getenv(var);
            if (val) {
				// allocate enough buffer space
				int envlen = strlen(val);
				int trylen = strlen(out) + envlen + 2;
				if (trylen > buflen) {
					out = realloc(out, trylen);
					buflen = trylen;
				}
                strcpy(out + oi, val);
                oi += envlen;
            }
        } else {
            out[oi++] = arg[i];
        }
    }

    out[oi] = '\0';
    return out;
}

/* -------------------------------------------------- expand_backticks() -- */
static char *
expand_backticks(const char *arg)
{
    const char *p = arg;
    char *out = malloc(strlen(arg) * 4 + 1);
    int oi = 0;

    while (*p) {
        if (*p == '`') {
            p++;
            char cmd[1024];
            int ci = 0;

            while (*p && *p != '`' && ci < 1023)
                cmd[ci++] = *p++;

            if (*p == '`')
                p++;

            cmd[ci] = '\0';

            FILE *fp = popen(cmd, "r");
            if (fp) {
                char buf[256];
                while (fgets(buf, sizeof(buf), fp)) {
                    size_t len = strlen(buf);
                    if (oi + len >= 4096) break;
                    memcpy(out + oi, buf, len);
                    oi += len;
                }
                pclose(fp);
            }
        } else {
            out[oi++] = *p++;
        }
    }

    out[oi] = '\0';
    return out;
}

/* ------------------------------------------------------- expand_glob() -- */
static char **
expand_glob(const char *arg, int *count)
{
    glob_t g;
    memset(&g, 0, sizeof(g));

    int r = glob(arg, 0, NULL, &g);

    if (r == 0) {
        *count = g.gl_pathc;
        char **out = malloc(sizeof(char *) * g.gl_pathc);
        for (size_t i = 0; i < g.gl_pathc; i++)
            out[i] = strdup(g.gl_pathv[i]);
        globfree(&g);
        return out;
    }

    *count = 1;
    char **out = malloc(sizeof(char *));
    out[0] = strdup(arg);
    return out;
}

/* ------------------------------------------------ parse_redirections() -- */
static void
parse_redirections(char ***argv_ptr, int *argc_ptr, struct redirs *r)
{
    char **argv = *argv_ptr;
    int argc = *argc_ptr;

    r->stdout_file = NULL;
    r->stderr_file = NULL;
    r->stdout_append = 0;
    r->stderr_append = 0;

    int out_i = 0;

    for (int i = 0; i < argc; i++) {
        char *tok = argv[i];

        // stdout >
        if (strcmp(tok, ">") == 0 && i + 1 < argc) {
            r->stdout_file = strdup(argv[++i]);
            r->stdout_append = false;
            continue;
        }

        // stdout >>
        if (strcmp(tok, ">>") == 0 && i + 1 < argc) {
            r->stdout_file = strdup(argv[++i]);
            r->stdout_append = true;
            continue;
        }

        // stderr 2>
        if (strcmp(tok, "2>") == 0 && i + 1 < argc) {
            r->stderr_file = strdup(argv[++i]);
            r->stderr_append = false;
            continue;
        }

        // stderr 2>>
        if (strcmp(tok, "2>>") == 0 && i + 1 < argc) {
            r->stderr_file = strdup(argv[++i]);
            r->stderr_append = true;
            continue;
        }

        // &> both
        if (strcmp(tok, "&>") == 0 && i + 1 < argc) {
            char *f = strdup(argv[++i]);
            r->stdout_file = strdup(f);
            r->stderr_file = strdup(f);
            r->stdout_append = false;
            r->stderr_append = false;
            free(f);
            continue;
        }

        // &>> both append
        if (strcmp(tok, "&>>") == 0 && i + 1 < argc) {
            char *f = strdup(argv[++i]);
            r->stdout_file = strdup(f);
            r->stderr_file = strdup(f);
            r->stdout_append = true;
            r->stderr_append = true;
            free(f);
            continue;
        }

        // keep normal argument
        argv[out_i++] = tok;
    }

    argv[out_i] = NULL;
    *argc_ptr = out_i;
}

/* -------------------------------------------------- parse_background() -- */
static bool
parse_background(char ***argv_ptr, int *argc_ptr)
{
    char **argv = *argv_ptr;
    int argc = *argc_ptr;

    if (argc == 0)
        return false;

    // background operator "&" must be the last token
    if (strcmp(argv[argc - 1], "&") == 0) {
        free(argv[argc - 1]);   // remove "&"
        argv[argc - 1] = NULL;
        *argc_ptr = argc - 1;
        return true;
    }

    return false;
}

/* ----------------------------------------------------- parse_segment() -- */
static struct cmdinfo
parse_segment(const char *cmdline)
{
	struct cmdinfo ci; //  = malloc(sizeof (struct cmdinfo));
	ci.argv = NULL;
	ci.argc = 0;
	ci.background = 0;
	ci.r.stdout_file = NULL;
	ci.r.stderr_file = NULL;
	ci.r.stdout_append = 0;
	ci.r.stderr_append = 0;

    int argc = 0;
    int capacity = 8;
    char **argv = malloc(capacity * sizeof(char *));
    const char *p = cmdline;

    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        int bufcap = 64;
        int len = 0;
        char *arg = malloc(bufcap);

        int in_single = 0, in_double = 0;

        while (*p) {
            char c = *p;

            if (!in_single && !in_double && isspace((unsigned char)c))
                break;

            if (c == '\'' && !in_double) {
                in_single = !in_single;
                p++;
                continue;
            }

            if (c == '"' && !in_single) {
                in_double = !in_double;
                p++;
                continue;
            }

            if (c == '\\' && !in_single) {
                p++;
                if (*p) c = *p;
            }

            if (len + 1 >= bufcap) {
                bufcap *= 2;
                arg = realloc(arg, bufcap);
            }

            arg[len++] = c;
            p++;
        }

        arg[len] = '\0';

        /* ---- EXPANSION PIPELINE ---- */

        char *t1 = expand_tilde(arg);
        free(arg);

        char *t2 = expand_env(t1);
        free(t1);

        char *t3 = expand_backticks(t2);
        free(t2);

        int gcount = 0;
        char **glist = expand_glob(t3, &gcount);
        free(t3);

        for (int i = 0; i < gcount; i++) {
            if (argc + 1 >= capacity) {
                capacity *= 2;
                argv = realloc(argv, capacity * sizeof(char *));
            }
            argv[argc++] = glist[i];
        }
        free(glist);
    }

    argv[argc] = NULL;

	parse_redirections(&argv, &argc, &ci.r);

	ci.background = parse_background(&argv, &argc);

	ci.argv = argv;
	ci.argc = argc;

	// detect VAR=value tokens at the beginning
	int ei;
	for (ei = 0; ei < argc; ei++) {
		char *eq = strchr(argv[ei], '=');
		if (!eq)				// no assignment
			break;
		if (eq == argv[ei])		// "=value" is invalid
			break;
		ci.envs = realloc(ci.envs, sizeof (struct env_dict) * (ei + 1));
		ci.envs[ei].name = strndup(argv[ei], eq - argv[ei]);
		ci.envs[ei].value = strdup(eq + 1);
	}
	if (ei > 0) {
		for (int i = ei; i < argc; i++)
			argv[i - ei] = argv[i];
		ci.argc = argc - ei;
		argv[ci.argc] = NULL;
	}

    return ci;
}

/* ------------------------------------------------------- split_pipes() -- */
/*
static char **
split_pipes(const char *cmdline, int *count)
{
	int cap = 4;
	int n = 0;
	char **list = malloc(cap * sizeof(char *));

	const char *start = cmdline;
	const char *p = cmdline;

	while (1) {
		if (*p == '|' || *p == '\0') {
			int len = p - start;
			char *seg = malloc(len + 1);
			memcpy(seg, start, len);
			seg[len] = '\0';

			if (n + 1 >= cap) {
				cap *= 2;
				list = realloc(list, cap * sizeof(char *));
			}
			list[n++] = seg;

			if (*p == '\0')
				break;

			p++;				// skip '|'
			start = p;
			continue;
		}
		p++;
	}

	*count = n;
	return list;
}
*/

/* ----------------------------------------------------- parse_command() -- */
struct pipeline
parse_command(const char *cmdline)
{
    struct pipeline pl;
    pl.cmds = NULL;
    pl.count = 0;

    int nseg = 0;
    char **segments = split_pipes(cmdline, &nseg);

    pl.cmds = malloc(nseg * sizeof(struct cmdinfo));
    pl.count = nseg;

    for (int i = 0; i < nseg; i++) {
        pl.cmds[i] = parse_segment(segments[i]);
        free(segments[i]);
    }

    free(segments);

    /* Only the last command may have & */
    for (int i = 0; i < nseg - 1; i++)
        pl.cmds[i].background = 0;

    return pl;
}

/* vim: set ts=4 sw=4 ai noet: */
