#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <fcntl.h>

int find_path(const char *arg, char *full_path) {
  char *path = strdup(getenv("PATH"));
  char *dir = strtok(path, ":");
  while (dir != NULL) {
    snprintf(full_path, PATH_MAX, "%s/%s", dir, arg);
    if (access(full_path, X_OK) == 0) {
      break;
    }
    dir = strtok(NULL, ":"); 
  }
  free(path);
  if (dir == NULL) {
    return 0;
  } else {
    return 1;
  }
}

void parse_input(char *input, char **args, int max_args) {
  int count = 0;
  char *src = input;

  while (*src != '\0' && count < max_args - 1) {
    while (*src == ' ' || *src == '\t') {
      src++;
    }

    if (*src == '\0') { break; }

    args[count++] = src;
    char *dst = src;
    int in_single_quote = 0;
    int in_double_quote = 0;
    while (*src != '\0') {
      if (*src == '\'' && !in_double_quote) {
        in_single_quote = !in_single_quote;
        src++;
      } else if (*src == '\"' && !in_single_quote) {
        in_double_quote = !in_double_quote;
        src++;
      } else if (*src == '\\' && !in_single_quote && !in_double_quote) {
        src++;
        if (*src != '\0') { *dst++ = *src++; }
      } else if (*src == '\\' && in_double_quote) {
        if (*(src + 1) == '\\' || *(src + 1) == '\"') {
          src++; 
          *dst++ = *src++;
        } else {
          *dst++ = *src++;
        }
      } else if (!in_single_quote && !in_double_quote && (*src == ' ' || *src == '\t')) { 
        break; 
      } else {
        *dst++ = *src++;
      }
    }
    
    if (*src != '\0') {
      src++;
    }
    *dst = '\0';
  }
  args[count] = NULL;
}

int handle_redirection(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0) {
      char *filename = args[i + 1];
      if(!filename) { fprintf(stderr, ">: redirection error\n"); return 1; }
      int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      dup2(file_fd, STDOUT_FILENO);
      close(file_fd);
      args[i] = NULL;
      return 0;
    } else if (strcmp(args[i], "2>") == 0) {
      char *filename = args[i + 1];
      if(!filename) { fprintf(stderr, "2>: redirection error\n"); return 1; }
      int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      dup2(file_fd, STDERR_FILENO);
      close(file_fd);
      args[i] = NULL;
      return 0;
    } else if (strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0) {
      char *filename = args[i + 1];
      if(!filename) { fprintf(stderr, ">>: redirection error\n"); return 1; }
      int file_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
      dup2(file_fd, STDOUT_FILENO);
      close(file_fd);
      args[i] = NULL;
      return 0;
    } else if (strcmp(args[i], "2>>") == 0 ) {
      char *filename = args[i + 1];
      if(!filename) { fprintf(stderr, "2>>: redirection error\n"); return 1; }
      int file_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
      dup2(file_fd, STDERR_FILENO);
      close(file_fd);
      args[i] = NULL;
      return 0;
    }
  }
  return 0;
} 

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  char *args[64];
  char input[1024];

  while (1) {
    printf("$ ");
    fgets(input, sizeof(input), stdin);
    input[strlen(input) - 1] = '\0';
    parse_input(input, args, 64);
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    int redirect_error = handle_redirection(args); 
    if(redirect_error) {
      close(saved_stdout);
      close(saved_stderr);
      continue;
    }

    if (args[0] == NULL) { continue; }

    else if (strcmp(args[0], "exit") == 0) { break; }
    
    else if (strcmp(args[0], "echo") == 0) {
      for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL) {
          printf(" ");
        }
      }
      printf("\n");
    
    } else if (strcmp(args[0], "type") == 0) {
      if (strcmp(args[1], "echo") == 0 || 
          strcmp(args[1], "exit") == 0 || 
          strcmp(args[1], "type") == 0 || 
          strcmp(args[1], "pwd")  == 0 ||
          strcmp(args[1], "cd") == 0) {
        printf("%s is a shell builtin\n", args[1]);
      } else {
        char full_path[PATH_MAX];
        if(find_path(args[1], full_path)) {
          printf("%s is %s\n", args[1], full_path);
        } else {
          fprintf(stderr, "%s: not found\n", args[1]);
        }
      }

    } else if (strcmp(args[0], "pwd") == 0) {
      char cwd[1024];
      if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
      } else {
        perror("pwd");
      }

    } else if (strcmp(args[0], "cd") == 0) {
      char *new_dir = args[1];
      char full_path[PATH_MAX];
      if (new_dir[0] == '~') {
        char *home = getenv("HOME");
        if (new_dir[1] == '\0') { 
          snprintf(full_path, sizeof(full_path), "%s", home);
        } else {
          snprintf(full_path, sizeof(full_path), "%s%s", home, new_dir + 1);
        }
      } else {
        snprintf(full_path, sizeof(full_path), "%s", new_dir);
      }
      if (chdir(full_path) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", new_dir);
      }

    } else { 
      char full_path[PATH_MAX];
      if(find_path(args[0], full_path)) {
        pid_t pid = fork();
        if (pid == 0) {
          execv(full_path, args);
          perror("Execution failed");
          exit(EXIT_FAILURE);
        } else {
          waitpid(pid, NULL, 0);
        }
      } else {
        fprintf(stderr, "%s: command not found\n", args[0]);
      }
    }
     
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
  }
  return 0;
}
