#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "parser.h"

// ===========================[ Constants ]===========================
#define PROMPT "$msh> "
#define MAX_LINE 80
#define MAX_COMMANDS 20
#define MAX_PROCESSES 20
#define DEBUG_MODE 0

// ===========================[ Structures ]==========================
typedef struct {
    int id;
    int status;
    tline * line;
    pid_t pids[MAX_COMMANDS];
    int * pipes[MAX_COMMANDS - 1];
} tjob;

// ===========================[ Prototypes ]==========================

void printDebugData(int mode, tline * line);
void readLine(char * line, int max);
int isInputOk(tline * line);

// ========================[ Global Variables ]=======================
tjob * jobs[MAX_PROCESSES];

// ==============================[ Main ]=============================
int main(int argc, char * argv[]) {
    tline * line;
    int i, count = 0;
    int current = 0;
    int auxPipe[2];
    pid_t auxPid;
    char buffer[MAX_LINE];

    // Initialize jobs
    for (i = 0; i < MAX_PROCESSES; i++) {
        jobs[i] = (tjob *) malloc(sizeof(tjob));
        jobs[i]->id = -1;
        jobs[i]->line = NULL;
    }

    // Main loop
    while (1) {
        // Read line and tokenize
        readLine(buffer, MAX_LINE);
        line = tokenize(buffer);

        // DEBUG
        printDebugData(DEBUG_MODE, line);

        // Check for user input errors or empty commands
        if (isInputOk(line) == -1) {
            fprintf(stderr, "Command Error: Command not found\n");
            continue;
        } else if (isInputOk(line) == 0) {
            continue;
        }

        // Get available job slot
        for (i = 0; i < MAX_PROCESSES; i++) {
            if (jobs[i]->id == -1 || jobs[i]->status == 0) {
                jobs[i]->id = count++;
                jobs[i]->line = line;
                break;
            }
        }

        current = i;

        // Initialize pipes
        for (i = 0; i < line->ncommands - 1; i++) {
            pipe(auxPipe);
            jobs[current]->pipes[i] = auxPipe;
        }

        for (i = 0; i < line->ncommands; i++) {
            auxPid = fork();
            jobs[current]->pids[i] = auxPid;

            if (auxPid == 0) {
                exit(0);
            }
        }

        printf("Created %d children\n", line->ncommands);
        printf("Waiting for children\n");

        for (i = 0; i < line->ncommands; i++) {
            auxPid = jobs[current]->pids[i];
            kill(auxPid, SIGUSR1);

            if (line->background == 0) {
                waitpid(auxPid, NULL, 0);
            } else {
                waitpid(auxPid, NULL, WNOHANG);
            }
        }

        printf("Children finished\n");
    }
}

// ===========================[ Functions ]===========================

/**
 * Prints debug data from a parsed line when DEBUG_MODE is enabled
 * 
 * @param mode Debug mode
 * @param line Parsed line to print
 */
void printDebugData(int mode, tline * line) {
    tcommand * command;
    int i, j;

    // Exit if mode is disabled
    if (mode == 0) return;

    // Print data
    fprintf(stdout, "---[ Debug Data ]---\n");
    fprintf(stdout, "Number of commands: %d\n", line->ncommands);
    fprintf(stdout, "Redirect input: %s\n", line->redirect_input);
    fprintf(stdout, "Redirect output: %s\n", line->redirect_output);
    fprintf(stdout, "Redirect error: %s\n", line->redirect_error);
    fprintf(stdout, "Background: %d\n", line->background);
    fprintf(stdout, "\n");

    for (i = 0; i < line->ncommands; i++) {
        command = line->commands + i;
        fprintf(stdout, "Command %d\n", i);
        fprintf(stdout, "\tFilename: %s\n", command->filename);
        fprintf(stdout, "\tArg Count: %d\n", command->argc);

        for (j = 0; j < command->argc; j++) {
            fprintf(stdout, "\tArg %d: %s\n", j, command->argv[j]);
        }
    }

    fprintf(stdout, "--------------------\n\n");
}

/**
 * Reads a line from the standard input
 * 
 * @param line Buffer to store the line
 * @param max Maximum number of characters to read
 * @return Number of characters read
 */
void readLine(char * line, int max) {
    fprintf(stdout, PROMPT);
    fgets(line, max, stdin);
    fprintf(stdout, "\n");
}

/**
 * Checks if the parsed line is correct
 * 
 * @param line Parsed line to check
 * @return 1 if the line is correct, 0 if there are no commands, -1 if there's an error
 */
int isInputOk(tline * line) {
    int i;

    if (line->ncommands == 0) {
        return 0;
    }

    for (i = 0; i < line->ncommands; i++) {
        if (line->commands[i].filename == NULL) {
            return -1;
        }
    }

    return 1;
}
