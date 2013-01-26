#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <memory.h>
#include <stdlib.h>


#define MAX_LENGTH 256
#define GENERAL_ERROR -255
#define NULL_STRING -1
#define NOT_BUILTIN -2
#define BUILTIN_EXCUTE_ERROR -3
#define REDUNDANT_REDIRECTION -4
#define EMPTY_COMMAND -5
#define EMPTY_REDIRECTION_FILE -6
#define COMMANDLINE_SYNTAX_ERROR -7

//this function is for dealing with the command of if is built-in command
//if it is built in and successes return 
//if it is not built in or other failures, return corresponding error code

int process_builtin(char* command){
    
    if(command == NULL)
        return NULL_STRING;
    
    //get the first word.
    char* first_word = NULL;
    size_t space_pointer = 0;
    for(; space_pointer < strlen(command); space_pointer ++){
        if(command[space_pointer] == ' '){
            //get the first word and break the loop.
            first_word = malloc(space_pointer);
            first_word = strncpy(first_word, command, space_pointer);
            break;
        }
    }

    if(first_word == NULL){
        //there is no space in the string, test the string whole
        //only see whether it is "exit"
        if( strcmp(command,"exit") != 0)
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

int eliminate_tab_space(char* command){
    size_t fast = 0;
    size_t slow = 0;
    int space_encountered = 0;
    while(fast < strlen(command)){
        if(command[fast] == ' '){
            if(!space_encountered){
                space_encountered = 1;
                command[slow] = command[fast];
                slow ++;
            }
            fast ++;
        }
        else{
            space_encountered = 0;
            //check tab
            if(command[fast] != '\t'){
                command[slow] = command[fast];
                slow ++;
            }
            fast ++;
        }
    }
    //at last.
    command[slow] = '\0';
    return 0;
}

int split_to_parts(char* command_line, char* command, char* input, char* output, char* output2){
    
    size_t probe = 0;
    
    int symbol_count = 0;
    //use '|' to stand for "<<"
    char symbol_sequences[3];
    
    //decide to go two passes
    //first pass the get the sequence of symbols 
    while(probe < strlen(command_line)){
        char c = command_line[probe];
        if(c == '<'){
           symbol_sequences[symbol_count] = '<';
           symbol_count ++;
        }
        else if(c == '>'){
            if( probe < strlen(command_line)){
                if(command_line[probe + 1] != '>')
                    symbol_sequences[symbol_count] = '>';
                else
                    symbol_sequences[symbol_count] = '|';
            }
            else
                symbol_sequences[symbol_count] = '>';
            symbol_count ++;
        }
        probe ++;
    }
    
    //check redundant redirection symbols.
    for(int i = 0; i < symbol_count; i ++){
        for(int j = i + 1; j < symbol_count; j ++){
            if(symbol_sequences[i] == symbol_sequences[j])
                return REDUNDANT_REDIRECTION;
        }
    }
    //special case, no redirection symbol
    //the command line is the command.
    if(symbol_count == 0){
        size_t length = strlen(command_line);
        if(length == 0)
            return EMPTY_COMMAND;
        command = (char*) malloc( length + 1);
        memcpy(command, command_line, length + 1);
        command[length] = '\0';
        return 0;
    }

    //second pass to fill in buffers.
    size_t start = 0;
    symbol_count = 0;
    probe = 0;
    while(probe < strlen(command_line)){
        char c = command_line[probe];
        if(c == '<' || c == '>'){
            //get the previous part
            if(symbol_count == 0){
                //get the command part...
                command = (char*)malloc( probe + 1);
                memset(command, 0 , probe + 1);
                memcpy(command, command_line, probe + 1);
                command[probe] = '\0';
            }
            else{
                //record the previous part
                char redirection_symbol = symbol_sequences[symbol_count - 1];
                size_t length = probe - start + 1;
                if(redirection_symbol == '>'){
                    output = (char*)malloc( length);
                    memset(output, 0, length);
                    memcpy(output, command_line + start, length);
                    output[length - 1] = '\0';
                }
                else if(redirection_symbol == '<'){
                    input = (char*)malloc(length);
                    memset(input, 0, length);
                    memcpy(input, command_line + start, length);
                    input[length - 1] = '\0';
                }
                else if(redirection_symbol == '|'){
                    output2 = (char*)malloc( length);
                    memset(output2, 0, length);
                    memcpy(output2, command_line + start, length);
                    output2[length - 1] = '\0';
                }
            }
            //set new starts
            if(symbol_sequences[symbol_count] == '|'){
                probe += 2;
            }
            else{
                probe ++;
            }
            start = probe;
            symbol_count ++;
        }
        else
            probe ++; 
    }
    //at last, record the last part
    char redirection_symbol = symbol_sequences[symbol_count - 1];
    size_t length = probe - start + 1;
    if(length == 1){
        //if there is no contents behind the redirection symbol, it is also a
        //syntax error
        return EMPTY_REDIRECTION_FILE;
    }

    if(redirection_symbol == '>'){
        output = (char*)malloc(length);
        memset(output, 0, length);
        memcpy(output, command_line + start, length);
        output[length - 1] = '\0';
    }
    else if(redirection_symbol == '<'){
        input = (char*)malloc(length);
        memset(input, 0, length);
        memcpy(input, command_line + start, length);
        input[length - 1] = '\0';
    }
    else if(redirection_symbol == '|'){
        output2 = (char*)malloc( length);
        memset(output2, 0, length);
        memcpy(output2, command_line + start, length);
        output2[length - 1] = '\0';
    }
       
    return 0;
}

int parse_command(char* command_line){
    //first eliminate all tabs ('/t') and redundant spaces in the string
    eliminate_tab_space(command_line);
    //get the redirection symbol and partition the file
    char* command = NULL;
    char* input = NULL;
    char* output = NULL;
    char* output2 = NULL;
    //get all parts.
    int res = split_to_parts(command_line, command, input, output, output2);
    if(res < 0){
        printf("Split Error Num: %d\n", res);
        printf("Split Error Num Meaning: \n \
                \t -4 REDUNDANT_REDIRECTION \n \
                \t -5 EMPTY_COMMAND \n \
                \t -6 EMPTY_REDIRECTION_FILE \n");
        return COMMANDLINE_SYNTAX_ERROR;
    }

    //print the parts.
    printf("command: %s\n", command);
    if(input != NULL){
        printf("input: %s\n", input);
    }
    if(output != NULL){
        printf("output: %s\n", output);
    }
    if(output2 != NULL){
        printf("output2: %s\n", output2);
    }


    //now we can fork and exeute the child processes
    return 0;
}

//this function deal with comman processing
//which is the core function of the shell
//return 0 for success
//any minus returned values represents errors
int process_command(char* command){
    
    eliminate_tab_space(command);   
    //check whether build-in command
    int process_builtin_result = process_builtin(command);
    if(!process_builtin_result)
        return 0;
    else{
        if(process_builtin_result == NOT_BUILTIN){
            //do the general command parsing
            //and do the corresponding actions
            int res = parse_command(command);   
            if(!res){
                printf("Command line syntax error");
            }

        }
        else{
            //other errors..
            return process_builtin_result;
        }
    }

    return GENERAL_ERROR;
}

void error_handler(int error_code){
    
    switch(error_code){
    
        default:
            printf("unknown error\n");
            break;
    }
    
    return;
}

int main(int argc, char *argv[])
{
	/* Go crazy! */
        if(argc == 0)
            return -1;
        
        for(int i = 0; i < argc - 1; i ++)
            printf(argv[i]);
        
        char* command = (char*)malloc(MAX_LENGTH);
        memset(command, 0, MAX_LENGTH); 
        while(1){
            int byte_read = read(STDIN_FILENO, command, MAX_LENGTH);
            command[byte_read - 1] = '\0';
	    //printf("%s\n", instruction);
            
             
            int res = process_command(command);
            if(res != 0){
                //something wrond.
                error_handler(res); 
            }
            
            printf("%s\n", command);
            memset(command, 0, MAX_LENGTH);
        }
    
        
        return 0;
}

