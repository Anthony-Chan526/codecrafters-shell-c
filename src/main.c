#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

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
      }
    } else {
        char *path = strdup(getenv("PATH"));
        char *dir = strtok(path, ":");
        char full_path[PATH_MAX];
        while (dir != NULL) {
          snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
          if (access(full_path, X_OK) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
              execvp(full_path, arg);
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
          printf("%s: not found\n", arg);
        }
      } 
    } else { printf("%s: command not found\n", input); }
  }

  return 0;
}
