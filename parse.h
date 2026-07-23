// parse.h

#ifndef _PARSE_H_
#define _PARSE_H_

struct redirs {
    char *stdout_file;
    bool  stdout_append;
    char *stderr_file;
    bool  stderr_append;
};

struct env_dict {
    char *name;
    char *value;
};

struct cmdinfo {
    char **argv;
    int argc;
    struct redirs r;
    bool background;        // true if command ends with &
    struct env_dict *envs;  // any environments specified
    int env_count;
};

struct pipeline {
    struct cmdinfo *cmds;   // array of commands
    int count;              // number of segments
};

extern struct pipeline parse_command(const char *cmdline);

#endif
