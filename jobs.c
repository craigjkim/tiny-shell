// jobs.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "jobs.h"


static struct job jobs[MAX_JOBS];
static int next_job_id = 1;


static struct job *
find_job_by_id(int id)
{
	for (int i = 0; i < MAX_JOBS; i++)
		if (jobs[i].id == id)
			return &jobs[i];
	return NULL;
}

#ifdef USE_FIND_JOB_BY_PGID
static struct job *
find_job_by_pgid(pid_t pgid)
{
	for (int i = 0; i < MAX_JOBS; i++)
		if (jobs[i].id != 0 && jobs[i].pgid == pgid)
			return &jobs[i];
	return NULL;
}
#endif

static int
parse_job_id(char *arg)
{
	if (!arg) return -1;

	if (arg[0] == '%')
		return atoi(arg + 1);

	return atoi(arg);
}

void
init_job_slot()
{
	for (int i = 0; i < MAX_JOBS; i++)
		jobs[i].id = 0;
}

int
alloc_job_slot()
{
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].id == 0)
            return i;
    return -1;
}

void
set_up_job(int pgid, int slot, char *cmdline)
{
	jobs[slot].id = next_job_id++;
	jobs[slot].pgid = pgid;
	jobs[slot].cmdline = strdup(cmdline);
	jobs[slot].state = JOB_RUNNING;
}

void
check_job(int pid, int status)
{
	// find job by pgid
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i].id == 0) {
			continue;
		}
		if (jobs[i].pgid == pid || getpgid(pid) == jobs[i].pgid) {
			if (WIFSTOPPED(status))
				jobs[i].state = JOB_STOPPED;
			else if (WIFCONTINUED(status))
				jobs[i].state = JOB_RUNNING;
			else if (WIFEXITED(status) || WIFSIGNALED(status)) {
				jobs[i].state = JOB_DONE;
			}
			break;
		}
	}
}

void
list_jobs()
{
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i].id == 0)
			continue;

		const char *state =
			(jobs[i].state == JOB_RUNNING) ? "Running" :
			(jobs[i].state == JOB_STOPPED) ? "Stopped" :
			"Done";
		printf("[%d]  %-8s  %s\n", jobs[i].id, state, jobs[i].cmdline);

		if (jobs[i].state == JOB_DONE) {
			// display it only once
			free(jobs[i].cmdline);
			jobs[i].cmdline = NULL;
			jobs[i].id = 0;
		}
	}
}

int
job_fg(char *argv[])
{
	int id = parse_job_id(argv[1]);
	if (id <= 0) {
		fprintf(stderr, "fg: usage: fg %%job\n");
		return 1;
	}

	struct job *j = find_job_by_id(id);
	if (!j) {
		fprintf(stderr, "fg: job %d not found\n", id);
		return 1;
	}

	// Move job to foreground
	tcsetpgrp(STDIN_FILENO, j->pgid);

	// Continue if stopped
	if (j->state == JOB_STOPPED)
		kill(-j->pgid, SIGCONT);

	j->state = JOB_RUNNING;

	// Wait for job to finish or stop again
	int status;
	waitpid(-j->pgid, &status, WUNTRACED);

	if (WIFSTOPPED(status))
		j->state = JOB_STOPPED;
	else {
		j->state = JOB_DONE;
		j->id = 0; // remove job
	}

	// Return terminal to shell
	tcsetpgrp(STDIN_FILENO, getpid());
	return 0;
}

int job_bg(char *argv[])
{
	int id = parse_job_id(argv[1]);
	if (id <= 0) {
		fprintf(stderr, "bg: usage: bg %%job\n");
		return 1;
	}

	struct job *j = find_job_by_id(id);
	if (!j) {
		fprintf(stderr, "bg: job %d not found\n", id);
		return 1;
	}

	if (j->state == JOB_RUNNING) {
		printf("bg: job %d already running\n", id);
		return 0;
	}

	// Resume job in background
	kill(-j->pgid, SIGCONT);
	j->state = JOB_RUNNING;

	printf("[%d] %s &\n", j->id, j->cmdline);
	return 0;
}

// vim: set ts=4 sw=4 ai noet:
