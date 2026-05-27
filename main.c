#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "linked_list.h"

#define _GNU_SOURCE
#define TOK_DELIM "|\n"
#define RED "\033[0;31m"
#define RESET "\e[0m"
#define TK_BUFF_SIZE 1024

char *read_line();

char **split_line(char *, int *);

void split_command(char *, Node **);

int ns_shell_exit(char **);

int ns_shell_execute(char **, int);

int ns_shell_execute_pipeline(Node *);

void errExit(char *msg);

void errExit(char *msg) {
    fprintf(stderr, msg);
    exit(EXIT_FAILURE);
}
// 5b. when chaining of one or more commands is received
int ns_shell_execute_pipeline(Node *head) {
    Node *current = head;

    // we need pipe for IPC
    int pipe_fd[2];
    int fd_in = 0;

    int status;
    // run through the linked list
    while (current != NULL) {
        // do not create pipe for the last command in chain
        if (current->next != NULL) {
            if (pipe(pipe_fd) == -1) {
                errExit("Pipeline: pipe creating error" RED);
            }
        }
        pid_t cpid;

        switch (cpid = fork()) {
            case 0:
                // if the command is not the first command in chain, take input from previous commands(fd_in)
                if (fd_in != 0) {
                    if (dup2(fd_in, STDIN_FILENO) == -1) {
                        errExit("Fd duplication error\n" RED);
                    }
                    if (close(fd_in) == -1) {
                        errExit("Fd closing error\n"RED);
                    }
                }
                // if the command is not the last command in chain, plug the stdout of the current command to the pipe
                if (current->next != NULL) {
                    if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
                        errExit("Fd duplication error\n"RED);
                    }
                    if (close(pipe_fd[0]) == -1) {
                        errExit("Fd closing error\n");
                    }
                    if (close(pipe_fd[1]) == -1) {
                        errExit("Fd closing error\n");
                    }
                }

                if (execvp(current->data[0], current->data) == -1) {
                    errExit("Command execution failed\n" RED);
                }
                exit(EXIT_FAILURE);
            case -1:
                errExit("Fork failed\n"RED);
            default:
                if (fd_in != 0) {
                    if (close(fd_in) == -1) {
                        errExit("Fd closing error\n"RED);
                    }
                }
                // prepare fd_in for the next command
                if (current->next != NULL) {
                    fd_in = pipe_fd[0];
                    if (close(pipe_fd[1]) == -1) {
                        errExit("Fd closing error\n"RED);
                    }
                }

                current = current->next;
        }
    }

    while (wait(&status) > 0);

    return status;
}

// 5a. If the user entered only one commands this function will execute
// In this case file redirection is allowed
int ns_shell_execute(char **args, int argc) {
    int status;
    bool is_redirected = false; // assume no file redirection
    if (strcmp(args[0], "exit") == 0) { // if the command is "exit" then exit
        return ns_shell_exit(args);
    }
    // if ">>" or ">" is present then only redirection is performed
    is_redirected = argc >= 3 && ((strcmp(args[argc - 2], ">>") == 0) || (strcmp(args[argc - 2], ">") == 0));

    //pipe is required on redirection
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        errExit("Piping failed\n"RED);
    }
    /*How pipe() system call works?
     * -> the pipe() syscall takes pipe_fd[2] as a parameter
     * -> in that fd pipe_fd[0] is the read end and pipe_fd[1] is the write end
     * -> On success the pipe(), the kernel creates a temporary buffer in memory
     */

    /* How dup2() system call works?
     * -> dup2(oldfd, newfd) means it copies the oldfd and overwrites the newfd
     */

    pid_t cpid;

    switch (cpid = fork()) {
        case 0:
            close(pipe_fd[0]);
            // if redirected, unplug the command's slot1(stdout) to buffer
            // when later execvp() is called, that process writes output to slot1 but instead of output showing up on screen, it is redirected to file
            if (is_redirected && dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
                errExit("File descriptor error"RED);
            }
            close(pipe_fd[1]);
            if (is_redirected) {
                args[argc - 2] = NULL;
            }
            if (execvp(args[0], args) < 0)
                printf("ns_shell: command not found: %s\n", args[0]);
            exit(EXIT_FAILURE);


        case -1:
            printf(RED "Error forking" RESET "\n");
            break;


        default:
            if (close(pipe_fd[1]) == -1) {
                errExit("Fd closing error 1"RED);
            }
            char temp[1024];
            // on redirection read from buffer (pipe) and write to the specified file
            if (is_redirected) {
                ssize_t bytes_read = read(pipe_fd[0], temp, 1024);
                temp[bytes_read] = '\0';

                int flags = O_RDWR | O_CREAT | O_APPEND;
                if (strcmp(args[argc - 2], ">") == 0) {
                    flags = O_RDWR | O_CREAT | O_TRUNC;
                }


                int fd = open(args[argc - 1], flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
                if (fd == -1) {
                    errExit("File opening failed\n");
                }
                if (write(fd, temp, strlen(temp)) == -1) {
                    errExit("Write error\n");
                }
            }
            if (close(pipe_fd[0]) == -1) {
                errExit("Fd closing error 2"RED);
            }

            // wait for the child process to terminate
            waitpid(cpid, &status, WUNTRACED);
    }
    return 1;
}

