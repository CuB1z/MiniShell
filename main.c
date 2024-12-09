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
#define MAX_COMMANDS 5

#ifdef DEBUG
    #define DEBUG_MODE 1
#else
    #define DEBUG_MODE 0
#endif

// ===========================[ Structures ]==========================

/**
 * Job structure
 * 
 * @param id: Job ID
 * @param status: Job status (0: Stopped, 1: Running)
 * @param line: Parsed line
 * @param pids: Array of process IDs
 * @param pipes: Array of pipes
 * @param command: Command string
 */
typedef struct {
    int id;
    int status;
    tline * line;
    pid_t * pids;
    int ** pipes;
    char * command;
    int background;
} tjob;

// ===========================[ Prototypes ]==========================

// Functions
void printDebugData(int mode, tline * line);
void readLine(char * line, int max);
void redirectIO(tjob * job, int i);
int isInputOk(tline * line);
int externalCommand(tline * line, char* command);
int changeDirectory(char * path);
void umaskCommand(char * mask);
void jobsCommand();
void bgCommand(char * job_id);
void initializeJob(tjob * job);
int addJob(tline * line, char * command);

// Signal handlers
void ctrlC(int sig);
void ctrlZ(int sig);
void terminatedChildHandler(int sig);

// Utilities
int getRunningJobIndex();
void sortJobsById(tjob * jobs[]);
int compareJobs(const void * a, const void * b);

// ========================[ Global Variables ]=======================
tjob * jobs[MAX_COMMANDS];
int stoppedJobs[MAX_COMMANDS];
int lastStoppedJob = -1;
int count = 0, bgJobs = 0;

