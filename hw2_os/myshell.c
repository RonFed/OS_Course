#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PRINT_ERROR() fprintf(stderr, "%s \n", strerror(errno))


typedef enum {
    FOREGROUND,
    BACKGROUND,
    PIPE,
    REDIRECT
} command_type_e;

typedef struct {
    command_type_e type;
    int shell_symbol_loc;
} command_dscr_t;


int prepare(void) {
    return 0;
}

static command_dscr_t find_command_type(int count, char ** arglist) {
    command_dscr_t command;
    for (size_t i = 0; i < count; i++)
    {
        if (arglist[i][0] == '|')
        {
            command.shell_symbol_loc = i;
            command.type = PIPE;
            return command;
        }
        if (arglist[i][0] == '>')
        {
            command.shell_symbol_loc = i;
            command.type = REDIRECT;
            return command;
        }
        if (arglist[i][0] == '&')
        {
            command.shell_symbol_loc = i;
            command.type = BACKGROUND;
            return command;
        }
    }
    command.shell_symbol_loc = -1;
    command.type = FOREGROUND;
    return command;
}

static int handle_foreground(char** arglist) {

    // printf("shell pid is %d \n", getpid());
    int pid = fork();
    if (pid == -1) {
        PRINT_ERROR();
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Child process : execute the desired command*/
        if (execvp(arglist[0], arglist) == -1) {
            PRINT_ERROR();
            exit(EXIT_FAILURE);
        }
    } else {
        /* Parent process wait for the foreground process to finish */
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status)) {
            PRINT_ERROR();
        }
    }

    /* THIS NEEDS TO CHANGE */
    return 0;
}

static int handle_background(int count, char** arglist) {
    arglist = (char **) realloc(arglist, count * sizeof(char*));
    int pid = fork();
    if (pid == -1) {
        PRINT_ERROR();
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Child process : execute the desired command*/
        if (execvp(arglist[0], arglist) == -1) {
            PRINT_ERROR();
            exit(EXIT_FAILURE);
        }
    }
    /* Parent will continue working on other stuff */
    return 0;
}

static int handle_pipe() {
      /* THIS NEEDS TO CHANGE */
    return 0;
}

static int handle_redirect() {
      /* THIS NEEDS TO CHANGE */
    return 0;
}

int process_arglist(int count, char** arglist) {
    command_dscr_t command = find_command_type(count, arglist);
    int result;
    
    switch (command.type) {
        case FOREGROUND:
            result = handle_foreground(arglist);
            break;
        case BACKGROUND:
            result = handle_background(count, arglist);
            break;
        case PIPE:
            /* code */
            break;
        case REDIRECT:
            /* code */
            break;
        default:
            break;
    }
    
    // return result;
    return 1;
}


int finalize(void) {
    return 0;
}