#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "parser.h"

// ===========================[ Constants ]===========================
#define MAX_LINE 80
#define MAX_COMMANDS 20
#define MAX_PROCESSES 20

#ifdef DEBUG
    #define DEBUG_MODE 1
#else
    #define DEBUG_MODE 0
#endif

// ===========================[ Structures ]==========================
typedef struct {
    int id;
    int status;
    tline * line;
    pid_t pids[MAX_COMMANDS];
    int pipes[MAX_COMMANDS - 1][2];
} tjob;

// ===========================[ Prototypes ]==========================

void printDebugData(int mode, tline * line);
void readLine(char * line, int max);
void redirectIO(tjob * job, int i);
int isInputOk(tline * line);
int externalCommand(tline * line);
int changeDirectory(char * path);
void umaskCommand(char * mask);

void ctrlC(int sig);

// ========================[ Global Variables ]=======================
tjob * jobs[MAX_PROCESSES];
int count = 0;

// ==============================[ Main ]=============================
int main(int argc, char * argv[]) {
    tline * line;
    int i;
    char buffer[MAX_LINE];

    // Initialize jobs
    for (i = 0; i < MAX_PROCESSES; i++) {
        jobs[i] = (tjob *) malloc(sizeof(tjob));
        jobs[i]->id = -1;
        jobs[i]->line = NULL;
    }

    // Set signal handlers
    signal(SIGINT, ctrlC);
    
    // Clear screen at the beginning
    system("clear");

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

        // Handle commands
        if (strcmp(line->commands[0].argv[0], "cd") == 0) {
            changeDirectory(line->commands[0].argv[1]);

        } else if (strcmp(line->commands[0].argv[0], "exit") == 0) {
            break;

        } else if (strcmp(line->commands[0].argv[0], "umask") == 0) {
            umaskCommand(line->commands[0].argv[1]);

        } else {
            externalCommand(line);
        }
    }

    // Free memory
    for (i = 0; i < MAX_PROCESSES; i++) {
        free(jobs[i]);
    }

    return 0;
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
    char * customPrompt, * cwd, * username;
    int len;

    // Get username
    username = getenv("USER");

    // Get current working directory
    cwd = getcwd(NULL, 0);

    // Get home subdirectory
    if (strstr(cwd, getenv("HOME")) != NULL) {
        len = strlen(getenv("HOME"));
        cwd += len;
        customPrompt = "%s@msh: ~%s $> ";
    } else {
        customPrompt = "%s@msh: %s $> ";
    }

    // Print custom prompt
    fprintf(stdout, customPrompt, username, cwd);
    fgets(line, max, stdin);
}

/**
 * Redirects input and output for a given job
 * 
 * @param job Job to redirect
 * @param i Index of the command
 */
void redirectIO(tjob * job, int i) {
    int j;
    tline * line = job->line;

    // Redirect input from previous pipe or file
    if (i > 0) {
        dup2(job->pipes[i - 1][0], STDIN_FILENO);
    } else if (line->redirect_input != NULL) {
        freopen(line->redirect_input, "r", stdin);
    }

    // Redirect output to next pipe or file
    if (i < line->ncommands - 1) {
        dup2(job->pipes[i][1], STDOUT_FILENO);
    } else if (line->redirect_output != NULL) {
        freopen(line->redirect_output, "w", stdout);
    }

    // Redirect error to file
    if (line->redirect_error != NULL) {
        freopen(line->redirect_error, "w", stderr);
    }

    // Close all pipe file descriptors
    for (j = 0; j < line->ncommands - 1; j++) {
        close(job->pipes[j][0]);
        close(job->pipes[j][1]);
    }
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

    // Handle cd command
    if (strcmp(line->commands[0].argv[0], "cd") == 0) {
        return 1;
    }

    // Handle exit command
    if (strcmp(line->commands[0].argv[0], "exit") == 0) {
        return 1;
    }

    // Handle jobs command
    if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
        return 1;
    }

    // Handle umask command
    if (strcmp(line->commands[0].argv[0], "umask") == 0) {
        return 1;
    }

    // Handle external commands
    for (i = 0; i < line->ncommands; i++) {
        if (line->commands[i].filename == NULL) {
            return -1;
        }
    }

    return 1;
}

/**
 * Changes the current working directory
 * 
 * @param path Path to change to (NULL for home directory)
 * @return 0 if successful, -1 if failed
 */
int changeDirectory(char * path) {
    char * dir;

    if (path == NULL) {
        dir = getenv("HOME");
    } else {
        dir = path;
    }

    return chdir(dir);
}

/**
 * Executes the umask command
 * @param mask Mask to set
 */
void umaskCommand(char * mask) {
    mode_t mode;

    // Output current mask if no mask is provided
    if (mask == NULL) {
        mode = umask(0);
        umask(mode);
        fprintf(stdout, "%04o\n", mode);

    // Set new mask if provided
    } else {
        mode = strtol(mask, NULL, 8);
        umask(mode);
    }
}

/**
 * Executes an external command from a parsed line
 * 
 * @param line Parsed line to execute
 * @return 0 if successful, -1 if failed
 */
int externalCommand(tline * line) {
    int i, current;
    pid_t pid;

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
        if (pipe(jobs[current]->pipes[i]) < 0) {
            fprintf(stderr, "Error: pipe failed\n");
            exit(EXIT_FAILURE);
        }
    }

    // Create children
    for (i = 0; i < line->ncommands; i++) {
        pid = fork();
        jobs[current]->pids[i] = pid;

        if (pid == 0) {

            // Redirect input and output
            redirectIO(jobs[current], i);

            // Execute command
            execvp(line->commands[i].filename, line->commands[i].argv);
            fprintf(stderr,"Error: execvp failed");
            exit(EXIT_FAILURE);
            
        } else if (pid < 0) {
            fprintf(stderr, "Error: fork failed\n");
            exit(EXIT_FAILURE);
        }
    }

    // Close all pipes in the parent process
    for (i = 0; i < line->ncommands - 1; i++) {
        close(jobs[current]->pipes[i][0]);
        close(jobs[current]->pipes[i][1]);
    }

    for (i = 0; i < line->ncommands; i++) {
        pid = jobs[current]->pids[i];

        if (line->background == 0) {
            waitpid(pid, NULL, 0);
        } else {
            waitpid(pid, NULL, WNOHANG);
        }
    }
    
    return 0;
}

// ===========================[ Signal Handlers ]==========================

/**
 * Handles the SIGINT signal (Ctrl+C)
 * 
 * @param sig Signal number
 */
void ctrlC(int sig) {
    int i, j;
    pid_t pid;

    for (i = 0; i < MAX_PROCESSES; i++) {
        if (jobs[i]->id != -1 && jobs[i]->line->background == 0) {
            for (j = 0; j < jobs[i]->line->ncommands; j++) {
                pid = jobs[i]->pids[j];
                kill(pid, SIGKILL);
            }
        }
    }
}