int ns_shell_exit(char **args) {
    return 0;
}
// 4. Split a command given by the user into arguments and push that into the linked list
// node(arguments, no_of_args)
void split_command(char *command, Node **head) {
    int buffsize = TK_BUFF_SIZE;
    int positions = 0;
    char **tokens = malloc(buffsize * sizeof(char *));

    if (!tokens) {
        fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    char *p = command;

    while (*p) {
        // Skip leading whitespace
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break; // End of string reached

        char *token_start = p;
        int in_quote = 0;
        char *write_ptr = p; // We use this to overwrite the string and remove quotes

        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote; // Toggle quote state ON/OFF
                p++; // Skip over the quote character
            } else if (!in_quote && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
                p++; // Skip the space
                break; // Space OUTSIDE quotes means end of token
            } else {
                *write_ptr++ = *p++; // Keep the character and move forward
            }
        }

        *write_ptr = '\0';
        tokens[positions] = token_start;
        positions++;
        if (positions >= buffsize) {
            buffsize += TK_BUFF_SIZE;
            tokens = realloc(tokens, buffsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
                exit(EXIT_FAILURE);
            }
        }
    }

    tokens[positions] = NULL;
    insert_at_end(head, tokens, positions);
}
// 3. splits the line with delimiter |
// This function splits the user input into one or more commands
char **split_line(char *line, int *argc) {
    int buffsize = TK_BUFF_SIZE;
    int positions = 0;
    char **commands = malloc(buffsize * sizeof(char *));
    char *command;


    if (!commands) {
        fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }
    command = strtok(line, TOK_DELIM);
    while (command != NULL) {
        commands[positions] = command;
        positions++;

        if (positions >= buffsize) {
            buffsize += TK_BUFF_SIZE;
            commands = realloc(commands, buffsize * sizeof(char *));

            if (!commands) {
                fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
                exit(EXIT_FAILURE);
            }
        }
        command = strtok(NULL, TOK_DELIM);
    }
    commands[positions] = NULL;
    *argc = positions;

    return commands;
}
// 2. Reads the user input
char *read_line() {
    // Allocate memory for user input
    int buffsize = 1024;
    int position = 0;
    char *buffer = malloc(sizeof(char) * buffsize);
    int c;

    if (!buffer) {
        fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    while (1) {
        c = getchar();
        // terminates the string in buffer on receiving EOF or enter
        if (c == EOF || c == '\n') {
            buffer[position] = '\0';
            return buffer;
        }
        buffer[position] = c;
        position++;

        if (position >= buffsize) {
            buffsize += 1024;
            buffer = realloc(buffer, buffsize);

            if (!buffer) {
                fprintf(stderr, "ns_shell: Allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}
// 1. Shell loops continuously waits for user input
void loop() {
    int status = 1;

    do {
        printf("ns_shell$> ");
        int no_of_commands = 0;
        char *line = read_line(); // reads the input given by the user
        // Now, the user input can contain multiple commands by piping
        char **commands = split_line(line, &no_of_commands);
        // creates a singly connected linkedlist of chained commands along with no_of_args per commands
        Node *head = NULL;
        for (int i = 0; i < no_of_commands; i++) {
            split_command(commands[i], &head);
        }
        // case when the user just entered without any commands
        if (no_of_commands == 0) {
            free(line);
            free(commands);
            continue;
        }
        // case without piping, eg "ls -l", "pwd" etc.
        if (no_of_commands == 1)
            status = ns_shell_execute(head->data, head->argc);
        else { // case with piping eg "ls -l | grep -F "CMake"
            status = ns_shell_execute_pipeline(head);
        }
        // now this command or chain of commands has been executed, free resources
        free_list(head);
        free(line);
        free(commands);
    } while (status);
}

int main() {
    loop();
    return 0;
}
