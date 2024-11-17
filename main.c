#include <stdio.h>

#include "parser.h"

// ===========================[ Constants ]===========================
#define PROMPT "$msh> "
#define MAX_LINE 80
#define MAX_ARGS 20
#define DEBUG_MODE 1

// ===========================[ Functions ]===========================

void printDebugData(tline * line);

// ==============================[ Main ]=============================
int main(int argc, char * argv[]) {
    // Variables declaration
    char input[MAX_LINE];
    tline * parsedLine;

    // Main loop
    while (1)
    {
        printf(PROMPT);
        fgets(input, MAX_LINE, stdin);
        printf("\n");

        parsedLine = tokenize(input);
        printDebugData(parsedLine);
    }
}

/**
 * Prints debug data from a parsed line when DEBUG_MODE is enabled
 * 
 * @param line Parsed line to print
 */
void printDebugData(tline * line) {
    tcommand * command;
    int i, j;

    // Exit if mode is disabled
    if (DEBUG_MODE == 0) return;

    // Print data
    printf("---[ Debug Data ]---\n");
    printf("Number of commands: %d\n", line->ncommands);
    printf("Redirect input: %s\n", line->redirect_input);
    printf("Redirect output: %s\n", line->redirect_output);
    printf("Redirect error: %s\n", line->redirect_error);
    printf("Background: %d\n", line->background);
    printf("\n");

    for (i = 0; i < line->ncommands; i++) {
        command = line->commands + i;
        printf("Command %d\n", i);
        printf("\tFilename: %s\n", command->filename);
        printf("\tArg Count: %d\n", command->argc);

        for (j = 0; j < command->argc; j++) {
            printf("\tArg %d: %s\n", j, command->argv[j]);
        }
    }

    printf("--------------------\n\n");
}