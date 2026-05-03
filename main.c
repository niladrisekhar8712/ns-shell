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

int ns_shell_execute_pipeline(Node *head) {
    Node *current = head;


    int pipe_fd[2];
    int fd_in = 0;

    int status;

    while (current != NULL) {
        if (current->next != NULL) {
            if (pipe(pipe_fd) == -1) {
                errExit("Pipeline: pipe creating error" RED);
            }
        }
        pid_t cpid;

        switch (cpid = fork()) {
            case 0:
                if (fd_in != 0) {
                    if (dup2(fd_in, STDIN_FILENO) == -1) {
                        errExit("Fd duplication error\n" RED);
                    }
                    if (close(fd_in) == -1) {
                        errExit("Fd closing error\n"RED);
                    }
                }

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


int ns_shell_execute(char **args, int argc) {
    int status;
    bool is_redirected = false;
    if (strcmp(args[0], "exit") == 0) {
        return ns_shell_exit(args);
    }

    is_redirected = argc >= 3 && ((strcmp(args[argc - 2], ">>") == 0) || (strcmp(args[argc - 2], ">") == 0));

    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        errExit("Piping failed\n"RED);
    }


    pid_t cpid;

    switch (cpid = fork()) {
        case 0:
            close(pipe_fd[0]);
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


            waitpid(cpid, &status, WUNTRACED);
    }
    return 1;
}

int ns_shell_exit(char **args) {
    return 0;
}

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

char *read_line() {
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

void loop() {
    int status = 1;

    do {
        printf("ns_shell$> ");
        int no_of_commands = 0;
        char *line = read_line();
        char **commands = split_line(line, &no_of_commands);
        Node *head = NULL;
        for (int i = 0; i < no_of_commands; i++) {
            split_command(commands[i], &head);
        }
        if (no_of_commands == 0) {
            free(line);
            free(commands);
            continue;
        }
        if (no_of_commands == 1)
            status = ns_shell_execute(head->data, head->argc);
        else {
            status = ns_shell_execute_pipeline(head);
        }
        free_list(head);
        free(line);
        free(commands);
    } while (status);
}

int main() {
    loop();
    return 0;
}
