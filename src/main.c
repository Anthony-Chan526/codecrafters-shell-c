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

char *builtins[] = {"exit", "echo", "type", "pwd", "cd", "complete", "jobs", "history", "declare", NULL};

typedef struct {
  char *command_name;
  char *completer_script;
} Completion;

Completion completion_registry[100];
int completion_count = 0;  

#define MAX_JOBS 32
typedef enum {
  EMPTY = 0,
  RUNNING,
  DONE
} JobState;

typedef struct {
  int job_id;
  pid_t pid;
  char *command;
  JobState state;
} Job;

Job job_list[MAX_JOBS];
int job_count = 0;
int job_history[MAX_JOBS];

typedef struct { 
  char *args[64];
} Command;

Command cmds[16];

#define MAX_HISTORY 1000
typedef struct {
    char *items[MAX_HISTORY];
    int count;
} History;

History history = { .count = 0 };
int append_idx = 0;

typedef struct {
  char *name;
  char *value;
} Variable;

Variable vars[64];
int var_count = 0;

static char **command_matches = NULL;
static int match_count = 0;
static int current_match_idx = 0;

static char **script_matches = NULL;
static int script_match_count = 0;
static int script_match_idx = 0;

void clear_matches(char ***matches, int *count, int *idx) {
  if (matches) {
    for (int i = 0; i < *count; i++) {
      free((*matches)[i]);
    }
    free(*matches);
    *matches = NULL;
  }
  if (count) { *count = 0; }
  if (idx) { *idx = 0; }
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
    clear_matches(&command_matches, &match_count, &current_match_idx);
    int len = strlen(text);
    
    for (int i = 0; builtins[i] != NULL; i++) {
      if (strncmp(builtins[i], text, len) == 0) {
        add_matches(builtins[i]);
      }
    }

    char *path_env = getenv("PATH");
    if (path_env) {
      char *path_copy = strdup(path_env);
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
  clear_matches(&command_matches, &match_count, &current_match_idx);
  return NULL;
}

int get_completion_context(char *cmd, const char *text, char *prev_word, int max_len) {
  cmd[0] = '\0';
  prev_word[0] = '\0';
  char line_up_to_cursor[1024];
  if (rl_point >= (int)sizeof(line_up_to_cursor)) { return 0; }
  strncpy(line_up_to_cursor, rl_line_buffer, rl_point);
  line_up_to_cursor[rl_point] = '\0';

  char *words[128];
  int word_count = 0;
  char *token = strtok(line_up_to_cursor, " \t");
  while (token != NULL && word_count < 128) {
    words[word_count++] = token;
    token = strtok(NULL, " \t");
  }
  if (word_count == 0) { return 0; }
  strncpy(cmd, words[0], max_len - 1);
  cmd[max_len - 1] = '\0';
  int is_completing_partial = (text != NULL && strlen(text) > 0);
  if (is_completing_partial) {
    if (word_count >= 2) {
      strncpy(prev_word, words[word_count - 2], max_len - 1);
    } else { 
      prev_word[0] = '\0';
    }
  } else {
    if (word_count >= 1) {
      strncpy(prev_word, words[word_count - 1], max_len - 1);
    } else {
      prev_word[0] = '\0';
    }
  }
  prev_word[max_len - 1] = '\0';
  return 1;
}

char *script_generator(const char *text, int state) {
  if (!state) {
    clear_matches(&script_matches, &script_match_count, &script_match_idx);
    char cmd[256];
    char prev_word[256];
    if (!get_completion_context(cmd, text, prev_word, 256)) { return NULL; }

    char *script_path = NULL;
    for (int i = 0; i < completion_count; i++) {
      if (strcmp(completion_registry[i].command_name, cmd) == 0) {
        script_path = completion_registry[i].completer_script;
        break;
      }
    }
    if(script_path == NULL) { return NULL; }
    
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) { return NULL; }
    pid_t pid = fork();
    if (pid == 0) {
      dup2(pipe_fds[1], STDOUT_FILENO); 
      close(pipe_fds[0]);
      close(pipe_fds[1]);
      char comp_point_str[16];
      snprintf(comp_point_str, sizeof(comp_point_str), "%d", rl_point);
      setenv("COMP_LINE", rl_line_buffer, 1);
      setenv("COMP_POINT", comp_point_str, 1);
      execl(script_path, script_path, cmd, text, prev_word, NULL);
      exit(EXIT_FAILURE);
    }

    close(pipe_fds[1]);
    FILE *stream = fdopen(pipe_fds[0], "r");
    char line[256];
    int len = strlen(text);
    while (fgets(line, sizeof(line), stream)) {
      line[strcspn(line, "\n")] = '\0';
      if (strncmp(line, text, len) == 0) {
        script_matches = realloc(script_matches, (script_match_count + 1) * sizeof(char *));
        script_matches[script_match_count++] = strdup(line);
      }
    }
    fclose(stream);
    waitpid(pid, NULL, 0);
  }
  if (script_match_idx < script_match_count) {
    return strdup(script_matches[script_match_idx++]);
  }
  clear_matches(&script_matches, &script_match_count, &script_match_idx);
  return NULL;
}

