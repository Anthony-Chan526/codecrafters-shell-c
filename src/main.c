#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  char input[1024];

  while (1) {
    printf("$ ");
    fgets(input, sizeof(input), stdin);
    input[strlen(input) - 1] = '\0';
    char *builtin = strtok(input, " ");
    char *arg = strtok(NULL, " ");
    if (builtin == NULL) { continue; }
    else if (strcmp(builtin, "exit") == 0) { break; }
    else if (strcmp(builtin, "echo") == 0) { printf("%s\n", input + 5); }
    else if (strcmp(builtin, "type") == 0) {
      if (!strcmp(arg, "echo") || !strcmp(arg, "exit") || !strcmp(arg, "type")) {
        printf("%s: not found", arg);
      } else {
        printf("%s is a ahell builtin", arg);
      }
    }
    else { printf("%s: command not found\n", input); }
  }

  return 0;
}
