/*
** tsh.c -- A tiny shell
** June 2026 - cjkim and Copilot
*/

#define _TSH_C_
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#ifdef TARGET_OS_OSX
# include <malloc/malloc.h>
#else
# include <malloc.h>
#endif
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "tsh.h"
#include "parse.h"
#include "commands.h"
#include "jobs.h"
#include "history.h"


static void
free_cmdinfo(struct cmdinfo *ci)
{
	if (ci->r.stdout_file)
		free(ci->r.stdout_file);
	if (ci->r.stderr_file)
		free(ci->r.stderr_file);
	for (int i = 0; i < ci->argc; i++)
		if (ci->argv[i])
			free(ci->argv[i]);
	if (ci->argv)
		free(ci->argv);
}

void
free_pipeline(struct pipeline *pl)
{
	if (!pl || !pl->cmds)
		return;

	for (int i = 0; i < pl->count; i++) {
		free_cmdinfo(&pl->cmds[i]);
	}

	free(pl->cmds);
	pl->cmds = NULL;
	pl->count = 0;
}

/* Look up NAME as the name of a command, and return a pointer to that
 | command.  Return a NULL pointer if NAME isn't a command name. */
COMMAND *
find_command(char *name)
{
    register int i;

    for (i = 0; commands[i].name; i++)
        if (strcmp(name, commands[i].name) == 0)
            return &commands[i];

    return (COMMAND *) NULL;
}

static void
sigchld_handler(int sig)
{
	(void) sig;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
		check_job(pid, status);
	}
}

void
init_shell()
{
    // ignore interactive job-control signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    // put the shell in its own process group
    pid_t shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);

    // take control of the terminal
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    // re-enable SIGCHLD handler
    signal(SIGCHLD, sigchld_handler);
	init_job_slot();
}

int
give_terminal_control(pid_t pgrp, int tty_fd)
{
	struct sigaction ignore_sa, old_ttou_sa, old_ttin_sa;
	int result = 0;
	int saved_errno = 0;

	// 1. Configure a modern sigaction structure to ignore the target signals
	ignore_sa.sa_handler = SIG_IGN;
	sigemptyset(&ignore_sa.sa_mask);
	ignore_sa.sa_flags = 0;

	// 2. Install the ignore handlers and save the original signal actions
	if (sigaction(SIGTTOU, &ignore_sa, &old_ttou_sa) < 0) return -1;
	if (sigaction(SIGTTIN, &ignore_sa, &old_ttin_sa) < 0) {
		// Rollback SIGTTOU if the second sigaction fails
		saved_errno = errno;
		sigaction(SIGTTOU, &old_ttou_sa, NULL);
		errno = saved_errno;
		return -1;
	}

	// 3. Perform the terminal group modification
	if (tcsetpgrp(tty_fd, pgrp) < 0) {
		result = -1;
		saved_errno = errno; // Preserve the original tcsetpgrp error code
	}
	// 4. Restore the original handlers immediately to preserve system behavior
	sigaction(SIGTTOU, &old_ttou_sa, NULL);
	sigaction(SIGTTIN, &old_ttin_sa, NULL);

	// 5. Restore errno if tcsetpgrp failed so the caller can check it
	if (result < 0) {
		errno = saved_errno;
	}

	return result;
}

#ifdef NOT_NEEDED
// execvp() takes care of executing a command found via $PATH
char *
wholepath(char *cmd)
{
	static char **paths = NULL;
	static char filepath[1024];
	if (paths == NULL) {
		char *env_path = NULL;
		env_path = malloc(strlen(getenv("PATH") + 2));
		strcpy(env_path, getenv("PATH"));
		int num_path = 0;
		for (char *cp = env_path; *cp; cp++)
			if (*cp == ':')
				num_path++;
		paths = malloc(sizeof (char *) * num_path + 1);
		char *start = env_path;
		int i = 0;
		for (char *cp = env_path; *cp; cp++) {
			if (*cp == ':') {
				*cp = (char) '\0';
				paths[i] = start;
				i++;
				start = cp + 1;
			}
		}
		paths[i] = NULL;
	}
	for (int i = 0; paths[i]; i++) {
		sprintf(filepath, "%s/%s", paths[i], cmd);
		if (access(filepath, X_OK) == 0)
			return filepath;
	}
	return NULL;
}
#endif // NOT_NEEDED

int
forkexec(struct cmdinfo *ci)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork() failed\n");
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		if (ci->r.stdout_file) {
			int fd = open(ci->r.stdout_file,
					ci->r.stdout_append ? O_WRONLY|O_CREAT|O_APPEND
										: O_WRONLY|O_CREAT|O_TRUNC,
						0666);
			dup2(fd, STDOUT_FILENO);
			close(fd);
		}

		if (ci->r.stderr_file) {
			int fd = open(ci->r.stderr_file,
					ci->r.stderr_append ? O_WRONLY|O_CREAT|O_APPEND
										: O_WRONLY|O_CREAT|O_TRUNC,
						0666);
			dup2(fd, STDERR_FILENO);
			close(fd);
		}
		execvp(ci->argv[0], ci->argv);
		perror("execv()");
		exit(1);
	}
	if (ci->background) {
		printf("[background pid %d]\n", pid);
	}
	else {
		int status;
		waitpid(pid, &status, 0);
	}
	return 0;
}

static void
apply_redirs(struct redirs *r)
{
	if (r->stdout_file) {
		int fd = open(r->stdout_file,
				r->stdout_append ? O_WRONLY|O_CREAT|O_APPEND
								 : O_WRONLY|O_CREAT|O_TRUNC,
				0666);
		dup2(fd, STDOUT_FILENO);
		close(fd);
	}

	if (r->stderr_file) {
		int fd = open(r->stderr_file,
				r->stderr_append ? O_WRONLY|O_CREAT|O_APPEND
								 : O_WRONLY|O_CREAT|O_TRUNC,
				0666);
		dup2(fd, STDERR_FILENO);
		close(fd);
	}
}

static char *
str_trim(const char *s)
{
	while (isspace((unsigned char) *s))
		s++;
	char *out = strdup(s);
	int len = strlen(out);
	while (len > 0 && isspace((unsigned char) out[len-1]))
		out[--len] = '\0';
	return out;
}

static char **
split_semicolons(const char *cmdline, int *count)
{
	int cap = 4;
	int n = 0;
	char **list = malloc(cap * sizeof(char *));

	const char *start = cmdline;
	const char *p = cmdline;

	while (1) {
		if (*p == ';' || *p == '\0') {
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

			p++;            // skip ';'
			start = p;
			continue;
		}
		p++;
	}

	*count = n;
	return list;
}

void
run_pipeline(struct pipeline *pl)
{
    int n = pl->count;
    int pipes[n - 1][2];
    pid_t pids[n];

    // create pipes as many as needed
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    pid_t pgid = 0;

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            // child

            // put all pipeline children in the same process group
            setpgid(0, pgid ? pgid : getpid());

			#ifdef SKIP_IT
            // if this is the first command in a foreground pipeline,
            // give it the terminal
			printf("C@ %d in %s\n", __LINE__, __FILE__);
            if (!pl->cmds[n - 1].background)
				give_terminal_control(getpgid(0), STDIN_FILENO);
			#endif

            // set up pipes
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // close all pipe fds
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // apply redirections
            apply_redirs(&pl->cmds[i].r);

            // exec
            execvp(pl->cmds[i].argv[0], pl->cmds[i].argv);
            perror(pl->cmds[i].argv[0]);
            exit(1);
        }

        // parent
        pids[i] = pid;

        // set process group for the pipeline
        if (i == 0)
            pgid = pid;
        setpgid(pid, pgid);
    }

    // parent closes all pipe fds
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // if background job: do not wait
    if (pl->cmds[n - 1].background) {
        printf("[background pid %d]\n", pgid);
		int slot = alloc_job_slot();
		if (slot >= 0) {
			set_up_job(pgid, slot, pl->cmds[0].argv[0]);
		}
        return;
    }

    // foreground job: give terminal to the job
    tcsetpgrp(STDIN_FILENO, pgid);

    // wait for all children
    for (int i = 0; i < n; i++)
        waitpid(pids[i], NULL, 0);

    // return terminal to shell
    tcsetpgrp(STDIN_FILENO, getpid());
}

bool
execute_line(const char *line)
{
	bool rc = true;
	int ncmds = 0;
	char **cmds = split_semicolons(line, &ncmds);

	for (int n = 0; n < ncmds; n++) {
		struct pipeline pl;
		COMMAND *command;
		char *expanded = expand_bang(cmds[n]);
		if (!expanded) {
			fprintf(stderr, "history: event not found\n");
			rc = false;
			continue;
		}
		char *trimmed = str_trim(expanded);
		free(expanded);
		if (trimmed[0] == '\0') {
			fprintf(stderr, "history: event exapnds to empty command\n");
			free(trimmed);
			continue;
		}
		expanded = trimmed;
		if (!expanded) {
			fprintf(stderr, "history: event not found\n");
			free(expanded);
			rc = false;
			continue;
		}

		if (expanded[0] == '\0' || expanded[0] == ' ') {
			fprintf(stderr, "history: event expands to empty command\n");
			free(expanded);
			rc = false;
			continue;
		}

		// show the expanded command like Bash does
		if (strcmp(expanded, cmds[n]) != 0) {
			printf("%s\n", expanded);
		}

		pl = parse_command(expanded);
		free(expanded);

		if (pl.count < 1) {
			// nothing to do
			rc = false;
			continue;
		}

		command = find_command(pl.cmds[0].argv[0]);
		if (command) {
			rc = (*(command->func))(pl.cmds[0].argv);
			free_pipeline(&pl);
			continue;
		}

		run_pipeline(&pl);
		free_pipeline(&pl);
	}

	for (int n = 0; n < ncmds; n++)
		free(cmds[n]);

	// rc = forkexec(pl.cmds[0]);
	return rc;
}

