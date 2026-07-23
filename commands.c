// commands.c

#define _COMMANDS_C_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "tsh.h"
#include "commands.h"
#include "jobs.h"

static char SysCmd[1024];	// generic command buffer

/* **************************************************************** */
/*                                                                  */
/*                      tsh internal commands                       */
/*                                                                  */
/* **************************************************************** */

/* Return non-zero if ARG is a valid argument for CALLER, else print
   an error message and return zero. */
static bool
valid_argument(char *argv[])
{
    if (!argv[1] || !*argv[1]) {
        fprintf(stderr, "%s: Argument required.\n", argv[0]);
        return false;
    }

    return true;
}

/*
 * resolve_path()
 *   base: the current directory (e.g., getenv("PWD"))
 *   path: the user-supplied path (relative or absolute)
 *
 * Returns a newly allocated string containing the resolved path.
 * Caller must free() the returned string.
 */
static char *
resolve_path(const char *base, const char *path)
{
	char temp[PATH_MAX];

	/* if path is absolute, just copy it */
	if (path[0] == '/') {
		strncpy(temp, path, sizeof(temp));
		temp[sizeof(temp)-1] = '\0';
	}
	else {
		/* build base/path */
		snprintf(temp, sizeof(temp), "%s/%s", base, path);
	}

	/* Normalize: remove /./ and resolve /../ */
	char *parts[PATH_MAX];
	int count = 0;

	char *copy = strdup(temp);
	char *tok = strtok(copy, "/");

	while (tok) {
		if (strcmp(tok, ".") == 0) {
			/* skip */
		}
		else if (strcmp(tok, "..") == 0) {
			if (count > 0)
				count--;			/* pop */
		}
		else {
			parts[count++] = tok;	/* push */
		}
		tok = strtok(NULL, "/");
	}

	/* rebuild the path */
	char *out = malloc(PATH_MAX);
	out[0] = '\0';

	for (int i = 0; i < count; i++) {
		strcat(out, "/");
		strcat(out, parts[i]);
	}

	/* special case: root */
	if (count == 0)
		strcpy(out, "/");

	free(copy);
	return out;
}

static int
change_path(char *cpath)
{
	static char saved_path[PATH_MAX];
	char temp_path[PATH_MAX];

	if (cpath == NULL) {
		if (saved_path[0] == '\0')
			return 0;
		if (chdir(saved_path) == 0) {
			strcpy(temp_path, getenv("PWD"));
			setenv("PWD", saved_path, 1);
			strcpy(saved_path, temp_path);
			return 0;
		}
		perror(saved_path);
		return 1;
	}
	if (chdir(cpath) == 0) {
		strcpy(temp_path, getenv("PWD"));
		setenv("PWD", cpath, 1);
		strcpy(saved_path, temp_path);
		return 0;
	}
	perror(cpath);
	return 1;
}

int
com_bg(char *argv[])
{
    return job_bg(argv);
}

int
com_cd(char *argv[])
{
	char *cpath;

	if (!argv[1] || !*argv[1]) {
		cpath = getenv("HOME");
		return change_path(cpath);
	}
	cpath = argv[1];
	if (strcmp(cpath, "-") == 0) {
		// go to the previous path
		return change_path(NULL);
	}
	if (*cpath == '/') {
		return change_path(cpath);
	}
	cpath = resolve_path(getenv("PWD"), argv[1]);
	if (change_path(cpath) == 0) {
		free(cpath);
		return 0;
	}
	free(cpath);
	return 1;
}

int
com_echo(char *argv[])
{
	for (register int i = 1; argv[i] && *argv[i]; i++)
		printf("%s ", argv[i]);
	puts("");
	return (1);
}

int
com_fg(char *argv[])
{
    return job_fg(argv);
}

/* Print out help for ARG, or for all of the commands if ARG is
   not present. */
int
com_help(char *argv[])
{
	register int i;
	int printed = 0;

	if (argv[1] && *argv[1]) {
		for (i = 0; commands[i].name; i++) {
			if (strcmp(argv[1], commands[i].name) == 0) {
				printf ("%s\t\t%s.\n", commands[i].name, commands[i].doc);
				printed++;
			}
		}
		return 0;
	}

	if (!argv[1] || !*argv[1]) {
		for (i = 0; commands[i].name; i++) {
			printf("%s\t\t%s\n", commands[i].name, commands[i].doc);
		}
		return 0;
	}

	if (!printed) {
		printf("No commands match '%s'.  Possibilties are:\n", argv[1]);

		for (i = 0; commands[i].name; i++) {
			/* Print in six columns. */
			if (printed == 6) {
				printed = 0;
				printf("\n");
			}

			printf("%s\t", commands[i].name);
			printed++;
		}

		if (printed)
			printf("\n");
	}
	return 0;
}

int
com_history(char *argv[])
{
	(void) argv;
    HIST_ENTRY **list = history_list();
    if (list) {
        for (int i = 0; list[i]; i++) {
            printf("%d: %s\n", i + history_base, list[i]->line);
        }
    }
	return 0;
}

int
com_jobs(char *argv[])
{
	(void) argv;
	list_jobs();
	return 0;
}

/* List the file(s) named in arg. */
int
com_list(char *argv[])
{
	strcpy(SysCmd, "ls -FClg");
	for (register int i = 1; argv[i] && *argv[i]; i++) {
  		strcat(SysCmd, " ");
		strcat(SysCmd, argv[i]);
	}
	return system(SysCmd);
}

/* Print out the current working directory. */
int
com_pwd(char *ignore[])
{
    char dir[PATH_MAX], *s;

	(void) ignore;
    s = getcwd(dir, sizeof (dir) - 1);
    if (s == 0) {
        printf("Error getting pwd: %s\n", dir);
        return 1;
    }

    printf("Current directory is %s\n", dir);
    return 0;
}

/* The user wishes to quit using this program.  Just set DONE non-zero. */
int
com_quit(char *ignore[])
{
	(void) ignore;
    Done = true;
    return 0;
}

#ifdef USE_COM_STAT
int
com_stat(char *argv[])
{
	struct stat finfo;

	if (!valid_argument(argv))
		return 1;

	for (register int i = 1; argv[i] && *argv[i]; i++) {
		if (stat(argv[i], &finfo) == -1) {
			perror(argv[i]);
			return 1;
		}

		printf("Statistics for '%s':\n", argv[i]);

#ifdef __GLIBC__
		printf("%s has %ld link%s, and is %ld byte%s in length.\n", argv[i],
#else
		printf("%s has %hu link%s, and is %lld byte%s in length.\n", argv[i],
#endif
				 finfo.st_nlink,
				(finfo.st_nlink == 1) ? "" : "s",
				 finfo.st_size,
				(finfo.st_size == 1) ? "" : "s");
		printf("Inode Last Change at: %s", ctime (&finfo.st_ctime));
		printf("      Last access at: %s", ctime (&finfo.st_atime));
		printf("    Last modified at: %s", ctime (&finfo.st_mtime));
	}
	return 0;
}
#endif

int
com_view(char *argv[])
{
	if (!valid_argument(argv))
		return 1;

	strcpy(SysCmd, "less");
	for (register int i = 1; argv[i] && *argv[i]; i++) {
		strcat(SysCmd, " ");
		strcat(SysCmd, argv[i]);
	}
	return system(SysCmd);
}

// vim: ts=4 sw=4 ai noet:
