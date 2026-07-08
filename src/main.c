#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>

int find_path(const *char arg, char *full_path) {
  char *path = strdup(getenv("PATH"));
  char *dir = strtok(path, ":");
  char full_path[PATH_MAX];
  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
    if (access(full_path, X_OK) == 0) {
      break;
    }
    dir = strtok(NULL, ":"); 
  }
  free(path);
  if (dir == NULL) {
    return 1;
  } else {
    return 0;
  }
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  char input[1024];

  while (1) {
    printf("$ ");
    fgets(input, sizeof(input), stdin);
    input[strlen(input) - 1] = '\0';
    if (strcmp(input, "exit") == 0) { break; }
    else if (strncmp(input, "echo ", 5) == 0) { printf("%s\n", input + 5); }
    else if (strncmp(input, "type ", 5) == 0) {
      char *arg = input + 5;
      if (!strcmp(arg, "echo") || !strcmp(arg, "exit") || !strcmp(arg, "type")) {
        printf("%s is a shell builtin\n", arg);
      } else {
        char *path = strdup(getenv("PATH"));
        char *dir = strtok(path, ":");
        char full_path[PATH_MAX];
        while (dir != NULL) {
          snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
          if (access(full_path, X_OK) == 0) {
            printf("%s is %s\n", arg, full_path);
            break;
          }
          dir = strtok(NULL, ":"); 
        }
        if (dir == NULL) {
          printf("%s: not found\n", arg);
        }
        free(path);
      }
    } else { 
            char input_copy[1024];
            strcpy(input_copy, input);

            char *args[64];
            int i = 0;
            args[i] = strtok(input_copy, " ");
            while (args[i] != NULL && i < 63) {
                i++;
                args[i] = strtok(NULL, " ");
            }

            if (args[0] == NULL) { continue; }

            char *path = strdup(getenv("PATH"));
            char *dir = strtok(path, ":");
            char full_path[PATH_MAX];

            while (dir != NULL) {
                snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
                if (access(full_path, X_OK) == 0) {
                    pid_t pid = fork();
                    if (pid == 0) {
                        execv(full_path, args);
                        perror("Execution failed");
                        exit(EXIT_FAILURE);
                    } else {
                        waitpid(pid, NULL, 0);
                    }
                    break;
                }
                dir = strtok(NULL, ":"); 
            }

            if (dir == NULL) {
                printf("%s: command not found\n", args[0]);
            }
            free(path);
        } 
    }

    return 0;
}