/* ************************************************************************ */
/*                                                                          */
/*                      Interface to Readline Completion                    */
/*                                                                          */
/* ************************************************************************ */

char **
completion_matches(const char *text, CharFunc gen)
{
    (void) text;
	(void) gen;
	return NULL;
}

/* Generator function for command completion.  STATE lets us know whether
 | to start from scratch; without any state (i.e. STATE == 0), then we
 | start at the top of the list. */
char *
command_generator(char *text, int state, int dummy)
{
    static int list_index, len;
    char *name;

	(void) dummy;

    /* If this is a new word to complete, initialize now.  This includes
     |  saving the length of TEXT for efficiency, and initializing the
     |  index variable to 0. */
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    // return the next name which partially matches from the command list.
    while ((name = commands[list_index].name)) {
        list_index++;

        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }

    // if no names matched, then return NULL. 
    return (char *) NULL;
}

/* Attempt to complete on the contents of TEXT.  START and END show the
 | region of TEXT that contains the word to complete.  We can use the
 | entire line in case we want to do some simple parsing.  Return the
 | array of matches, or NULL if there aren't any. */
char **
fileman_completion(const char *text, int start, int end)
{
    char **matches;
	(void) start;
	(void) end;

    matches = (char **) NULL;

    /* if this word is at the start of the line, then it is a command
     |  to complete.  Otherwise it is the name of a file in the current
     |  directory. */
    if (start == 0)
        matches = completion_matches(text, command_generator);

    return matches;
}

/* Tell the GNU Readline library how to complete.  We want to try to complete
 | on command names if this is the first word in the line, or on filenames
 | if not. */
void
initialize_readline()
{
    // get the history filename 
    char *home = getenv("HOME");
    HistFile = malloc(strlen(home) + 20);
    sprintf(HistFile, "%s/.tsh_history", home);

    read_history(HistFile);

    // allow conditional parsing of the ~/.inputrc file.
    rl_readline_name = "tsh";

    // tell the completer that we want a crack first. 
    rl_attempted_completion_function = fileman_completion;
}

void
check_child_procs()
{
	// continuously reap terminated children without blocking
	pid_t pid;
	int status;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		printf("Child process [%d] completed\n", pid);
	}
}

char *
shell_prompt()
{
	static char *result = NULL;
	char cwd[PATH_MAX]; // To store the current working directory
	char *home = getenv("HOME"); // Get the user's home directory

	if (result) {
		free(result);
		result = NULL;
	}
	check_child_procs();
	// get current working directory
	char *pwd = getenv("PWD");
	if (pwd) {
		strcpy(cwd, pwd);
	}
	else {
		if (getcwd(cwd, sizeof (cwd) - 1) == NULL) {
			fprintf(stderr, "ERROR: Unable to get current working directory\n");
			perror("getcwd() error");
			exit(1);
		}
	}
	// check if the current working directory starts with the home path
	if (home != NULL && strncmp(cwd, home, strlen(home)) == 0) {
		size_t tilde_len = strlen(cwd) - strlen(home) + 2; // +2 for "~" and null terminator
		result = malloc(tilde_len + 6);
		if (!result) {
			fprintf(stderr, "ERROR: Out of memory\n");
			perror("malloc failed");
			exit(1);
		}
		snprintf(result, tilde_len, "~%s", cwd + strlen(home));
	} else {
		// if not under HOME, copy the full current directory
		result = malloc(strlen(cwd) + 6);
		if (!result) {
			fprintf(stderr, "ERROR: Out of memory\n");
			perror("strdup failed");
			exit(1);
		}
		strcpy(result, cwd);
	}
	strcat(result, "> ");
	return result;
}

int
main(int argc, char *argv[])
{
	char *line;

	(void) argc;
	Done = false;
	PrevCmd[0] = '\0';					// initialize
	char *cp = basename(argv[0]);
	ProgName = malloc(strlen(cp) + 1);
	strcpy(ProgName, cp);

	init_shell();
	initialize_readline();				// bind our completer

    // loop reading and executing lines until the user quits
    while (!Done) {
        line = readline(shell_prompt());
        if (!line)						// end of input like ^D
            break;
		char *trimmed = str_trim(line);
		free(line);
		line = trimmed;
		if (strlen(line) > 1) {
			if (execute_line(line)) {
				add_history(line);
				strncpy(PrevCmd, line, sizeof (PrevCmd) - 1);
			}
		}
		free(line);						// readline() mallocs
    }
	write_history(HistFile);
	free(ProgName);
	exit(0);
}

// vim: ts=4 sw=4 ai noet: 
