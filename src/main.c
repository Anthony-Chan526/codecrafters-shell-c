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
    else if (strcmp(input, "exit ") == 0) { break; }
    else if (strcmp(input, "echo ") == 0) { printf("%s\n", input + 5); }
    else if (strcmp(input, "type ") == 0) {
      char *arg = input + 5;
      if (strcmp(arg, "echo") && strcmp(arg, "exit") && strcmp(arg, "type")) {
        printf("%s: not found\n", arg);
      } else {
        printf("%s is a shell builtin\n", arg);
      }
    }
    else { printf("%s: command not found\n", input); }
  }

  return 0;
}
