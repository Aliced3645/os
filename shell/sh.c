#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <memory.h>
#include <stdlib.h>


#define MAX_LENGTH 256



//this function is for dealing with the command of if is built-in command


//this function deal with comman processing
//which is the core function of the shell

int process_command(char* command){
    
    //check whether build-in command
    command = NULL;
    return 0;
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
            fprintf(stderr, "Love\n");
            read(STDIN_FILENO, command, MAX_LENGTH);
	    //printf("%s\n", instruction);
            int res = process_command(command);
            if(res != 0){
                //something wrond.
                error_handler(res); 
            }
            memset(command, 0, MAX_LENGTH);
        }
    
        
        return 0;
}

