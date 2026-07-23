// commands.h

#ifndef _COMMANDS_H_
#define _COMMANDS_H_
/* A structure which contains information on the commands this program
   can understand. */

typedef int (*IntFunc)(char **);
typedef char *(*CharFunc)(char *, int, int);

typedef struct {
    char *name;		/* User printable name of the function. */
    IntFunc func;		/* Function to call to do the job. */
    char *doc;		/* Documentation for this function.  */
} COMMAND;

/* The names of functions that actually do the manipulation */
int com_bg(char *argv[]);
int com_cd(char *argv[]);
int com_echo(char *argv[]);
int com_fg(char *argv[]);
int com_help(char *argv[]);
int com_history(char *argv[]);
int com_jobs(char *argv[]);
int com_list(char *argv[]);
int com_pwd(char *argv[]);
int com_quit(char *argv[]);
#ifdef USE_COM_STAT
int com_stat(char *argv[]);
#endif
int com_view(char *argv[]);

#ifdef _TSH_C_
COMMAND commands[] = {
	{ "?",      com_help,   "Synonym for 'help'" },
	{ "bg",     com_bg,     "Place the current process to background" },
	{ "cd",     com_cd,     "Change to directory DIR" },
	{ "chdir",  com_cd,     "Synonym for 'cd'" },
	{ "echo",   com_echo,   "Print anything follows it" },
	{ "exit",   com_quit,   "Exit from tsh" },
	{ "fg",     com_fg,     "Bring a background to foreground" },
	{ "help",   com_help,   "Display this text" },
	{ "history",com_history,"Display command history" },
	{ "jobs",   com_jobs,   "List background jobs" },
	{ "list",   com_list,   "List files in DIR" },
	{ "pwd",    com_pwd,    "Print the current working directory" },
	{ "quit",   com_quit,   "Quit using tsh" },
#ifdef USE_COM_STAT
	{ "stat",   com_stat,   "Print out statistics on FILE" },
#endif
	{ "view",   com_view,   "View the contents of FILE" },
	{ (char *) NULL, (IntFunc) NULL, (char *) NULL }
};
#else
extern COMMAND commands[];
#endif //_TSH_C_

#endif //_COMMANDS_H_
// vim: set ts=4 sw=4 ai noet:
