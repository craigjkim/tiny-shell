/*
 * tsh.h
 */

#ifndef _TSH_H_
#define _TSH_H_

#ifdef _TSH_C_
# define _EXTERN_
#else
# define _EXTERN_ extern
#endif	// _TSH_C_

_EXTERN_ bool  Done;		// Is the user done?
_EXTERN_ char *HistFile;	// command history file
_EXTERN_ char  PrevCmd[PATH_MAX];	// previous history
_EXTERN_ char *ProgName;	// The name of this program, as taken from argv[0]

#undef _EXTERN_

#endif	// _TSH_H_

// vim: set ts=4 sw=4 ai noet:
