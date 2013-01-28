#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <memory.h>
#include <stdlib.h>
#include <math.h>

#define MAX_LENGTH 256
#define GENERAL_ERROR -255
#define NULL_STRING -1
#define NOT_BUILTIN -2
#define BUILTIN_EXCUTE_ERROR -3
#define REDUNDANT_REDIRECTION -4
#define EMPTY_COMMAND -5
#define EMPTY_REDIRECTION_FILE -6
#define COMMANDLINE_SYNTAX_ERROR -7
#define REDUNDANT_COMMANDS -8

char* error_strings[] = {"SUCCESS", "The string is null", "This comand is not a built-in command", 
                         "Error occurs in executing the command", "Redundant input redirection files",
                         "The command is empty, cannot execute", "No name for redirection file", 
                         "The commandline has a syntax error", "More than one type of commands"};

int eliminate_dup_tab_spaces(char* string);
int eliminate_tab_spaces(char* string);

char* commandline = NULL;
char* command = NULL;
char* input = NULL;

struct output{
    char* file_name;
    //0 for '<', 1 for '<<'
    int append;
    struct output* next;
};

struct output* outputs = NULL;
//utility functions for output linked lists
//add a output file name
int add_output(char* name, int _append){
    
    //check whether the name is null
    eliminate_tab_spaces(name);
    if(strlen(name) == 0){
        return EMPTY_REDIRECTION_FILE;
    }
    struct output* to_add = (struct output*)malloc(sizeof(struct output));
    to_add -> file_name = name;
    to_add -> append = _append;

    //append at the head
    to_add -> next = outputs;
    outputs = to_add;
    
    return 0;
}

//delete all output files
void clear_outputs(){
    //free all structs
    struct output* traverser = outputs;
    while( traverser != NULL){
        struct output* temp = traverser -> next;
        free(traverser->file_name);
        free(traverser);
        traverser = temp;
    }
    outputs = NULL;
}

//this function is for dealing with the command of if is built-in command
//if it is built in and successes return 
//if it is not built in or other failures, return corresponding error code

int process_builtin(){
    
    if(commandline == NULL)
        return NULL_STRING;
    
    //get the first word.
    char* first_word = NULL;
    size_t space_pointer = 0;
    for(; space_pointer < strlen(commandline); space_pointer ++){
        if(commandline[space_pointer] == ' '){
            //get the first word and break the loop.
            first_word = malloc(space_pointer);
            first_word = strncpy(first_word, commandline, space_pointer);
            break;
        }
    }

    if(first_word == NULL){
        //there is no space in the string, test the string as a whole
        //only see whether it is "exit"
        if( strcmp(commandline,"exit") != 0)
            return NOT_BUILTIN;
        else{
            //exit the shell
            exit(1);
        }
    }

    else{
        //parse and process the first word
        if(!strcmp(first_word, "ln")){
            printf("ln detected\n");
            return 0;
        }
        else if(!strcmp(first_word, "cd")){
            printf("cd detected\n");
            return 0;
        }
        else if(!strcmp(first_word, "rm")){
            printf("rm detected\n");
            return 0;
        }
        else{
            return NOT_BUILTIN;
        }
    }
    return BUILTIN_EXCUTE_ERROR;
}

int eliminate_dup_tab_spaces(char* string){
    size_t fast = 0;
    size_t slow = 0;
    int space_encountered = 0;
    while( string[fast] == ' ' || string[fast] == '\t'){
            fast ++;
    }
        

    while(fast < strlen(string)){

        //ignore all precedent space or tabs
                if(string[fast] == ' '){
            if(fast >= 1 && string[fast - 1] == '\\'){
                string[slow] = string[fast];
                slow ++;
            }
            else if(space_encountered == 0){
                space_encountered = 1;
                string[slow] = string[fast];
                slow ++;
            }
            fast ++;
        }
        else{
            space_encountered = 0;
            //check tab
            if(string[fast] != '\t'){
                string[slow] = string[fast];
                slow ++;
            }
            fast ++;
        }
    }
    //at last.
    string[slow] = '\0';
    //printf("%s\n", string);
    return 0;
}

//eliminate all tab and spaces, but we will save the the space after '\'
//so, if there is any pair appears as '\ ' it will not eliminate the space
int eliminate_tab_spaces(char* string){
    size_t fast = 0;
    size_t slow = 0;
    while(fast < strlen(string)){
        if(string[fast] == ' '){
            if(fast > 1 && string[fast - 1] == '\\'){
                string[slow] = string[fast];
                slow ++;
            }
            fast ++;
        }
        else{
            if(string[fast] != '\t'){
                string[slow] = string[fast];
                slow ++;
            }
            fast ++;
        }
    }
    //at last.
    string[slow] = '\0';
    return 0;
}

int split_to_parts(){
    
    int input_appeared = 0;   
    size_t probe = 0;
    int command_appeared = 0;
    size_t length = 0;
    //other commannd if it is a command
    //or '>', '<' or '|' for ">>"
    char last_symbol = commandline[0];
    size_t start = 0;
    if( last_symbol == '>' || last_symbol == '<'){
        if(commandline[0] == '>'){
            if(commandline[1] == '>'){
                last_symbol = '|';
                start = 2;
                probe = 2;
            }
            else{
                start = 1;
                probe = 1;
            }
        }
        else{
            start = 1;
            probe = 1;
            if(commandline[0] == '<')
                input_appeared = 1;
        }
    }
       

    while(probe < MAX_LENGTH){

        length = probe - start + 1;
        if((commandline[probe] == ' ' && commandline[probe - 1] != '\\')){
            char previous_char = commandline[probe - 1];
            if(previous_char == '<' || previous_char == '>'){
                probe ++;
                continue;
            }

            //true space, check the type of current part
            if(last_symbol == '>'){
                char* name = (char*) malloc(length);
                memcpy(name, commandline + start, length);
                name[length - 1] = '\0';
                int res = add_output(name, 0);
                if(res  <  0){
                    return res;
                }
            }

            else if(last_symbol == '<'){
                //check the length...
                memcpy(input, commandline + start, length);
                input[length - 1] = '\0';
                eliminate_tab_spaces(input);
                if(strlen(input) == 0)
                    return EMPTY_REDIRECTION_FILE;
            }

            else if(last_symbol == '|'){
                char* name = (char*) malloc(length);
                memcpy(name, commandline + start, length);
                name[length - 1] = '\0';
                int res = add_output(name, 1);
                if( res < 0)
                    return res;
            }

            if(last_symbol == '>' || last_symbol == '<' || last_symbol == '|'){
                if(commandline[probe + 1] == '<'){
                    if(input_appeared == 0){
                        input_appeared = 1;
                    }
                    else if(input_appeared == 1)
                        return REDUNDANT_REDIRECTION;
                }
                char next_to_inspect;
                next_to_inspect = commandline[probe + 1];
                if(next_to_inspect == '>' || next_to_inspect == '<'){
                    if(next_to_inspect == '>'){
                        if( commandline[probe + 2] == '>' ){
                            last_symbol = '|';
                            probe ++;
                        }
                        else {
                            last_symbol = '>';
                        }
                    }
                    else {
                        last_symbol = '<';
                    }

                    probe += 2;
                }
                else{
                    probe ++;
                    last_symbol = next_to_inspect;
                }
                start = probe;
            }
            else{
                probe ++;
            }
        }

        else if(commandline[probe] == '>' || commandline[probe] == '<' || commandline[probe] == '\0'){
            
            //do partition
            if(last_symbol == '>'){
                length = probe - start + 1;
                char* name = (char*) malloc(length);
                memset(name, 0, length);
                memcpy(name, commandline + start, length);
                name[length - 1] = '\0';
                int res = add_output(name, 0);
                if(res  <  0){
                    return res;
                }
            }
            else if(last_symbol == '<'){
                //check the length...
                memcpy(input, commandline + start, length);
                input[length - 1] = '\0';
                eliminate_tab_spaces(input);
                if(strlen(input) == 0)
                    return EMPTY_REDIRECTION_FILE;
            }
            else if(last_symbol == '|'){
                char* name = (char*) malloc(length);
                memcpy(name, commandline + start, length);
                name[length - 1] = '\0';
                int res = add_output(name, 1);
                if( res < 0)
                    return res;
            }
            else{
                if(command_appeared == 0){
                    memcpy(command, commandline+start, length);
                    command[length -1] = '\0';
                    command_appeared = 1;
                }
                else return REDUNDANT_COMMANDS;
            }

            //update the last symbol
            if(commandline[probe] == '>'){
                if(commandline[probe+1] == '>'){
                    last_symbol = '|';
                    probe ++;
                }
                else last_symbol = '>';
            }
            else if(commandline[probe] == '<'){
                if(input_appeared == 0){
                    last_symbol = '<';
                    input_appeared = 1;
                }
                else if(input_appeared == 1)
                    return REDUNDANT_REDIRECTION;
            }

            else if(commandline[probe] == '\0'){
                //check if the command part is empty
                if( strlen(command) == 0 ){
                    return EMPTY_COMMAND;
                }
                else{
                    return 0;
                }
            }
            probe ++;
            start = probe;
        }
        //ordinary alphanumeric chars
        else{
            probe ++;
        }
    }
    return 0;
}

int parse_command(){
    //get the redirection symbol and partition the file
    //get all parts.
    int res = split_to_parts();
    if(res < 0){
        return res;
    }

    //print the parts.
    printf("command: %s\n", command);
    if(input != NULL){
        printf("input: %s\n", input);
    }
    printf("output: \n");
    struct output* traverser = outputs;
    while(traverser != NULL){
        printf("  %s\n",traverser->file_name);
        traverser = traverser -> next;
    }

    return 0;
}

//this function deal with comman processing
//which is the core function of the shell
//return 0 for success
//any minus returned values represents errors
int process_command(){
    
    eliminate_dup_tab_spaces(commandline);   
    //check whether build-in command
    int process_builtin_result = process_builtin();
    if(process_builtin_result == 0)
        return 0;
    else{
        if(process_builtin_result == NOT_BUILTIN){
            //do the general command parsing
            //and do the corresponding actions
            int res = parse_command();   
            if(res < 0){
                return res;
            }
            else{
                //execute the commands
            }
        }
        else{
            //other errors..
            return process_builtin_result;
        }
    }

    return 0;
}

void error_handler(int error_code){
    fflush(NULL);
    int error_string_entry_index = abs(error_code);
    printf("Error Number %d : %s\n", error_code, error_strings[error_string_entry_index]);
    return;
}

void refresh_buffers(){
    memset(commandline, 0, MAX_LENGTH); 
    memset(command, 0, MAX_LENGTH);
    memset(input, 0, MAX_LENGTH);
    clear_outputs();
}

int main(int argc, char *argv[])
{
	/* Go crazy! */
        if(argc == 0)
            return -1;
        
        for(int i = 0; i < argc - 1; i ++)
            printf(argv[i]);
        //initialize global strings
        
        commandline = (char*)malloc(MAX_LENGTH);
        command = (char*) malloc(MAX_LENGTH);
        input = (char*) malloc(MAX_LENGTH);
        refresh_buffers();

        while(1){
            //the byte_read includes the '\0'
            int byte_read = read(STDIN_FILENO, commandline, MAX_LENGTH);
            //change '\n' to '\0'
            commandline[byte_read-1] = '\0';
             
            int res = process_command();
            if(res != 0){
                //something wrond.
                error_handler(res); 
            }
            refresh_buffers();   
        }
    
        return 0;
}

