/*
 * tclXshell.c --
 *
 * Support code for the Extended Tcl shell.
 *-----------------------------------------------------------------------------
 * Copyright 1991-1993 Karl Lehenbauer and Mark Diekhans.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies.  Karl Lehenbauer and
 * Mark Diekhans make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *-----------------------------------------------------------------------------
 * $Id: tclXstartup.c,v 2.18 1993/09/05 02:01:59 markd Exp markd $
 *-----------------------------------------------------------------------------
 */

#include "tclExtdInt.h"

extern char *optarg;
extern int   optind, opterr;

static char *TCLXENV = "TCLXENV";


/*
 *-----------------------------------------------------------------------------
 *
 * TclX_ParseCmdLine --
 *
 *   Parse the command line for the TclX shell ("tcl") and similar programs.
 * This sets Tcl variables and returns, no other action is taken at this
 * time.  The following Tcl variables are initialized by this routine:
 *
 *   o argv0 -  The name of the Tcl program specified on the command line or
 *     the name that the Tcl shell was invoked under if no program was
 *     specified.
 *   o argc - Contains a count of the number of argv arguments (0 if none).
 *   o argv- A list containing the arguments passed in from the command line,
 *     excluding arguments used by the Tcl shell.  The first element is the
 *     first passed argument, not the program name.
 *   o tcl_interactive - Set to 1 if Tcl shell is invoked interactively, or
 *     0 if the Tcl shell is directly executing a script.
 *   o TCLXENV(evalCmd) - Command to eval, as specified by the -c flag.
 *   o TCLXENV(evalFile) - File specified on the command to evaluate rather
 *     than go interactive.
 *   o TCLXENV(quick) - If defined, the -q for quick startup flag was
 *     specified.
 *   o TCLXENV(noDump) - If defined, the -n for no stack dump on error flag
 *     was specified.
 *
 * This function should be called before any application or package specific
 * initialization.  It aborts if an error occurs processing the command line.
 *
 * Parameters:
 *   o interp - A pointer to the interpreter.
 *   o argc, argv - Arguments passed to main for the command line.
 * Notes:
 *   The variables tclAppName, tclAppLongName, tclAppVersion must be set
 * before calling thus routine if special values are desired.
 *-----------------------------------------------------------------------------
 */
void
TclX_ParseCmdLine (interp, argc, argv)
    Tcl_Interp   *interp;
    int           argc;
    char        **argv;
{
    char  *scanPtr, *tclArgv, *errorStack, numBuf [32];
    int    option;
    char  *evalFile = NULL;
    char  *evalCmd  = NULL;
    int    quick    = FALSE;
    int    noDump   = FALSE;

    /*
     * Scan arguments looking for flags to process here rather than to pass
     * on to the scripts.  The '-c' or '-f' must also be the last option to
     * allow for script arguments starting with `-'.
     */
    while ((option = getopt (argc, argv, "qc:f:un")) != -1) {
        switch (option) {
          case 'q':
            if (quick)
                goto usageError;
            quick = TRUE;
            break;
          case 'n':
            if (noDump)
                goto usageError;
            noDump = TRUE;
            break;
          case 'c':
            evalCmd = optarg;
            goto exitParse;
          case 'f':
            evalFile = optarg;
            goto exitParse;
          case 'u':
          default:
            goto usageError;
        }
    }
  exitParse:
  
    /*
     * If neither `-c' nor `-f' were specified and at least one parameter
     * is supplied, then if is the file to execute.  The rest of the arguments
     * are passed to the script.  Check for '--' as the last option, this also
     * is a terminator for the file to execute.
     */
    if ((evalCmd == NULL) && (evalFile == NULL) && (optind != argc) &&
        !STREQU (argv [optind-1], "--")) {
        evalFile = argv [optind];
        optind++;
    }

    /*
     * Set the Tcl argv0, argv & argc variables.
     */
    if (Tcl_SetVar (interp, "argv0",
                    (evalFile != NULL) ? evalFile : argv [0],
                    TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
        goto tclError;

    tclArgv = Tcl_Merge (argc - optind,  &argv [optind]);
    if (Tcl_SetVar (interp, "argv", tclArgv,
                    TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
        goto tclError;
    ckfree (tclArgv);

    sprintf (numBuf, "%d", argc - optind);
    if (Tcl_SetVar (interp, "argc", numBuf, 
                    TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
        goto tclError;

    /*
     * Set the interactive flag, based on what we have parsed.
     */
    if (Tcl_SetVar (interp, "tcl_interactive", 
                    ((evalCmd == NULL) && (evalFile == NULL)) ? "1" : "0",
                    TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
        goto tclError;

    /*
     * Set elements in the TCLXENV array.
     */
    if (evalCmd != NULL) {
        if (Tcl_SetVar2 (interp, TCLXENV, "evalCmd", evalCmd,
                         TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
            goto tclError;
    }
    if (evalFile != NULL) {
        if (Tcl_SetVar2 (interp, TCLXENV, "evalFile", evalFile,
                         TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
            goto tclError;
    }
    if (quick) {
        if (Tcl_SetVar2 (interp, TCLXENV, "quick", "1",
                         TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
            goto tclError;
    }
    if (noDump) {
        if (Tcl_SetVar2 (interp, TCLXENV, "noDump", "1",
                         TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL)
            goto tclError;
    }
    return;

  usageError:
    fprintf (stderr, "usage: %s %s\n", argv [0],
             "?-qun? ?-f? ?script?|?-c command? ?args?");
    exit (1);

  tclError:
    TclX_ErrorExit (interp, 255);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TclX_RunShell --
 *
 *   This function runs the TclX shell once all apllication and package
 * initialization has taken place.  It either enters interactive command mode
 * or evaulates a script or command from the command line.
 * TclX_ParseCmdLine must have been called.
 *
 * Parameters:
 *   o interp  (I) - A pointer to the interpreter.
 * Returns:
 *   The result of the command being evaulated.  Check for TCL_ERROR and call
 * TclX_ErrorExit if an error occured after any cleanup you want to do.
 *-----------------------------------------------------------------------------
 */
int
TclX_RunShell (interp)
    Tcl_Interp  *interp;
{
    char  *evalStr;

    /*
     * Evaluate either a command or file if it was specified on the command
     * line.
     */
    evalStr = Tcl_GetVar2 (interp, TCLXENV, "evalCmd", TCL_GLOBAL_ONLY);
    if (evalStr != NULL) {
        return Tcl_Eval (interp, evalStr);
    }

    evalStr = Tcl_GetVar2 (interp, TCLXENV, "evalFile", TCL_GLOBAL_ONLY);
    if (evalStr != NULL) {
        return Tcl_EvalFile (interp, evalStr);
    }
    
    /*
     * Otherwise, enter an interactive command loop.  Setup SIGINT handling
     * so user may interrupt with out killing program.
     */
    Tcl_SetupSigInt ();
    Tcl_CommandLoop (interp, isatty (0));

    return TCL_OK;
}
