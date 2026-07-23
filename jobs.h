// jobs.h

#define MAX_JOBS 128

typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } job_state;

struct job {
	int id;
	pid_t pgid;
	char *cmdline;
	job_state state;
};

extern int alloc_job_slot();
extern void init_job_slot();
extern int alloc_job_slot();
extern void set_up_job(int pgid, int slot, char *cmdline);
extern void check_job(int pid, int status);
extern void list_jobs();
extern int job_bg(char *argv[]);
extern int job_fg(char *argv[]);

// vim: set ts=4 sw=4 ai noet:
