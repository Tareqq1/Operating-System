#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define MEMORY_SIZE 60
#define MAX_VARIABLES_PER_PROCESS 3
#define MAX_LINE_LENGTH 100
#define MAX_PROCESSES 10
#define TIME_QUANTUM 1

// Global variables for mutexes
int file_mutex = 1;
int user_input_mutex = 1;
int screen_output_mutex = 1;

// Total memory storage
char memory[MEMORY_SIZE][MAX_LINE_LENGTH];

// Structure to represent a Process Control Block (PCB)
typedef struct {
    int process_id;
    int process_state;
    int program_counter;
    int memory_lower_bound;
    int memory_upper_bound;
    int cycles_remaining; // Cycles remaining in the current time quantum
    char waiting_for_resource[MAX_LINE_LENGTH]; // Resource the process is waiting for
} PCB;

// Structure to represent a Process
typedef struct {
    char instructions[MEMORY_SIZE][MAX_LINE_LENGTH];
    char variables[MAX_VARIABLES_PER_PROCESS][MAX_LINE_LENGTH];
    PCB pcb;
    int arrival_time;
} Process;

// Define Process State
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    FINISHED
} ProcessState;

// Queue for process management
typedef struct {
    Process *processes[MAX_PROCESSES];
    int front;
    int rear;
    int size;
} ProcessQueue;

// Function to initialize the queue
void initQueue(ProcessQueue *queue) {
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
}

// Function to check if the queue is empty
bool isQueueEmpty(ProcessQueue *queue) {
    return queue->size == 0;
}

// Function to check if the queue is full
bool isQueueFull(ProcessQueue *queue) {
    return queue->size == MAX_PROCESSES;
}

// Function to enqueue a process
void enqueue(ProcessQueue *queue, Process *process) {
    if (!isQueueFull(queue)) {
        queue->rear = (queue->rear + 1) % MAX_PROCESSES;
        queue->processes[queue->rear] = process;
        queue->size++;
    }
}

// Function to dequeue a process
Process* dequeue(ProcessQueue *queue) {
    if (!isQueueEmpty(queue)) {
        Process *process = queue->processes[queue->front];
        queue->front = (queue->front + 1) % MAX_PROCESSES;
        queue->size--;
        return process;
    }
    return NULL;
}

// Global operating system state
ProcessQueue readyQueue; // Ready queue for processes
ProcessQueue blockedQueue; // Blocked queue for processes
ProcessQueue storageQueue; // Storage unit for processes
int clockCycles = 0; // Global clock cycle counter
int next_process_id = 1;

// Function to allocate memory for a process
void allocateMemory(Process *process, int memory_lower_bound, int memory_upper_bound) {
    process->pcb.memory_lower_bound = memory_lower_bound;
    process->pcb.memory_upper_bound = memory_upper_bound;
}

// Function to store instructions in memory for a process
void storeInstructions(Process *process, char *instruction, int instruction_index) {
    strcpy(process->instructions[instruction_index], instruction);
}

// Function to store variables in memory for a process
void storeVariables(Process *process, char *variable, char *value) {
    for (int i = 0; i < MAX_VARIABLES_PER_PROCESS; i++) {
        if (process->variables[i][0] == '\0' || strncmp(process->variables[i], variable, strlen(variable)) == 0) {
            snprintf(process->variables[i], sizeof(process->variables[i]), "%s=%s", variable, value);
            break;
        }
    }
}

char* retrieveVariable(Process *process, const char *variable_name) {
    for (int i = 0; i < MAX_VARIABLES_PER_PROCESS; i++) {
        char *var_value_pair = process->variables[i];
        if (var_value_pair != NULL && var_value_pair[0] != '\0') {
            char *var = strtok(var_value_pair, "=");
            if (strcmp(var, variable_name) == 0) {
                char *val = strtok(NULL, "=");
                return val;
            }
        }
    }
    return NULL;
}

void executeAssign(Process *process, char *variable, char *value) {
    if (strcmp(value, "input") == 0) {
        printf("Please enter a value for variable %s: ", variable);
        fgets(value, MAX_LINE_LENGTH, stdin); // Read input as a string
        value[strcspn(value, "\n")] = '\0'; // Remove newline character
    } else if (strncmp(value, "readFile", 8) == 0) {
        // Handle readFile instruction
        char *fileVar = value + 9; // Skip "readFile " to get the variable name
        char *filename = retrieveVariable(process, fileVar);
        if (filename != NULL) {
            FILE *file = fopen(filename, "r");
            if (file == NULL) {
                perror("Error opening file");
                printf("Error opening file: %s\n", filename);
                exit(EXIT_FAILURE);
            }
            char fileData[MAX_LINE_LENGTH];
            if (fgets(fileData, sizeof(fileData), file) != NULL) {
                fileData[strcspn(fileData, "\n")] = '\0';
                strcpy(value, fileData);
            }
            fclose(file);
        } else {
            printf("Filename variable '%s' not found.\n", fileVar);
            return;
        }
    }
    storeVariables(process, variable, value);
}

void executeWriteFile(Process *process, const char *filename_variable, const char *data_variable) {
    char *filename = retrieveVariable(process, filename_variable);
    char *data = retrieveVariable(process, data_variable);
    if (filename != NULL && data != NULL) {
        printf("Creating file: %s\n", filename); // Print the filename
        FILE *file = fopen(filename, "w"); // "w" mode creates the file if it doesn't exist
        if (file == NULL) {
            perror("Error opening file");
            printf("Error opening file: %s\n", filename);
            exit(EXIT_FAILURE);
        }
        if (fprintf(file, "%s", data) < 0) {
            perror("Error writing to file");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        if (fclose(file) != 0) {
            perror("Error closing file");
            exit(EXIT_FAILURE);
        }
    } else {
        printf("Error: Invalid filename or data.\n");
    }
}

void executeReadFile(Process *process, const char *filename_variable) {
    char *filename = retrieveVariable(process, filename_variable);
    if (filename != NULL) {
        FILE *file = fopen(filename, "r");
        if (file == NULL) {
            perror("Error opening file");
            printf("Error opening file: %s\n", filename);
            exit(EXIT_FAILURE);
        }
        char data[MAX_LINE_LENGTH];
        while (fgets(data, sizeof(data), file)) {
            printf("%s", data);
        }
        fclose(file);
    } else {
        printf("Filename variable '%s' not found.\n", filename_variable);
    }
}

void executePrint(Process *process, char *variable) {
    char *value = retrieveVariable(process, variable);
    if (value != NULL) {
        printf("%s\n", value);
    } else {
        printf("Variable '%s' not found.\n", variable);
    }
}

// Function to block a process
void blockProcess(Process *process, const char *resource) {
    process->pcb.process_state = BLOCKED;
    strcpy(process->pcb.waiting_for_resource, resource);
    enqueue(&blockedQueue, process);
}

// Function to unblock processes waiting for a specific resource
void unblockProcesses(const char *resource) {
    int size = blockedQueue.size;
    for (int i = 0; i < size; i++) {
        Process *process = dequeue(&blockedQueue);
        if (strcmp(process->pcb.waiting_for_resource, resource) == 0) {
            process->pcb.process_state = READY;
            enqueue(&readyQueue, process);
        } else {
            enqueue(&blockedQueue, process);
        }
    }
}

// Function to execute semWait instruction
void executeSemWait(Process *process, char *resource) {
    if (strcmp(resource, "userInput") == 0) {
        if (user_input_mutex == 0) {
            blockProcess(process, resource);
        } else {
            user_input_mutex = 0; // Acquire mutex
        }
    } else if (strcmp(resource, "file") == 0) {
        if (file_mutex == 0) {
            blockProcess(process, resource);
        } else {
            file_mutex = 0; // Acquire mutex
        }
    } else if (strcmp(resource, "userOutput") == 0) {
        if (screen_output_mutex == 0) {
            blockProcess(process, resource);
        } else {
            screen_output_mutex = 0; // Acquire mutex
        }
    }
}

// Function to execute semSignal instruction
void executeSemSignal(char *resource) {
    if (strcmp(resource, "userInput") == 0) {
        user_input_mutex = 1; // Release mutex
    } else if (strcmp(resource, "file") == 0) {
        file_mutex = 1; // Release mutex
    } else if (strcmp(resource, "userOutput") == 0) {
        screen_output_mutex = 1; // Release mutex
    }
    unblockProcesses(resource);
}

// Function to execute printFromTo instruction
void executePrintFromTo(Process *process, char *startVar, char *endVar) {
    char *startStr = retrieveVariable(process, startVar);
    char *endStr = retrieveVariable(process, endVar);
    if (startStr != NULL && endStr != NULL) {
        int start = atoi(startStr);
        int end = atoi(endStr);
        for (int i = start; i <= end; i++) {
            printf("%d ", i);
        }
        printf("\n");
    } else {
        printf("Error: Variables not found.\n");
    }
}

void executeProcess(Process *process) {
    process->pcb.process_state = RUNNING;
    char *line = process->instructions[process->pcb.program_counter];
    printf("Executing instruction [%s] from Process %d at clock cycle %d\n", line, process->pcb.process_id, clockCycles);

    // Tokenize the instruction line
    char *instruction = strtok(line, " \n");
    char *arg1 = strtok(NULL, " \n");
    char *arg2 = strtok(NULL, " \n");
    char *arg3 = strtok(NULL, "\n"); // To handle the `assign` case with `readFile`

    if (strcmp(instruction, "print") == 0) {
        executePrint(process, arg1); 
    } else if (strcmp(instruction, "assign") == 0) {
        // Handle the case where `arg2` and `arg3` should be concatenated
        if (arg3 != NULL) {
            char combinedValue[MAX_LINE_LENGTH];
            snprintf(combinedValue, sizeof(combinedValue), "%s %s", arg2, arg3);
            executeAssign(process, arg1, combinedValue);
        } else {
            executeAssign(process, arg1, arg2);
        }
    } else if (strcmp(instruction, "writeFile") == 0) {
        executeWriteFile(process, arg1, arg2);
    } else if (strcmp(instruction, "readFile") == 0) {
        executeReadFile(process, arg1); 
    } else if (strcmp(instruction, "printFromTo") == 0) {
        executePrintFromTo(process, arg1, arg2);
    } else if (strcmp(instruction, "semWait") == 0) {
        executeSemWait(process, arg1);
    } else if (strcmp(instruction, "semSignal") == 0) {
        executeSemSignal(arg1);
    } else {
        printf("Unknown instruction: %s\n", instruction);
    }

    // Remove executed instruction from the storage unit
    if (strlen(process->instructions[process->pcb.program_counter]) > 0) {
        strcpy(process->instructions[process->pcb.program_counter], ""); // Clear the executed instruction
    }

    process->pcb.program_counter++;
    process->pcb.cycles_remaining--;

    if (process->pcb.process_state == RUNNING) {
        if (process->pcb.program_counter >= MEMORY_SIZE || strlen(process->instructions[process->pcb.program_counter]) == 0) {
            process->pcb.process_state = FINISHED;
        } else if (process->pcb.cycles_remaining == 0) {
            // Time quantum expired, move to end of ready queue
            process->pcb.process_state = READY;
            process->pcb.cycles_remaining = TIME_QUANTUM;
            enqueue(&readyQueue, process);
        } else {
            process->pcb.process_state = READY;
        }
    }
}

// Function to parse and execute instructions from a program file
bool executeProgram(char *filename, Process *process) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        printf("Error opening file: %s\n", filename); // Enhanced error message
        return false;
    }

    char line[MAX_LINE_LENGTH];
    int instruction_index = 0;
    while (fgets(line, sizeof(line), file)) {
        storeInstructions(process, line, instruction_index);
        instruction_index++;
    }

    fclose(file);
    return true;
}

void printQueue(const char *queueName, ProcessQueue *queue) {
    printf("%s Queue:\n", queueName);
    printf("+------------+-----------------------+\n");
    printf("| Process ID | Current Instruction   |\n");
    printf("+------------+-----------------------+\n");
    for (int i = queue->front, count = 0; count < queue->size; i = (i + 1) % MAX_PROCESSES, count++) {
        printf("| %-10d | %-21s |\n", queue->processes[i]->pcb.process_id, queue->processes[i]->instructions[queue->processes[i]->pcb.program_counter]);
    }
    printf("+------------+-----------------------+\n");
}

void printStorageUnit(ProcessQueue *queue) {
    printf("Memory Contents:\n");
    printf("+------------+-----------------------+\n");
    printf("| Process ID | Instructions          |\n");
    printf("+------------+-----------------------+\n");
    for (int i = queue->front, count = 0; count < queue->size; i = (i + 1) % MAX_PROCESSES, count++) {
        Process *p = queue->processes[i];
        for (int j = 0; j < MEMORY_SIZE && strlen(p->instructions[j]) > 0; j++) {
            if (strlen(p->instructions[j]) > 0) { // Only print non-empty instructions
                printf("| %-10d | %-21s |\n", p->pcb.process_id, p->instructions[j]);
            }
        }
    }
    printf("+------------+-----------------------+\n");
}

void enqueueProcessToReadyQueue(Process *process) {
    if (!isQueueFull(&readyQueue)) {
        readyQueue.rear = (readyQueue.rear + 1) % MAX_PROCESSES;
        readyQueue.processes[readyQueue.rear] = process;
        readyQueue.size++;
        printf("Process %d has arrived at clock cycle %d\n", process->pcb.process_id, clockCycles);
        printf("Ready Queue:\n");
        printf("+------------+-----------------------+\n");
        printf("| Process ID | Current Instruction   |\n");
        printf("+------------+-----------------------+\n");
        printf("| %-10d | %-21s |\n", process->pcb.process_id, process->instructions[process->pcb.program_counter]);
        printf("+------------+-----------------------+\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <arrival_time1> <program_file1> [<arrival_time2> <program_file2> ...]\n", argv[0]);
        return 1;
    }

    // Initialize the ready queue, blocked queue, and storage unit
    initQueue(&readyQueue);
    initQueue(&blockedQueue);
    initQueue(&storageQueue);

    // Create an array to store process programs and arrival times
    Process *processes[MAX_PROCESSES];
    int arrival_times[MAX_PROCESSES];
    int process_count = 0;

    // Parse the program files and arrival times
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            printf("Error: Missing program file for arrival time %s\n", argv[i]);
            return 1;
        }

        int arrival_time = atoi(argv[i]);
        char *filename = argv[i + 1];

        Process *process = (Process *)malloc(sizeof(Process));
        process->pcb.process_id = next_process_id++;
        process->pcb.process_state = READY;
        process->pcb.program_counter = 0;
        process->pcb.cycles_remaining = TIME_QUANTUM;
        process->arrival_time = arrival_time;
        allocateMemory(process, 0, MEMORY_SIZE - 1);
        if (!executeProgram(filename, process)) {
            // If loading the program fails, free the allocated memory and break out of the loop
            free(process);
            printf("Failed to load program: %s\n", filename);
            return 1;
        }

        processes[process_count] = process;
        arrival_times[process_count++] = arrival_time;
    }

    Process *executedProcess = NULL; // Track the currently executed process for removal
    
    // Execute processes from the ready queue
    while (true) {
        bool anyProcessActive = false;

        // Check for process arrivals
        for (int i = 0; i < process_count; i++) {
            if (arrival_times[i] == clockCycles) {
                enqueue(&storageQueue, processes[i]); // Enqueue arriving process into the storage unit
                enqueueProcessToReadyQueue(processes[i]);
            }
        }

        // Print the status of all queues
        printQueue("Ready", &readyQueue);
        printQueue("Blocked", &blockedQueue);
        printStorageUnit(&storageQueue);

        // Execute processes in the ready queue
        while (!isQueueEmpty(&readyQueue)) {
            printQueue("Ready", &readyQueue); // Print ready queue before executing each process
            printQueue("Blocked", &blockedQueue); // Print blocked queue before executing each process
            printStorageUnit(&storageQueue); // Print storage unit each cycle

            Process *process = dequeue(&readyQueue);
            if (process->pcb.process_state == READY) {
                anyProcessActive = true;
                                executedProcess = process;
                executeProcess(process);
                clockCycles++;
                
                // Check if new processes arrive during current execution
                for (int j = 0; j < process_count; j++) {
                    if (arrival_times[j] == clockCycles) {
                        enqueue(&storageQueue, processes[j]); // Enqueue arriving process into the storage unit
                        enqueueProcessToReadyQueue(processes[j]);
                    }
                }

                // Remove the executed instruction from the storage unit
                if (executedProcess != NULL) {
                    int size = storageQueue.size;
                    ProcessQueue tempQueue;
                    initQueue(&tempQueue);

                    for (int k = 0; k < size; k++) {
                        Process *storageProcess = dequeue(&storageQueue);
                        if (storageProcess->pcb.process_id == executedProcess->pcb.process_id) {
                            strcpy(storageProcess->instructions[executedProcess->pcb.program_counter - 1], ""); // Clear the executed instruction
                        }
                        enqueue(&tempQueue, storageProcess);
                    }

                    // Transfer back to storageQueue to keep the original order
                    for (int k = 0; k < size; k++) {
                        enqueue(&storageQueue, dequeue(&tempQueue));
                    }

                    executedProcess = NULL;
                }

                if (process->pcb.process_state == READY) {
                    enqueue(&readyQueue, process);
                } else if (process->pcb.process_state == BLOCKED || process->pcb.process_state == FINISHED) {
                    if (process->pcb.process_state == FINISHED) {
                        printf("Process %d has finished execution.\n", process->pcb.process_id);
                        // Remove the finished process from the storage unit
                        int size = storageQueue.size;
                        ProcessQueue tempQueue;
                        initQueue(&tempQueue);

                        for (int k = 0; k < size; k++) {
                            Process *storageProcess = dequeue(&storageQueue);
                            if (storageProcess->pcb.process_id != process->pcb.process_id) {
                                enqueue(&tempQueue, storageProcess);
                            } else {
                                free(storageProcess);
                            }
                        }

                        // Transfer back to storageQueue to keep the original order
                        for (int k = 0; k < tempQueue.size; k++) {
                            enqueue(&storageQueue, dequeue(&tempQueue));
                        }
                    }
                }
            }
        }

        // Print the status of all queues after processing
        printQueue("Ready", &readyQueue);
        printQueue("Blocked", &blockedQueue);
        printStorageUnit(&storageQueue);

        // Break the loop if no processes are active and no processes are in the blocked queue
        if (!anyProcessActive && isQueueEmpty(&blockedQueue)) {
            break;
        }

        // Increment clock cycles for idle waiting
        if (!anyProcessActive) {
            clockCycles++;
        }
    }

    printf("All processes have finished execution.\n");
    return 0;
}

