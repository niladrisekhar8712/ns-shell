#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define __GNU_SOURCE
#define TOK_DELIM " \t\r\n"
#define RED "\033[0;31m"
#define RESET "\e[0m"
#define TK_BUFF_SIZE 1024

char *read_line();

char **split_line(char *, int *);

int ns_shell_exit(char **);

int ns_shell_execute(char **, int);

void errExit(char *msg);

void errExit(char *msg) {
    fprintf(stderr, msg);
    exit(EXIT_FAILURE);
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
                ssize_t bytes_read = read(pipe_fd[0],temp, 1024);
                temp[bytes_read] = '\0';

                int flags = O_RDWR | O_CREAT | O_APPEND;
                if (strcmp(args[argc - 2], ">") == 0) {
                    flags = O_RDWR | O_CREAT | O_TRUNC;
                }


                int fd = open(args[argc-1], flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
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

char **split_line(char *line, int *argc) {
    int buffsize = TK_BUFF_SIZE;
    int positions = 0;
    char **tokens = malloc(buffsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }
    token = strtok(line, TOK_DELIM);
    while (token != NULL) {
        tokens[positions] = token;
        positions++;

        if (positions >= buffsize) {
            buffsize += TK_BUFF_SIZE;
            tokens = realloc(tokens, buffsize * sizeof(char *));

            if (!tokens) {
                fprintf(stderr, "%sns_shell: Allocation error%s\n", RED, RESET);
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, TOK_DELIM);
    }
    tokens[positions] = NULL;
    *argc = positions;
    return tokens;
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
        printf("> ");
        int argc = 0;
        char *line = read_line();

        char **args = split_line(line, &argc);
        if (argc == 0) continue;

        status = ns_shell_execute(args, argc);
        free(line);
        free(args);
    } while (status);
}

int main() {
    loop();
    return 0;
}
