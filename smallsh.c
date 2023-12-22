#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>


//global variables
int foreground_only = 1;
int last_status;


struct user_command{
    char* command;
    char** argument;
    int background;
    int count;
};

// Global variables to track background processes
struct BackgroundProcess {
    pid_t pid;
    int status;
};

struct BackgroundProcess backgroundProcesses[512]; // Adjust the size as needed
int numBackgroundProcesses = 0;

void prompt();

//signals functions
void sigintHandler(int signo){

    printf("Cntrl-C");
    fflush(stdout);

}


void handle_sigint(int signo){
    if (getpid() == getpgrp()) {
        // when the shell recieved SIGINt it should ignore it
        printf("\n SIGINT ignoring...\n");
        fflush(stdout);
        prompt();
        fflush(stdout);
    } else {
        // terminate if child processs recieves a sigint
        printf(" terminating...\n");
        fflush(stdout);
        exit(1);
    }
}

void handle_sigtstp(int signo){
    
    // where SIGSTP is recieved make it not foreground only mode
    foreground_only = !foreground_only;

    if (foreground_only == 1) {
        printf("\nForeground only mode disabled\n");
    } 
    else if(foreground_only == 0) {
        printf("\nForeground only mode enabled\n");
    }
    fflush(stdout);
    prompt();
}

void setup_signal_handlers(){

    //set up signal handler for control C
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("Error setting up SIGINT handler");
        exit(1);
    }

    //set up signal handler for control Z
    if (signal(SIGTSTP, handle_sigtstp) == SIG_ERR) {
        perror("Error setting up SIGTSTP handler");
        exit(1);
    }

}

struct user_command *input_command(char* user_input){


    //create the user struct command and allocate memory to it
    struct user_command *command1 = malloc(sizeof(struct user_command));
    int num_args = 0;

    char* saveptr;
    //splitting user input into tokens
    char *token = strtok_r(user_input, " ", &saveptr);

    //allocate memory for the command and copy the token
    command1->command = calloc(strlen(token)+1,sizeof(char));
    strcpy(command1->command,token);

    command1->argument = NULL;

    //loop to extract arguments fro the input
    while(1){
        token = strtok_r(NULL, " ", &saveptr);
        if (token != NULL)
        {
            //allocate memory for the argument string in the user_command struct
            char* user_arg = calloc(strlen(token)+1,sizeof(char));

            //if foreground only mode is 1 ignore the &
            if ((foreground_only == 0) && (strcmp(token, "&") == 0))
            {
                continue;
            }

            strcpy(user_arg, token);

            //reallocate memory for the argument array to accomodate the new argument
            command1->argument = realloc(command1->argument, (num_args + 1) * sizeof(char*));
            command1->argument[num_args] = user_arg;

            //add to number of arguments
            num_args++;
        }
        else{
            break;
        }
    }

    // Check if the last argument is "&"
    if (foreground_only != 0 && num_args > 0 && strcmp(command1->argument[num_args - 1], "&") == 0 && strcmp(command1->command, "cd") != 0 && strcmp(command1->command, "exit") != 0 && strcmp(command1->command, "status") != 0) {
        // Set the background flag to 1
        command1->background = 1;
        // Remove the "&" from the arguments
        free(command1->argument[num_args - 1]);
        num_args--;
    } else {
        // Set the background flag to 0
        command1->background = 0;
    }

    command1->count = num_args;

    //return the filled command struct
    return command1;

}



// void prompt(){

//     pid_t process_id;
//     process_id = getpid(); // Call getpid to get the current process ID
//     // printf("Process ID: %d\n", process_id);
//     fflush(stdout);
//     printf(":");
//     fflush(stdout);

//     return;
// }

void checkBackgroundProcesses();

void prompt() {
    // Check for completed background processes before prompting
    checkBackgroundProcesses();

    //get pid
    pid_t process_id = getpid();

    //flush the stdout
    fflush(stdout);
    printf(":");
    fflush(stdout);
}


int handleBlankAndComment(char* user_prompt){
    
    //if its a comment return 1
    if (isspace(user_prompt[0]) || user_prompt[0] == '#')
    {
        return 1;
    }

    //else returns 0
    return 0;
}




char* expandDollas(char* input){

    //allocate memory for the expanded input
    char* exp_input = malloc(strlen(input) * 2); 
    int found = 0;
    int j = 0;

    //check for the $$ pattern
    for (int i = 0; i < strlen(input); i++) {
        if (input[i] == '$' && input[i + 1] == '$'){

            //convert the current process ID to a string
            int pid = getpid();
            char pid_str[20];
            sprintf(pid_str, "%d", pid);

            //copy the process ID string to the expanded input above
            strcpy(&exp_input[j], pid_str);
            j += strlen(pid_str);

            //skip the second $
            i += 1;
            found = 1;
        } else{

            //copt the current character to the expanded input
            exp_input[j] = input[i];
            j++;
        }
    }

    //null-terminate the expanded input
    exp_input[j] = '\0';
    strcpy(input, exp_input);

    // Check if expansion occurred
    if (found == 1)
    {
        return input;
    }

    else{
        return NULL;
    }


}








void change_directory(struct user_command* command) {
    char* new_directory;

    // If no arguments provided, change to the HOME directory
    if (command->argument == NULL || command->argument[0] == NULL) {
        new_directory = getenv("HOME");
    } else {
        new_directory = command->argument[0];
    }

    //if there is no dirrectory with the name then print an error
    if (chdir(new_directory) != 0) {

        perror("cd"); // Print an error message if chdir fails

    //else get the current working directory we transferred into
    } else {
        char current_directory[100000];
        getcwd(current_directory, sizeof(current_directory));
        printf("Current directory: %s\n", current_directory);
        fflush(stdout);
    }
}



void checkBackgroundProcesses() {
    int i;

    //iterate through the list of background processes
    for (i = 0; i < numBackgroundProcesses; ++i) {
        //use waitpit to check the status of background process
        pid_t pid = waitpid(backgroundProcesses[i].pid, &backgroundProcesses[i].status, WNOHANG);

        //check if the process has terminated
        if (pid > 0) {

            //process has terminated
            if (WIFEXITED(backgroundProcesses[i].status)) {
                printf("background pid %d is done: exited normally with exit value %d\n", backgroundProcesses[i].pid, WEXITSTATUS(backgroundProcesses[i].status));
            } else {
                //terminated by a signal
                printf("background pid %d is done: terminated by signal %d\n", backgroundProcesses[i].pid, WTERMSIG(backgroundProcesses[i].status));
            }
            fflush(stdout);


            // Clear the background process information
            numBackgroundProcesses--;

            // Shift the remaining processes to fill the gap
            for (int j = i; j < numBackgroundProcesses; ++j) {
                backgroundProcesses[j] = backgroundProcesses[j + 1];
            }
            // Decrement i to recheck the current index (it now contains a new process)
            --i;  
        }
    }
}






/*
* This is int main
*/
int main()
{   
    //initialize variables
    int comment_or_blank;
    char user_prompt[2048]; 

    //prompt user for their first input
    prompt();
    
    //set up the signal handlers for the programs
    setup_signal_handlers();
    
    // Infinite loop
    while (1) { 

        //get the user input as a string
        fgets(user_prompt, sizeof(user_prompt), stdin);

        //get the expand dollas function return and expand the user input
        char* temp = expandDollas(user_prompt);

        //if $$ was expanded then update the user_prompt
        if (temp != NULL)
        {
            strcpy(user_prompt, temp);
        }

        //remove the newline character from the user input
        size_t command_length = strlen(user_prompt);
        if (command_length > 0 && user_prompt[command_length - 1] == '\n') {
            // Remove the newline character
            user_prompt[command_length - 1] = '\0'; 
        }

        // handle blank and comment
        comment_or_blank = handleBlankAndComment(user_prompt);
        if (comment_or_blank == 1) {
            prompt();
            continue;
        }

        struct user_command* input = input_command(user_prompt);


        //checking for built in commands
        if(strcmp(input->command, "$$") == 0){
            // expand_dollas(user_prompt);
            // prompt();
            // continue;
        }
        else if(strcmp(input->command, "cd") == 0){

            change_directory(input);

        }
        else if(strcmp(input->command, "exit") == 0) {
            // printf("TERMINATEDDDDDDDD");
            killpg(0, 15);
            break;
            
        }else if (strcmp(input->command, "status") == 0) {
            if (input->background) {
                // Check if there are background processes
                if (numBackgroundProcesses > 0) {
                    last_status = backgroundProcesses[numBackgroundProcesses - 1].status;
                }
            }

            printf("Exit Value %d\n", last_status);
            fflush(stdout);
        }
        else {
            // Execute non-built-in commands
            pid_t pid = fork();

            // Set up arguments for execvp
            char* user_args[512];
            user_args[0] = input->command;

            //copy user arguments
            for(int i = 0; i < input->count; i++){
                user_args[i+1] = input->argument[i];
            }

            user_args[input->count+1] = NULL;

            //fork failure
            if (pid == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pid == 0) { 
                //child process
                // Set up file redirection

                // Set SIGINT to default action
                signal(SIGINT, SIG_DFL);  


                int redirect_in = -1;
                int redirect_out = -1;

                //check which way we will be redirecting
                for(int i = 0; i < input->count+1; i++){
                    if(strcmp(user_args[i], ">") == 0){
                        redirect_out = i;
                    }
                    else if(strcmp(user_args[i], "<") == 0){
                        redirect_in = i;
                    }
                }

                //if its redirect out
                if(redirect_out != -1){
    
                    //open the file  
                    int fd = open(user_args[redirect_out + 1], O_WRONLY | O_CREAT, 0660);

                    //error opening the file
                    if (fd == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                    
                    int success_result = dup2(fd, 1);

                    //error the success result
                    if(success_result == -1){
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }

                    // Close the file descriptor after duplicating it
                    close(fd);
                    
                    //set the user args back to NULL
                    user_args[redirect_out] = NULL;
                    user_args[redirect_out +1] = NULL;

                }

                //same thing for redirect in
                if(redirect_in != -1){
                    int fd = open(user_args[redirect_in + 1], O_RDONLY);
                    if (fd == -1) {
                        perror("open");
                        continue;
                    }
                    
                    int success_result = dup2(fd, 0);

                    if(success_result == -1){
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }

                    // Close the file descriptor after duplicating it
                    close(fd);
                    user_args[redirect_in] = NULL;
                    user_args[redirect_in +1] = NULL;
                }

                execvp(user_args[0], user_args);

                // If execvp fails, print an error message
                perror("execvp");

                exit(EXIT_FAILURE);


            } else { 
                // Parent process
                if (input->background) {
                    // Print the background process ID
                    printf("Background process started with PID: %d\n", pid);

                    // Save the background process information
                    backgroundProcesses[numBackgroundProcesses].pid = pid;
                    backgroundProcesses[numBackgroundProcesses].status = 0;  // Initialize status

                    ++numBackgroundProcesses;

                    fflush(stdout);
                    // Do not wait for the background process to complete
                }
                else {
                    
                    // Wait for the foreground process to complete
                    int status;
                    pid_t child_pid = waitpid(pid, &status, 0);

                    if (child_pid == -1) {
                        perror("waitpid");
                        exit(EXIT_FAILURE);
                    }

                    // Check the exit status of the child process
                    if (WIFEXITED(status)) {
                        last_status = WEXITSTATUS(status);
                        
                    }
                    else if (WIFSIGNALED(status)) {
                        int signal_number = WTERMSIG(status);
                        printf("Child process terminated by signal %d: %s\n", signal_number, strsignal(signal_number));
                        fflush(stdout);
                        last_status = signal_number;
                    }
                }
            }
        }
        checkBackgroundProcesses();

        prompt();
    }

    

    return EXIT_SUCCESS;
}