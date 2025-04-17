#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>   // for open()
#include <unistd.h>  // for dup2(), close()
#include <signal.h>


#define MAX_LINE 1024
#define MAX_TOKENS 100

pid_t child_pid = -1; // -1 means there's no child process


char** shell_paths = NULL;
int path_count = 0;

void free_paths() {
    for (int i = 0; i < path_count; i++) {
        free(shell_paths[i]);
    }
    free(shell_paths);
    shell_paths = NULL;
    path_count = 0;
}



void print_error() {
    char error_message[] = "An error has occured\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

void set_paths(char** args) {
    free_paths(); // clear old paths

    // Count new paths
    int count = 0;
    for (int i = 1; args[i] != NULL; i++) {
        count++;
    }

    // Allocate memory
    shell_paths = malloc(sizeof(char*) * count);
    if (!shell_paths) {
        print_error();
        return;
    }

    for (int i = 0; i < count; i++) {
        shell_paths[i] = strdup(args[i + 1]);
        if (!shell_paths[i]) {
            print_error();
            return;
        }
    }

    path_count = count;
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        write(STDOUT_FILENO, "\n", 1);
        if (child_pid > 0) {
            kill(child_pid, SIGINT);
        } else {
            write(STDOUT_FILENO, "cmpsh> ", 7);
            fflush(stdout);
        }
    } else if (sig == SIGTSTP) {
        write(STDOUT_FILENO, "\n", 1);
        if (child_pid > 0) {
            kill(child_pid, SIGTSTP);
        } else {
            write(STDOUT_FILENO, "cmpsh> ", 7);
            fflush(stdout);
        }
    }
}


void execute_command(char** args) {
    if (args[0] == NULL) return; // No commands

    // Check for output redirection
    char* output_file = NULL;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL || args[i + 2] != NULL) {
                print_error(); // error if there's no outputfile "args[i+1] or there are more that one "args[i+2]"
                return;
            }
            output_file = args[i + 1];
            args[i] = NULL;  // Cut args at '>'
            break;
        }
    }

    // Built-in: exit
    if (strcmp(args[0], "exit") == 0) {
        if (output_file != NULL) {
            print_error();
            return;
        }
        exit(0);
    }

    // Built-in: cd
    else if (strcmp(args[0], "cd") == 0) {
        if (output_file != NULL) {
            print_error();
            return;
        }
        if (args[1] == NULL || chdir(args[1]) != 0) {
            print_error();
        }
        return;
    }

    // Built-in: pwd
    else if (strcmp(args[0], "pwd") == 0) {
        if (output_file != NULL) {
            print_error();
            return;
        }
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            print_error();
        }
        return;
    }

    // Built-in: paths
    else if (strcmp(args[0], "paths") == 0) {
        if (output_file != NULL) {
            print_error();
            return;
        }
        set_paths(args);
        return;
    }

    // External commands
    for (int i = 0; i < path_count; i++) {
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", shell_paths[i], args[0]);
    
        if (access(full_path, X_OK) == 0) //check execute permission
        {
            child_pid = fork();
            if (child_pid == 0) {
                if (output_file != NULL) {
                    int fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
                    if (fd < 0) {
                        print_error();
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                execv(full_path, args);
                print_error();
                exit(1);
            }  else if (child_pid > 0) {
                // In parent process
                int status;
                waitpid(child_pid, &status, WUNTRACED);
                child_pid = -1; // Reset after child terminates
                return;
            }else {
                print_error();
                return;
            }
        }
    }

    print_error();
}


void shell_loop(FILE* input_stream) {
    char line[MAX_LINE];
    char* args[MAX_TOKENS];

    while (1) {
        if (input_stream == stdin) {
            printf("cmpsh> ");
        }

        if (!fgets(line, sizeof(line), input_stream)) {
            break; // EOF
        }

        // Tokenize
        int i = 0;
        char* token = strtok(line, " \t\n"); // separate
        while (token != NULL && i < MAX_TOKENS - 1) {
            args[i++] = token;
            token = strtok(NULL, " \t\n");
        }
        args[i] = NULL;

        execute_command(args);
    }
}


int main(int argc, char* argv[]) {
    char* default_path[] = {"paths", "/bin", NULL};
    set_paths(default_path);

    FILE* input_stream = stdin;

    if (argc == 2) {
        input_stream = fopen(argv[1], "r");
        if (input_stream == NULL) {
            print_error();
            exit(1);
        }
    } else if (argc > 2) {
        print_error();
        exit(1);
    }

   


    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);


    shell_loop(input_stream);

    if (input_stream != stdin) {
        fclose(input_stream);
    }

    free_paths();

    return 0;
}
