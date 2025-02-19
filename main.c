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
 * @param background: Background flag (0: Foreground, 1: Background)
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
void umaskCommand(tline * line);
void jobsCommand(tline * line);
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
int count = 0, bgJobs = 0, stoppedJobs = 0;
int lastStoppedJobId = -1;

// ==============================[ Main ]=============================
int main(int argc, char * argv[]) {
    tline * line;
    int i, allowExit = 0;
    int selectedJob = -1;
    char buffer[MAX_LINE];

    // Initialize jobs
    for (i = 0; i < MAX_COMMANDS; i++) {
        jobs[i] = (tjob *) malloc(sizeof(tjob));

        // Check for malloc errors
        if (jobs[i] == NULL) {
            fprintf(stderr, "Error: malloc failed\n");
            exit(EXIT_FAILURE);
        }

        initializeJob(jobs[i]);
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

        // Check for user input, errors or empty commands
        selectedJob = isInputOk(line);

        if (selectedJob == -1) {
            fprintf(stderr, "Command Error: Command not found\n");
            continue;
        } else if (selectedJob == 0) {
            continue;
        }

        // Handle commands
        if (selectedJob == 3) {
            if (allowExit == 1) break;

            // If there are stopped jobs, warn the user
            if (stoppedJobs > 0) {
                fprintf(stdout, "There are stopped jobs.\n");
                allowExit = 1;
            } else break;
        }

        // Execute command
        else if (selectedJob == 1) externalCommand(line, buffer);
        else if (selectedJob == 2) changeDirectory(line->commands[0].argv[1]);
        else if (selectedJob == 4) jobsCommand(line);
        else if (selectedJob == 5) umaskCommand(line);
        else if (selectedJob == 6) bgCommand(line->commands[0].argv[1]);
    }

    // Free memory
    for (i = 0; i < MAX_COMMANDS; i++) {
        free(jobs[i]->pids);
        free(jobs[i]->pipes);
        free(jobs[i]->command);
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
        customPrompt = "\033[1;32m%s@msh\033[0m: \033[1;34m~%s\033[0m $> ";
    } else {
        customPrompt = "\033[1;32m%s@msh\033[0m: \033[1;34m%s\033[0m $> ";
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
 *         2 if the command is cd, 3 if the command is exit, 4 if the command is jobs,
 *         5 if the command is umask, 6 if the command is bg
 */
int isInputOk(tline * line) {
    int i;

    if (line->ncommands == 0) {
        return 0;
    }

    // Handle cd command
    if (line->commands->filename == NULL && strcmp(line->commands[0].argv[0], "cd") == 0) {
        return 2;
    }

    // Handle exit command
    if (line->commands->filename == NULL && strcmp(line->commands[0].argv[0], "exit") == 0) {
        return 3;
    }

    // Handle jobs command
    if (line->commands->filename == NULL && strcmp(line->commands[0].argv[0], "jobs") == 0) {
        return 4;
    }

    // Handle umask command
    if (line->commands->filename == NULL && strcmp(line->commands[0].argv[0], "umask") == 0) {
        return 5;
    }

    // Handle bg command
    if (line->commands->filename == NULL && strcmp(line->commands[0].argv[0], "bg") == 0) {
        return 6;
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
    int res;
    char * dir;

    if (path == NULL) {
        dir = getenv("HOME");
    } else {
        dir = path;
    }

    res = chdir(dir);

    if (res == -1) {
        fprintf(stderr, "Error: Directory not found\n");
    }

    return res;
}

/**
 * Executes the umask command
 * @param line Parsed line to execute
 */
void umaskCommand(tline * line) {
    mode_t mode;
    char * mask = NULL;

    // Redirect IO to files if needed
    if (line->redirect_input != NULL) freopen(line->redirect_input, "r", stdin);
    if (line->redirect_output != NULL) freopen(line->redirect_output, "w", stdout);
    if (line->redirect_error != NULL) freopen(line->redirect_error, "w", stderr);

    // Get mask if provided in the command
    if (line->commands[0].argc > 1) {
        mask = line->commands[0].argv[1];
    }

    // Get mask from stdin if not provided and handle errors
    if (mask == NULL && line->redirect_input != NULL) {
        mask = (char *) malloc(5);

        // Check for malloc errors
        if (mask == NULL) {
            fprintf(stderr, "Error: malloc failed\n");
            return;
        }

        // Get mask from stdin
        if (mask != NULL && fgets(mask, 5, stdin) == NULL) {
            free(mask);
            mask = NULL;
        }
    }

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

    // Reset redirection to terminal (/dev/tty)
    if (line->redirect_input != NULL) freopen("/dev/tty", "r", stdin);
    if (line->redirect_output != NULL) freopen("/dev/tty", "w", stdout);
    if (line->redirect_error != NULL) freopen("/dev/tty", "w", stderr);

    // Free allocated memory
    if (mask != NULL && line->redirect_input != NULL) free(mask);
}

/**
 * Executes the jobs command
 * 
 * @param line Parsed line to execute
 */
void jobsCommand(tline * line) {
    int i, count = 0;
    char * outputFormat;

    // Redirect IO to files if needed
    if (line->redirect_input != NULL) freopen(line->redirect_input, "r", stdin);
    if (line->redirect_output != NULL) freopen(line->redirect_output, "w", stdout);
    if (line->redirect_error != NULL) freopen(line->redirect_error, "w", stderr);

    // Sort jobs by id
    sortJobsById(jobs);
    
    for (i = 0; i < MAX_COMMANDS; i++) {
        if (jobs[i]->id != -1) {
            count++;

            // Assign output format
            if (jobs[i]->status == 0) outputFormat = "Stopped";
            else outputFormat = "Running";                

            fprintf(stdout, "[%d]  %s\t\t %s", count, outputFormat, jobs[i]->command);
        }
    }

    // Reset redirection to terminal (/dev/tty)
    if (line->redirect_input != NULL) freopen("/dev/tty", "r", stdin);
    if (line->redirect_output != NULL) freopen("/dev/tty", "w", stdout);
    if (line->redirect_error != NULL) freopen("/dev/tty", "w", stderr);
}

/**
 * Executes the bg command
 * 
 * @param job_id Job ID to execute
*/
void bgCommand(char * job_id) {
    int i, id;
    int len, found = 0;

    // Sort jobs by id
    sortJobsById(jobs);

    if (job_id == NULL) {
        // Search for the job by id and send SIGCONT to all processes
        for (i = 0; i < MAX_COMMANDS; i++) {
            if (jobs[i]->id == lastStoppedJobId) {
                found = 1;
                break;
            }
        }

    } else {
        i = atoi(job_id) - 1;
        found = 1;
    }

    // Return if the job was not found
    if (found == 0) return;

    // Update background jobs and stopped jobs count
    bgJobs++;
    stoppedJobs--;

    // Get job id and set new status and background flag
    id = jobs[i]->id;
    jobs[i]->status = 1;
    jobs[i]->background = 1;

    // Send SIGCONT to all processes in the job's process group
    killpg(jobs[i]->pids[0], SIGCONT);

    // Add '&' to the command string
    len = strlen(jobs[i]->command);
    jobs[i]->command = (char *) realloc(jobs[i]->command, len + 2);
    jobs[i]->command[len - 1] = ' ';
    jobs[i]->command[len] = '&';
    jobs[i]->command[len + 1] = '\n';

    // Print message
    fprintf(stdout, "[%d]+ %s", id, jobs[i]->command);
}


/**
 * Executes an external command from a parsed line
 * 
 * @param line Parsed line to execute
 * @param command Command string
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

        if (DEBUG_MODE) fprintf(stdout, "PID: %d\n", pid);

        if (pid == 0) {
            // Set the child process group ID to its own PID
            setpgid(0, 0);

            // Set default signal handlers
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

        } else {
            if (i == 0) setpgid(pid, pid);
            else setpgid(pid, jobs[current]->pids[0]);
            
            jobs[current]->pids[i] = pid;
            jobs[current]->status = 1;
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

            // Check if the process was stopped
            if (WIFSTOPPED(status)) {
                jobs[current]->status = 0;
            }

            // Check if the process was terminated or killed
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                jobs[current]->status = -1;
            }
        } else {
            waitpid(pid, &status, WNOHANG);
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

    // Check for malloc errors
    if (job->pids == NULL || job->pipes == NULL) {
        fprintf(stderr, "Error: malloc failed\n");
        exit(EXIT_FAILURE);
    }
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
    int runningJobIndex = -1;

    // Get the index of the running job
    runningJobIndex = getRunningJobIndex();

    // Return if no job is running
    if (runningJobIndex == -1) return;

    // Send SIGINT to all processes in the job's process group
    if (DEBUG_MODE) fprintf(stdout, "Sending SIGINT to process group: %d\n", jobs[runningJobIndex]->pids[0]);
    killpg(jobs[runningJobIndex]->pids[0], SIGINT);


    // Print message
    if (DEBUG_MODE) fprintf(stdout, "Killed [%d]\t %s\n", jobs[runningJobIndex]->id, jobs[runningJobIndex]->command);
}

/**
 * Handles the SIGTSTP signal (Ctrl+Z)
 * 
 * @param sig Signal number
 */
void ctrlZ(int sig){
    int runningJobIndex = -1;

    // Get the index of the running job
    runningJobIndex = getRunningJobIndex();

    // Return if no job is running
    if (runningJobIndex == -1) return;

    // Send SIGTSTP to all processes in the job's process group
    if (DEBUG_MODE) fprintf(stdout, "Sending SIGTSTP to process group: %d\n", jobs[runningJobIndex]->pids[0]);
    killpg(jobs[runningJobIndex]->pids[0], SIGTSTP);

    // Update stopped jobs count and last stopped job id
    stoppedJobs++;
    lastStoppedJobId = jobs[runningJobIndex]->id;
    
    // Print stopped job
    fprintf(stdout, "\n[%d]+  Stopped\t\t %s", count, jobs[runningJobIndex]->command);
}

/**
 * Handles the SIGCHLD signal (Child terminated)
 * 
 * @param sig Signal number
 */
void terminatedChildHandler(int sig) {
    int i, j;
    int all_terminated;
    pid_t pid;

    // Check for terminated jobs
    for (i = 0; i < MAX_COMMANDS; i++) {

        // Initialize flag for each job
        all_terminated = 1;

        if (jobs[i]->id != -1) {
            for (j = 0; j < jobs[i]->line->ncommands; j++) {
                pid = jobs[i]->pids[j];

                // Check if the process has terminated
                if (waitpid(pid, NULL, WNOHANG) == 0) {
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

/**
 * Sorts the jobs array by job id
 * 
 * @param jobs Array of jobs to sort
*/
void sortJobsById(tjob * jobs[]){
    qsort(jobs, MAX_COMMANDS, sizeof(tjob *), compareJobs);
}

/**
 * Compares two jobs by their id
 * 
 * @param a First job
 * @param b Second job
 * @return -1 if a < b, 1 if a > b
*/
int compareJobs(const void * a, const void * b) {
    tjob * jobA = *(tjob **) a;
    tjob * jobB = *(tjob **) b;

    // Jobs with id -1 are considered greater than any other job
    if (jobA->id == -1) return 1;
    if (jobB->id == -1) return -1;

    return jobA->id - jobB->id;
}