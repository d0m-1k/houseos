#include <stdio.h>
#include <syscall.h>

int main(void) {
    printf("\n");
    printf("+----------------------------------+\n");
    printf("| HouseOS Terminal                 |\n");
    printf("+----------------------------------+\n");
    printf("| Type 'help' for shell commands.  |\n");
    printf("| Type 'exit' to close terminal.   |\n");
    printf("+----------------------------------+\n");
    printf("\n");
    return exec("/bin/sh");
}
