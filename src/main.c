#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  while (1) {
    printf("$ ");
    char command[1024];
    fgets(command, sizeof(command), stdin);
    command[strlen(command) - 1] = '\0';
    printf("%s: command not found\n", command);
    if (strcmp(input, "exit") == 0) { break; }
  }

  return 0;
}