// ==============================[ Main ]=============================
int main(int argc, char * argv[]) {
    tline * line;
    int i;
    char buffer[MAX_LINE];

    // Initialize jobs
    for (i = 0; i < MAX_COMMANDS; i++) {
        jobs[i] = (tjob *) malloc(sizeof(tjob));
        initializeJob(jobs[i]);
    }

    // Initialize stopped jobs
    for (i = 0; i < MAX_COMMANDS; i++) {
        stoppedJobs[i] = -1;
    }

    // Set signal handlers
    signal(SIGINT, ctrlC);
    signal(SIGTSTP, ctrlZ);

    // Set signal handlers for child processes
    signal(SIGCHLD, terminatedChildHandler);
    
    
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

        } else if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
            jobsCommand();

        } else if (strcmp(line->commands[0].argv[0], "bg") == 0) {
            bgCommand(line->commands[0].argv[1]);

        } else {
            externalCommand(line, buffer);
        }
    }

    // Free memory
    for (i = 0; i < MAX_COMMANDS; i++) {
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

    // Handle bg command
    if (strcmp(line->commands[0].argv[0], "bg") == 0) {
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
 * Executes the jobs command
 */
void jobsCommand() {
    int i, count = 0;
    char * status = "Running";

    // Sort jobs by id
    sortJobsById(jobs);
    
    for (i = 0; i < MAX_COMMANDS; i++) {
        if (jobs[i]->id != -1) {
            count++;
            if (jobs[i]->status == 0) status = "Stopped";
            else status = "Running";

            fprintf(stdout, "[%d] %s\t %s\n", count, status, jobs[i]->command);
        }

    }
}

/**
 * Executes the bg command
 * 
 * @param job_id Job ID to execute
*/
void bgCommand(char * job_id) {
    int i, j, id;
    pid_t pid;

    // Selecte target job_id
    if (job_id == NULL) id = stoppedJobs[lastStoppedJob];
    else id = atoi(job_id);
    
    // Search for the job by id and send SIGCONT to all processes
    for (i = 0; i < MAX_COMMANDS; i++) {
        if (jobs[i]->id == id) {
            jobs[i]->status = 1;
            jobs[i]->background = 1;

            for (j = 0; j < jobs[i]->line->ncommands; j++) {
                pid = jobs[i]->pids[j];
                kill(pid, SIGCONT);
            }
        }
    }
}


/**
 * Executes an external command from a parsed line
 * 
 * @param line Parsed line to execute
 * @return 0 if successful, -1 if failed
 */
int externalCommand(tline * line, char* command) {
    int i, current;
    int status;
    pid_t pid;

    // Add job to the jobs array
    current = addJob(line, command);

    // Check error when adding job
    if (current == -1) return -1;

    // Update background jobs count and print job id
    if (line->background == 1) {
        bgJobs++;
        fprintf(stdout, "[%d] %d\n", bgJobs, jobs[current]->id);
    }


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
        jobs[current]->status = 1;

        if (pid == 0) {
            // Custom Signals
            signal(SIGTSTP, SIG_DFL);
            signal(SIGINT, SIG_DFL);

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

    // Wait for children
    for (i = 0; i < line->ncommands; i++) {
        pid = jobs[current]->pids[i];

        if (line->background == 0) {
            waitpid(pid, &status, WUNTRACED);
        } else {
            waitpid(pid, &status, WNOHANG);
        }

        // Check if the process was stopped
        if (WIFSTOPPED(status)) {
            jobs[current]->status = 0;
        }

        // Check if the process was terminated or killed
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            jobs[current]->status = -1;
        }
    }
    
    return 0;
}

/**
 * Initializes a job structure
 * 
 * @param job Job to initialize
 */
void initializeJob(tjob * job) {
    job->id = -1;
    job->status = -1;
    job->line = NULL;
    job->pids = (pid_t *) malloc(sizeof(pid_t));
    job->pipes = (int **) malloc(sizeof(int *));
}

/*
 * Adds a job to the jobs array
 *
 * @param line Parsed line to add
 * @param command Command string
 * @return Index of the job in the array, -1 if failed
 */
int addJob(tline * line, char * command) {
    int i, j, updated = 0;

    for (i = 0; i < MAX_COMMANDS; i++) {
        if (jobs[i]->id == -1) {
            count++;

            jobs[i]->id = count;
            jobs[i]->line = line;
            jobs[i]->command = strdup(command);
            jobs[i]->status = 1;
            jobs[i]->pids = (pid_t *) realloc(jobs[i]->pids, sizeof(pid_t) * line->ncommands);
            jobs[i]->pipes = (int **) realloc(jobs[i]->pipes, sizeof(int *) * (line->ncommands - 1));
            jobs[i]->background = line->background;

            // Allocate memory for pipes
            for (j = 0; j < line->ncommands - 1; j++) {
                jobs[i]->pipes[j] = (int *) malloc(sizeof(int) * 2);
            }

            updated = 1;
            break;
        }
    }

    // Return error if no slots are available
    if (updated == 0) {
        fprintf(stderr, "Error: Maximum number of commands reached\n");
        return -1;
    }

    return i;
}

// ===========================[ Signal Handlers ]==========================

/**
 * Handles the SIGINT signal (Ctrl+C)
 * 
 * @param sig Signal number
 */
void ctrlC(int sig) {
    int i, runningJobIndex;
    pid_t pid;

    // Get the index of the running job
    runningJobIndex = getRunningJobIndex();

    // Return if no job is running
    if (runningJobIndex == -1) return;

    if (DEBUG_MODE) fprintf(stdout, "Terminating job [%d] - %s\n", jobs[runningJobIndex]->id, jobs[runningJobIndex]->command);

    // Send SIGINT to all processes in the job
    for (i = 0; i < jobs[runningJobIndex]->line->ncommands; i++) {
        pid = jobs[runningJobIndex]->pids[i];
        fprintf(stdout, "Sending SIGINT to PID: %d\n", pid);
        kill(pid, SIGINT);
    }

    // Print message
    fprintf(stdout, "Killed [%d]\t %s\n", jobs[runningJobIndex]->id, jobs[runningJobIndex]->command);
}

/**
 * Handles the SIGTSTP signal (Ctrl+Z)
 * 
 * @param sig Signal number
 */
void ctrlZ(int sig){
    int i;
    int runningJobIndex = -1;
    pid_t pid;

    // Get the index of the running job
    runningJobIndex = getRunningJobIndex();

    // Return if no job is running
    if (runningJobIndex == -1) return;

    // Update stopped job list
    lastStoppedJob++;
    for (i = 0; i < MAX_COMMANDS; i++) {
        if (stoppedJobs[i] == -1) {
            stoppedJobs[i] = runningJobIndex;
            break;
        }
    }

    printf("%d", runningJobIndex);
    
    // Print stopped job
    fprintf(stdout, "\n[%d]+ Stopped\t %s\n", count, jobs[runningJobIndex]->command);

    // Update stopped jobs array
    for (i = 0; i < MAX_COMMANDS; i++) {
        if (stoppedJobs[i] == -1) {
            stoppedJobs[i] = runningJobIndex;
            break;
        }
    }

    // Send SIGTSTP to all processes in the job
    for (i = 0 ; i < jobs[runningJobIndex]->line->ncommands; i++) {
        pid = jobs[runningJobIndex]->pids[i];
        kill(pid, SIGTSTP);
    }
}

/**
 * Handles the SIGCHLD signal (Child terminated)
 * 
 * @param sig Signal number
 */
void terminatedChildHandler(int sig) {
    int i, j, status;
    int all_terminated;
    pid_t pid;

    // Debug message
    if (DEBUG_MODE) fprintf(stdout, "SIGCHLD received\n");

    // Check for terminated jobs
    for (i = 0; i < MAX_COMMANDS; i++) {

        // Initialize flag for each job
        all_terminated = 1;

        if (jobs[i]->id != -1) {
            if (jobs[i]->background == 1) continue;

            for (j = 0; j < jobs[i]->line->ncommands; j++) {
                pid = jobs[i]->pids[j];

                // Check if the process has terminated
                if (waitpid(pid, &status, WNOHANG) == 0) {
                    all_terminated = 0;
                }
            }

            if (all_terminated) {
                // Debug message
                if (DEBUG_MODE) fprintf(stdout, "All child processes for job [%d] have terminated.\n", jobs[i]->id);

                // Update background jobs count if the job was running in the background
                if (jobs[i]->background == 1) bgJobs--;

                // Reset job so it can be used again
                jobs[i]->id = -1;
                jobs[i]->status = -1;
            }
        }
    }
}

// =============================[ Utilities ]==============================

/**
 * Searches for the foreground job running at the moment.
 * 
 * @return Index of the job in the jobs array, -1 if no job is running
*/
int getRunningJobIndex() {
    int i;

    for (i = 0; i < MAX_COMMANDS; i++) {
        if (jobs[i]->id != -1) {
            // Skip background jobs and return if the job is running
            if (jobs[i]->background == 0) {
                if (jobs[i]->status == 1) return i;
            }
        }
    }

    return -1;
}

void sortJobsById(tjob * jobs[]){
    qsort(jobs, MAX_COMMANDS, sizeof(tjob *), compareJobs);
}

int compareJobs(const void * a, const void * b) {
    tjob * jobA = *(tjob **) a;
    tjob * jobB = *(tjob **) b;

    // Jobs with id -1 are considered greater than any other job
    if (jobA->id == -1) return 1;
    if (jobB->id == -1) return -1;

    return jobA->id - jobB->id;
}