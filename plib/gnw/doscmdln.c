#include "plib/gnw/doscmdln.h"

#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// 0x4CE0A0
void DOSCmdLineInit(DOSCmdLine* d)
{
    if (d != NULL) {
        d->numArgs = 0;
        d->args = NULL;
    }
}

// 0x4CE0B4
bool DOSCmdLineCreate(DOSCmdLine* d, char* commandLine)
{
    char moduleFileName[MAX_PATH];
    int moduleFileNameLength;
    const char* delim = " \t";
	int arg;
    int argc = 0;

    // Get the number of arguments in command line.
    if (*commandLine != '\0') {
		char* tok;
        char* copy = strdup(commandLine);
        if (copy == NULL) {
            DOSCmdLineDestroy(d);
            return false;
        }

        tok = strtok(copy, delim);
        while (tok != NULL) {
            argc++;
            tok = strtok(NULL, delim);
        }

        free(copy);
    }

    // Make a room for argv[0] - program name.
    argc++;

    d->numArgs = argc;
    d->args = (char**)malloc(sizeof(*d->args) * argc);
    if (d->args == NULL) {
        // NOTE: Uninline.
        return DOSCmdLineFatalError(d);
    }

    for (arg = 0; arg < argc; arg++) {
        d->args[arg] = NULL;
    }

    // Copy program name into argv[0].
    moduleFileNameLength = GetModuleFileNameA(NULL, moduleFileName, MAX_PATH);
    if (moduleFileNameLength == 0) {
        // NOTE: Uninline.
        return DOSCmdLineFatalError(d);
    }

    if (moduleFileNameLength >= MAX_PATH) {
        moduleFileNameLength = MAX_PATH - 1;
    }

    moduleFileName[moduleFileNameLength] = '\0';

    d->args[0] = strdup(moduleFileName);
    if (d->args[0] == NULL) {
        // NOTE: Uninline.
        return DOSCmdLineFatalError(d);
    }

    // Copy arguments from command line into argv.
    if (*commandLine != '\0') {
		int arg;
		char* tok;
        char* copy = strdup(commandLine);
        if (copy == NULL) {
            DOSCmdLineDestroy(d);
            return false;
        }

        arg = 1;

        tok = strtok(copy, delim);
        while (tok != NULL) {
            d->args[arg] = strdup(tok);
            tok = strtok(NULL, delim);
            arg++;
        }

        free(copy);
    }

    return true;
}

// 0x4CE24C
void DOSCmdLineDestroy(DOSCmdLine* d)
{
    if (d->args != NULL) {
		int index;
        // NOTE: Compiled code is slightly different - it decrements argc.
        for (index = 0; index < d->numArgs; index++) {
            if (d->args[index] != NULL) {
                free(d->args[index]);
            }
        }
        free(d->args);
    }

    d->numArgs = 0;
    d->args = NULL;
}

// 0x4CE298
bool DOSCmdLineFatalError(DOSCmdLine* d)
{
    DOSCmdLineDestroy(d);
    return false;
}
