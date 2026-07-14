#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <sys/stat.h>

char *commands[] = {"exit", "echo", "type", "pwd", "cd", NULL};

static char **command_matches = NULL;
static int match_count = 0;
static int current_match_idx = 0;

void clear_matches() {
  if (command_matches) {
    for (int i = 0; i < match_count; i++) {
      free(command_matches[i]);
    }
    free(command_matches);
    command_matches = NULL;
  }
  match_count = 0;
  current_match_idx = 0;
}

void add_matches(const char *name) {
  for (int i = 0; i < match_count; i++) {
    if (strcmp(command_matches[i], name) == 0) return;
  }
  command_matches = realloc(command_matches, (match_count + 1) * sizeof(char*));
  command_matches[match_count++] = strdup(name);
}

char *command_generator(const char *text, int state) {
  if (!state) {
    clear_matches();
    int len = strlen(text);
    
    for (int i = 0; builtins[i] != NULL; i++) {
      if (strncmp(builtins[i], text, len) == 0) {
        add_matches(builtins[i]);
      }
    }

    char *path_env = getenv(PATH);
    if (path_env) {
      char *path_copy = strdup(path_eenv);
      char *dir_path = strtok(path_copy, ":");

      while (dir_path != NULL) {
        DIR *dir = opendir(dir_path);
        if (dir) {
          struct dirent *entry;
          while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
              continue;
            }

            if (strncmp(entry->d_name, text, len) == 0) {
              char full_path[PATH_MAX];
              snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
              struct stat st;
              if (access(full_path, X_OK) == 0 && stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
                add_matches(entry->d_name);
              }
            }
          }
          closedir(dir);
        }
        dir_path = strtok(NULL, ":");
      }
      free(path_copy);
    }
  }
  if (current_match_idx < match_count) {
        return strdup(command_matches[current_match_idx++]);
  }
  return NULL;
}

char **command_completion(const char *text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    rl_attempted_completion_over = 0;
    return NULL;
}

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
  rl_attempted_completion_function = command_completion;

  while (1) {
    char *line = readline("$ ");
    if (line == NULL) { 
      break; 
    }
    strncpy(input, line, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    free(line);

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