char **command_completion(const char *text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }

    char *line_copy = strdup(rl_line_buffer);
    char *cmd = strtok(line_copy, " \t");
    char *script = NULL;
    if (cmd != NULL) {
      for (int i = 0; i < completion_count; i++) {
        if (strcmp(completion_registry[i].command_name, cmd) == 0) {
          script = completion_registry[i].completer_script;
          break;
        }
      }
    }
    free(line_copy);
    if (script != NULL) {
      rl_attempted_completion_over = 1;
      return rl_completion_matches(text, script_generator);
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

void reap_background_jobs() {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (int i = 0; i < MAX_JOBS; i++) {
      if (job_list[i].state == RUNNING && job_list[i].pid == pid) {
        job_list[i].state = DONE;
        break;
      }
    }
  }
}

void add_job_history(int idx) {
  for (int i = job_count; i > 0; i--) {
    job_history[i] = job_history[i - 1];
  }
  job_history[0] = idx;
  job_count++;
}

void remove_job_history(int idx) {
  for (int i = 0; i < job_count; i++) {
    if (job_history[i] == idx) {
      for (int j = i; j < job_count - 1; j++) {
        job_history[j] = job_history[j + 1];
      }
      job_count--;
      break;
    }
  }
}

void print_and_clean_reaped_jobs() {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i].state == DONE) {
      char symbol = ' ';
      if (job_history[0] == i) {
        symbol = '+';
      } else if (job_history[1] == i) {
        symbol = '-';
      } 
      printf("[%d]%c  Done                 %s\n", job_list[i].job_id, symbol, job_list[i].command);
      free(job_list[i].command);
      job_list[i].command = NULL;
      job_list[i].state = EMPTY;
      remove_job_history(i);
    }
  }
}

void add_cmd_history(const char *input) {
  if (input == NULL || strlen(input) == 0) { return; }
  if (history.count > 0 && strcmp(history.items[history.count - 1], input) == 0) { return; }
  if (history.count >= MAX_HISTORY) {
    free(history.items[0]);
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      history.items[i] = history.items[i + 1];
    }
    history.count = MAX_HISTORY - 1; 
  }
  history.items[history.count++] = strdup(input);
}

void read_history_file(const char *path) {
  FILE *file = fopen(path, "r");
  if (file == NULL) { return; }
  char fline[1024];
  while (fgets(fline, sizeof(fline), file) != NULL) {
    fline[strcspn(fline, "\r\n")] = '\0';
    if (fline[0] != '\0') {
      add_history(fline);
      add_cmd_history(fline);
    }
  }
  fclose(file);
  append_idx = history.count;
} 

int split_pipeline(char **args, Command *cmds) {
  int idx = 0;
  int num_cmds = 0;
  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "|") == 0) {
      cmds[num_cmds].args[idx] = NULL;
      num_cmds++;
      idx = 0;    
    } else {
      cmds[num_cmds].args[idx++] = args[i];
    }
  }
  cmds[num_cmds].args[idx] = NULL;
  num_cmds++;
  return num_cmds;
}

void execute_single_command(char **args) {
  if (args[0] == NULL) { exit(EXIT_SUCCESS); }
  if (strcmp(args[0], "exit") == 0) { exit(EXIT_SUCCESS); }
    
  else if (strcmp(args[0], "echo") == 0) {
    for (int i = 1; args[i] != NULL; i++) {
      printf("%s", args[i]);
      if (args[i + 1] != NULL) {
        printf(" ");
      }
    }
    printf("\n");
    exit(EXIT_SUCCESS);
    
  } else if (strcmp(args[0], "type") == 0) {
    if (strcmp(args[1], "echo") == 0 || 
        strcmp(args[1], "exit") == 0 || 
        strcmp(args[1], "type") == 0 || 
        strcmp(args[1], "pwd") == 0 ||
        strcmp(args[1], "cd") == 0 ||
        strcmp(args[1], "complete") == 0 ||
        strcmp(args[1], "jobs") == 0 ||
        strcmp(args[1], "history") == 0 ||
        strcmp(args[1], "declare") == 0) {
          printf("%s is a shell builtin\n", args[1]);
    } else {
      char full_path[PATH_MAX];
      if (find_path(args[1], full_path)) {
        printf("%s is %s\n", args[1], full_path);
      } else {
        fprintf(stderr, "%s: not found\n", args[1]);
      }
    }
    exit(EXIT_SUCCESS);

  } else if (strcmp(args[0], "pwd") == 0) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      printf("%s\n", cwd);
    } else {
      perror("pwd");
    }
    exit(EXIT_SUCCESS);

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
    exit(EXIT_SUCCESS);
    
  } else if (strcmp(args[0], "complete") == 0) {  
    if (strcmp(args[1], "-p") == 0) {
      if (args[2] == NULL) {
        for (int i = 0; i < completion_count; i++) {
          printf("complete -C '%s' %s\n", 
                  completion_registry[i].completer_script, 
                  completion_registry[i].command_name);
        }
      } else {
        int i;
        for (i = 0; i < completion_count; i++) {
          if (strcmp(completion_registry[i].command_name, args[2]) == 0) {
            printf("complete -C '%s' %s\n", 
                    completion_registry[i].completer_script, 
                    completion_registry[i].command_name);
            break;
          }
        }
        if (i >= completion_count) {
          fprintf(stderr, "complete: %s: no completion specification\n", args[2]);
        } 
      }
    } else if (strcmp(args[1], "-C") == 0) {
      int found_idx = -1; 
      for (int i = 0; i < completion_count; i++) {
        if (strcmp(completion_registry[i].command_name, args[3]) == 0) {
          found_idx = i;
          break;
        }
      }  
        
      if (found_idx != -1) {
        free(completion_registry[found_idx].completer_script);
        completion_registry[found_idx].completer_script = strdup(args[2]);
      } else {
        completion_registry[completion_count].command_name = strdup(args[3]);
        completion_registry[completion_count].completer_script = strdup(args[2]);
        completion_count++;
      }
    } else if (strcmp(args[1], "-r") == 0) {
      if (args[2] == NULL) {
        for (int i = 0; i < completion_count; i++) {
          free(completion_registry[i].command_name);
          free(completion_registry[i].completer_script);
        }
      } else {
        for (int i = 0; i < completion_count; i++) {
          if (strcmp(completion_registry[i].command_name, args[2]) == 0) {
            free(completion_registry[i].command_name);
            free(completion_registry[i].completer_script);
            for (int j = i; j < completion_count - 1; j++) {
              completion_registry[j] = completion_registry[j + 1];
            }
            completion_count--;
            break;
          }
        }
      }  
    }
    exit(EXIT_SUCCESS);

  } else if (strcmp(args[0], "jobs") == 0) {
    reap_background_jobs();
    for (int i = 0; i < MAX_JOBS; i++) {
      if (job_list[i].state != EMPTY) {
        char symbol = ' ';
        if (job_history[0] == i) {
          symbol = '+';
        } else if (job_history[1] == i) {
          symbol = '-';
        } 
        if (job_list[i].state == RUNNING) {
          printf("[%d]%c  Running                 %s &\n", job_list[i].job_id, symbol, job_list[i].command);
        } else {
          printf("[%d]%c  Done                 %s\n", job_list[i].job_id, symbol, job_list[i].command);
          free(job_list[i].command);
          job_list[i].command = NULL;
          job_list[i].state = EMPTY;
          remove_job_history(i);
        }
      }
    }
    exit(EXIT_SUCCESS);
  
  } else if(strcmp(args[0], "history") == 0){
    if (args[1] != NULL && strcmp(args[1], "-r") == 0) {
      if (args[2] != NULL) { 
        read_history_file(args[2]); 
      }
    } else if (args[1] != NULL && strcmp(args[1], "-w") == 0) {
      if (args[2] != NULL) {
        FILE *file = fopen(args[2], "w");
        if (file != NULL) {
          for (int i = 0; i < history.count; i++) {
            fprintf(file, "%s\n", history.items[i]);
          }
          fclose(file);
          append_idx = history.count;
        }
      }
    } else if (args[1] != NULL && strcmp(args[1], "-a") == 0) {
      if (args[2] != NULL) {
        FILE *file = fopen(args[2], "a");
        if (file != NULL) {
          for (int i = append_idx; i < history.count; i++) {
            fprintf(file, "%s\n", history.items[i]);
          }
          fclose(file);
          append_idx = history.count;
        }
      }
    } else {
      int start = 0;
      if (args[1] != NULL) {
        int limit = atoi(args[1]);
        if (limit > 0) {
          start = history.count - limit;
          if (start < 0) { start = 0; }
        }
      }
      for (int i = start; i < history.count; i++) {
        printf("%d  %s\n", i + 1, history.items[i]);
      }
    }
    exit(EXIT_SUCCESS);

  } else { 
    char full_path[PATH_MAX];
    if (find_path(args[0], full_path)) {
      execv(full_path, args);
      perror("execv failed");
      exit(EXIT_FAILURE);
    } else {
      fprintf(stderr, "%s: command not found\n", args[0]);
      exit(EXIT_FAILURE);
    }
  }     
}

