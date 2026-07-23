// history.c

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <readline/history.h>
#include "history.h"


static char *
history_get_line(int index)
{
    HIST_ENTRY **list = history_list();
    if (!list) return NULL;

    int base = history_base;      // usually 1
    int last = history_length;    // number of entries

    if (index < 0)                // negative index: count from end
        index = last + index + 1;

    if (index < base || index >= base + last)
        return NULL;

    return list[index - base]->line;
}

char *
expand_bang(const char *line)
{
	const char *p = line;
	char out[4096];
	int oi = 0;

	while (*p) {
		if (*p != '!') {
			out[oi++] = *p++;
			continue;
		}

		p++; // skip '!'

		// !!   previous command
		if (*p == '!') {
			char *cmd = history_get_line(-1);
			if (!cmd) return NULL;
			if (strcmp(cmd, "!!") == 0)
				return strdup("!!");
			return expand_bang(cmd);
			/*
			strcpy(out + oi, cmd);
			oi += strlen(cmd);
			p++;
			continue;
			*/
		}

		// !number
		if (isdigit((unsigned char)*p)) {
			int num = atoi(p);
			while (isdigit((unsigned char)*p)) p++;

			char *cmd = history_get_line(num);
			if (!cmd) return NULL;

			strcpy(out + oi, cmd);
			oi += strlen(cmd);
			continue;
		}

		// !-number
		if (*p == '-') {
			p++;
			int num = atoi(p);
			while (isdigit((unsigned char)*p)) p++;

			char *cmd = history_get_line(-num);
			if (!cmd) return NULL;

			strcpy(out + oi, cmd);
			oi += strlen(cmd);
			continue;
		}

		// !prefix
		if (isalnum((unsigned char)*p)) {
			char prefix[256];
			int pi = 0;

			while (isalnum((unsigned char)*p) || *p == '_')
				prefix[pi++] = *p++;
			prefix[pi] = '\0';

			// search backwards
			HIST_ENTRY **list = history_list();
			int last = history_length;
			// int base = history_base;

			char *match = NULL;
			for (int i = last - 1; i >= 0; i--) {
				if (strncmp(list[i]->line, prefix, strlen(prefix)) == 0) {
					match = list[i]->line;
					break;
				}
			}

			if (!match) return NULL;

			strcpy(out + oi, match);
			oi += strlen(match);
			continue;
		}

		// !?substring?
		if (*p == '?') {
			p++;
			char sub[256];
			int si = 0;

			while (*p && *p != '?' && si < 255)
				sub[si++] = *p++;
			sub[si] = '\0';

			if (*p == '?') p++;

			HIST_ENTRY **list = history_list();
			int last = history_length;

			char *match = NULL;
			for (int i = last - 1; i >= 0; i--) {
				if (strstr(list[i]->line, sub)) {
					match = list[i]->line;
					break;
				}
			}

			if (!match) return NULL;

			strcpy(out + oi, match);
			oi += strlen(match);
			continue;
		}

		// literal '!' if nothing matched
		out[oi++] = '!';
	}

	out[oi] = '\0';
	return strdup(out);
}

// vim: set ts=4 sw=4 ai noet:
