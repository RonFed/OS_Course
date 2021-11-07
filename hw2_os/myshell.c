#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PRINT_ERROR()   fprintf(stderr, "%s \n", strerror(errno))
#define STDIN_FD        (0)
#define STDOUT_FD       (1)

static volatile int is_foreground;
static pid_t current_fg_id;

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

void sigchld_handler(int signum, siginfo_t * info, void * unused) {
    printf("hello \n");

    if (is_foreground && info->si_pid == current_fg_id) {
        is_foreground = 0;
    }

    int status;
    int pid = wait(&status);
    if (!WIFEXITED(status) && errno != ECHILD) {
        PRINT_ERROR();
    }
    return;
}

int prepare(void) {
    is_foreground = 0;
    current_fg_id = 0;

    struct sigaction sigchld_action;
    memset(&sigchld_action, 0, sizeof(sigchld_action));

    sigchld_action.sa_sigaction = sigchld_handler;
    sigchld_action.sa_flags = SA_RESTART | SA_SIGINFO;
    
    return sigaction(SIGCHLD, &sigchld_action, NULL);
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
        is_foreground = 1;
        current_fg_id = pid;
        while (is_foreground) {}
        
        // if (WEXITSTATUS(status) != 0) {
        //     PRINT_ERROR();
        // }
    }

    /* THIS NEEDS TO CHANGE */
    return 0;
}

static int handle_background(int count, char** arglist) {
    /* remove the '&' from arglist */
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
  


    /* THIS NEEDS TO CHANGE */
    return 0;
}

static int handle_pipe(int count, int pipe_loc, char** arglist) {
    int   pipefd[2];

    if (pipe(pipefd) == -1){
        PRINT_ERROR();
        exit(EXIT_FAILURE);
    }
    
    int pid1 = fork();
    if (pid1 == -1) {
        PRINT_ERROR();
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        /* Child1 process (the writer) */
        arglist[pipe_loc] = NULL;
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FD) == -1) {
            PRINT_ERROR();
            exit(EXIT_FAILURE);
        }

        if (execvp(arglist[0], arglist) == -1) {
            PRINT_ERROR();
            exit(EXIT_FAILURE);
        }
    } else {
        /* Parent process */
        int pid2 = fork();

        if (pid2 == -1) {
            PRINT_ERROR();
            exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
            /* Child2 process (the reader) */
            arglist += (pipe_loc + 1);
            /* Close the write end-point */
            close(pipefd[1]);

            if (dup2(pipefd[0], STDIN_FD) == -1) {
                PRINT_ERROR();
                exit(EXIT_FAILURE);
            }

            if (execvp(arglist[0], arglist) == -1) {
                PRINT_ERROR();
                exit(EXIT_FAILURE);
            }  
        } else {
            /* Parent process wait for 2 of it's children to finish*/
            /* Close the parent's read/wrtie to pipe options to aloow the children to finish*/
            close(pipefd[0]);
            close(pipefd[1]);
            int status;
            do
            {
               if (wait(&status) == -1 && errno != ECHILD)
               {
                   PRINT_ERROR();
                   exit(EXIT_FAILURE);
               }
            } while (errno != ECHILD);
        }   
    }
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
            result = handle_pipe(count, command.shell_symbol_loc, arglist);
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