void execute_pipeline(Command *cmds, int num_cmds) {
  int i;
  int pipefds[2 * (num_cmds - 1)];

  for (i =0; i < num_cmds - 1; i++) {
    if (pipe(pipefds + i * 2) < 0) {
      perror("Pipe creation failed");
      exit(EXIT_FAILURE);
    }
  }

  for (i = 0; i < num_cmds; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      if (i != 0) {
        if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) < 0) {
          perror("dup2 failed (stdin)");
          exit(EXIT_FAILURE);
        }
      }
      if ( i != num_cmds - 1) {
        if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) < 0) {
          perror("dup2 failed (stdout)");
          exit(EXIT_FAILURE);
        }
      }
      for (int j = 0; j < 2 * (num_cmds - 1); j++) {
        close(pipefds[j]);
      }
      execute_single_command(cmds[i].args);
    } else if (pid < 0) {
      perror("Fork failed");
      exit(EXIT_FAILURE);
    }
  }
  for (i = 0; i < 2 * (num_cmds - 1); i++) {
    close(pipefds[i]);
  }
  for (i = 0; i < num_cmds; i++) {
    wait(NULL);
  }
}

int main(int argc, char *words[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  char *args[64];
  char input[1024];
  char input_copy[1024];

  using_history();
  rl_attempted_completion_function = command_completion;
  read_history_file(getenv("HISTFILE"));

  while (1) {
    reap_background_jobs();
    print_and_clean_reaped_jobs();
    char *line = readline("$ ");
    if (line == NULL) { 
      break; 
    }
    if (line[0] != '\0') {
      add_history(line);
      add_cmd_history(line);
    }
    strncpy(input, line, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    strncpy(input_copy, line, sizeof(input) - 1);
    input_copy[sizeof(input) - 1] = '\0';
    free(line);

    parse_input(input, args, 64);
    if (args[0] == NULL) { continue; }

    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    int redirect_error = handle_redirection(args); 
    if(redirect_error) {
      close(saved_stdout);
      close(saved_stderr);
      continue;
    }

    int background = 0;
    int last_idx = 0;
    while (args[last_idx] != NULL) {
      last_idx++;
    }
    last_idx--;
    if (last_idx >= 0 && strcmp(args[last_idx], "&") == 0) {
      background = 1;
      args[last_idx] = NULL;
    }

    int num_cmds = split_pipeline(args, cmds);
    if (num_cmds > 1) {
      execute_pipeline(cmds, num_cmds);
      dup2(saved_stdout, STDOUT_FILENO);
      dup2(saved_stderr, STDERR_FILENO);
      close(saved_stdout);
      close(saved_stderr);
      continue;
    }

    if (strcmp(args[0], "exit") == 0) { break; }
    
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
          strcmp(args[1], "pwd") == 0 ||
          strcmp(args[1], "cd") == 0 ||
          strcmp(args[1], "complete") == 0 ||
          strcmp(args[1], "jobs") == 0 ||
          strcmp(args[1], "history") == 0 ||
          strcmp(args[1], "declare") == 0) {
        printf("%s is a shell builtin\n", args[1]);
      } else {
        char full_path[PATH_MAX];
        if (find_path(args[1], full_path)) {
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
    
    } else if (strcmp(args[0], "complete") == 0) {  
      if (strcmp(args[1], "-p") == 0) {
        if (args[2] == NULL) {
          for (int i = 0; i < completion_count; i++) {
            printf("complete -C '%s' %s\n", 
                    completion_registry[i].completer_script, 
                    completion_registry[i].command_name);
          }
        } else {
          int i;
          for (i = 0; i < completion_count; i++) {
            if (strcmp(completion_registry[i].command_name, args[2]) == 0) {
              printf("complete -C '%s' %s\n", 
                      completion_registry[i].completer_script, 
                      completion_registry[i].command_name);
              break;
            }
          }
          if (i >= completion_count) {
            fprintf(stderr, "complete: %s: no completion specification\n", args[2]);
          } 
        }
      } else if (strcmp(args[1], "-C") == 0) {
        int found_idx = -1; 
        for (int i = 0; i < completion_count; i++) {
          if (strcmp(completion_registry[i].command_name, args[3]) == 0) {
            found_idx = i;
            break;
          }
        }  
          
        if (found_idx != -1) {
          free(completion_registry[found_idx].completer_script);
          completion_registry[found_idx].completer_script = strdup(args[2]);
        } else {
          completion_registry[completion_count].command_name = strdup(args[3]);
          completion_registry[completion_count].completer_script = strdup(args[2]);
          completion_count++;
        }
      } else if (strcmp(args[1], "-r") == 0) {
        if (args[2] == NULL) {
          for (int i = 0; i < completion_count; i++) {
            free(completion_registry[i].command_name);
            free(completion_registry[i].completer_script);
          }
        } else {
          for (int i = 0; i < completion_count; i++) {
            if (strcmp(completion_registry[i].command_name, args[2]) == 0) {
              free(completion_registry[i].command_name);
              free(completion_registry[i].completer_script);
              for (int j = i; j < completion_count - 1; j++) {
                completion_registry[j] = completion_registry[j + 1];
              }
              completion_count--;
              break;
            }
          }
        }  
      }

    } else if (strcmp(args[0], "jobs") == 0) {
      reap_background_jobs();
      for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].state != EMPTY) {
          char symbol = ' ';
          if (job_history[0] == i) {
            symbol = '+';
          } else if (job_history[1] == i) {
            symbol = '-';
          } 
          if (job_list[i].state == RUNNING) {
            printf("[%d]%c  Running                 %s &\n", job_list[i].job_id, symbol, job_list[i].command);
          } else {
            printf("[%d]%c  Done                 %s\n", job_list[i].job_id, symbol, job_list[i].command);
            free(job_list[i].command);
            job_list[i].command = NULL;
            job_list[i].state = EMPTY;
            remove_job_history(i);
          }
        }
      }
    
    } else if(strcmp(args[0], "history") == 0){
      if (args[1] != NULL && strcmp(args[1], "-r") == 0) {
        if (args[2] != NULL) {
          read_history_file(args[2]);
        }
      } else if (args[1] != NULL && strcmp(args[1], "-w") == 0) {
        if (args[2] != NULL) {
          FILE *file = fopen(args[2], "w");
          if (file != NULL) {
            for (int i = 0; i < history.count; i++) {
              fprintf(file, "%s\n", history.items[i]);
            }
            fclose(file);
            append_idx = history.count;
          }
        }
      } else if (args[1] != NULL && strcmp(args[1], "-a") == 0) {
        if (args[2] != NULL) {
          FILE *file = fopen(args[2], "a");
          if (file != NULL) {
            for (int i = append_idx; i < history.count; i++) {
              fprintf(file, "%s\n", history.items[i]);
            }
            fclose(file);
            append_idx = history.count;
          }
        }
      } else {
        int start = 0;
        if (args[1] != NULL) {
          int limit = atoi(args[1]);
          if (limit > 0) {
            start = history.count - limit;
            if (start < 0) { start = 0; }
          }
        }
        for (int i = start; i < history.count; i++) {
          printf("%d  %s\n", i + 1, history.items[i]);
        }
      }
    
    } else if(strcmp(args[0], "declare") == 0){
      if (strcmp(args[1], "-p") == 0) {
        int found = 0;
        for (int i = 0; i < var_count; i++) {
          if (strcmp(args[2], vars[i].name) == 0) {
            printf("declare -- %s=\"%s\"", vars[i].name, vars[i].value);
            found = 1;
            break;
          }
        }
        if (!found) {
          fprintf(stderr, "declare: %s: not found", args[2])
        }
      } else {
        if (strchr(args[2], '=') != NULL) {
          vars[var_count].name = strtok(args[2], "=");
          vars[var_count].value = strtok(NULL, "=");
          var_count++;
      }

    } else { 
      char full_path[PATH_MAX];
      if (find_path(args[0], full_path)) {
        pid_t pid = fork();
        if (pid == 0) {
          if (background) {
            int dev_null = open("/dev/null", O_RDONLY);
            if (dev_null != -1) {
              dup2(dev_null, STDIN_FILENO);
              close(dev_null);
            }
          }
          execv(full_path, args);
          perror("Execution failed");
          exit(EXIT_FAILURE);
        } else {
          if (background) {
            for (int i = 0; i < MAX_JOBS; i++) {
              if (job_list[i].state == EMPTY) {
                job_list[i].job_id = i + 1;
                job_list[i].pid = pid;
                char *amp = strrchr(input_copy, '&');
                if (amp) { *amp = '\0'; }
                int len = strlen(input_copy);
                while (len > 0 && (input_copy[len - 1] == ' ' || input_copy[len - 1] == '\t')) {
                  input_copy[len - 1] = '\0';
                  len--; 
                }
                job_list[i].command = strdup(input_copy);
                job_list[i].state = RUNNING;
                printf("[%d] %d\n", job_list[i].job_id, pid);
                add_job_history(i);
                break;
              }
            }
          } else {
            waitpid(pid, NULL, 0);
          }
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
  write_history(getenv("HISTFILE"));
  return 0;
}
