/***********************************************/
/* Author: David Koňař (xkonar07)
 * Date: 01.05.2011
 * Project: IOS 2 project
 * Description: Solution of 'sleeping barber' problem. Semaphores are used to sync procceses.
************************************************/

/**********
 * Header files
**********/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>

/**********
 * Definitions
**********/

//#define DEBUG 1
#define NUM_ARG 5
#define MAX_PROC 99


void clean_all(int sig);

/**********
 * Global variables
**********/

sem_t *s_cekarna = NULL;
sem_t *s_customers = NULL;
sem_t *s_holic = NULL;
sem_t *s_pocitadlo = NULL;
sem_t *s_working = NULL;
sem_t *s_leaving = NULL;
sem_t *s_rest = NULL;
sem_t *s_ready = NULL;

pid_t children[MAX_PROC];
/**********
 * Structures, enum, warnings
**********/

enum states {
	S_HELP,
	S_TEST,
};

enum e_codes {
	E_OK,
	E_WRONG_PARAM,
	E_WRONG_OUTPUT,
	E_PROBLEM_SHM,
	E_PROBLEM_SHMDT,
	E_PROBLEM_FCLOSE,
	E_PROBLEM_CHILD,
	E_PROBLEM_SEMAPHOR,
	E_PROBLEM_SEM_CLOSE,
	E_PROBLEM_SEM_UNLINK,
};

const char *error_msg[] = {
	"Vse v poradku !\n",
	"Spatny vstupni parametr(y) !\n",
	"Zadan spatny vystup !\n",
	"Probem se sdilenou pameti !\n",
	"Chyba pri uvolneni sdilene pameti !\n",
	"Chyba pri uzavirani souboru !\n",
	"Chyba pri vytvareni podprocesu !\n",
	"Chyba pri vytvareni semaforu !\n",
	"Chyba pri uzavirani semaforu !\n",
	"Chyba pri odstranovani semaforu !\n",
};

typedef struct params {
	int state;
	int error;
	FILE *file;
	int chairs;
	int genC;
	int genB;
	int num_proc;
	char output[25];
} TParams;

/**********
 * Functions declaration
**********/


/**
 * Print error on stderr output
 * @param code - code of error message
 **/
void print_error (int code) {
	fprintf(stderr, "%s", error_msg[code]);
}

/**
 * Check whether the input string is in fact an integer. Return "-1" if not.
 * @param str - string which should be integer
 **/
int check_numbers (char *str) {
    int result = 0;
    int q;
    char *empty;
    int base = 10;
    result = strtol(str, &empty, base);
    
    /* check for error */
    if((q = strcmp(empty, "")) != 0) 
        result = -1;

    if ((errno == ERANGE && (result == LONG_MAX  || result == LONG_MIN)) || (errno != 0 && result == 0))
        result = -1;

return result;
}

/**
 * Check whether there's a possibility of creating an output file
 * @param str - string, name of file
 * @param init - structure, where the FILE pointer is saved
 **/
int check_n_creat_file (char *str, TParams *init) {
	int result = 1;
    
    
	if((init->file = fopen(str, "w+")) == NULL) {	
		result = -1;
		init->error = E_WRONG_OUTPUT;
        }
	else    
		setbuf(init->file,NULL);
    
return result;
}

/**
 * Proccessing the arguments
 * @param argc - number of arguments
 * @param argv - array of inputed arguments
 **/
TParams getParams (int argc, char *argv[]) {
	int i = 0;
	int valid = 0;

	TParams init = {		// init structure
		.state = S_HELP,
		.error = E_OK,
		.chairs = 0,
		.genC = 0,
		.genB = 0,
		.num_proc = 0,
		.file = NULL,
	};

	if (argc == NUM_ARG+1) {
	  #ifdef DEBUG
		printf("Params E_OK\n");
	  #endif
        for (i = 1; i < NUM_ARG-1; i++) {
            if((valid = check_numbers(argv[i])) < 0) {
                init.error = E_WRONG_PARAM;
                break;
            }
        }
        if(init.error == E_OK) {
            if((strcmp(argv[NUM_ARG],"-")) != 0) {
                //output will be written into a file
                if(check_n_creat_file(argv[NUM_ARG], &init) == -1)		    
		    init.error = E_WRONG_OUTPUT;
            }
            else {  // output is written into a terminal windows

            }
        }

        if(init.error == E_OK) {
            init.chairs = atoi(argv[1]);
            init.genC = atoi(argv[2]);
            init.genB = atoi(argv[3]);
            init.num_proc = atoi(argv[4]);
            strcpy(init.output, argv[5]);
        }
	}
	else {
		init.error = E_WRONG_PARAM;
	}

return init;
}


/**
 * Take care of output of the program
 * @param params - structure according which the output is detected (stdout or file)
 * @param mode - type of output; 1 or 2 digits are displayed
 * @param line - number of line
 * @param str - 1 string which is always written on output
 * @param var - [if mode == 2] - this number is also written to output according to pattern
 * @param str - [if mode == 2] - this string is also written to output according to pattern (after 'var1')
 **/
int output(TParams *params, int mode, int line, char *str, int var1, char *str2) {
  if(params->file == NULL) {	// writing to standard output (stdout)
	if(mode == 0) {   
	    printf("%d: %s\n", line, str);   
	}
	if(mode == 1) {   
	    printf("%d: %s %d: %s\n", line, str, var1, str2);   
	}
  }
  else {			// writing into a file
	if(mode == 0) {   
	    fprintf(params->file, "%d: %s\n", line, str);   
	}
	if(mode == 1) {   
 	    fprintf(params->file, "%d: %s %d: %s\n", line, str, var1, str2);   
	} 
  }
  return 1;
}

/*******************
 * Cleaning funstions
 ******************/

/**
 * Closes all semaphores. Returns '1 ' if everthing goes well...
 * @param params - structure where the error warning is set
 **/
int close_all_semaphores (TParams *params) {
	if((sem_close(s_cekarna)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_pocitadlo)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_holic)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_customers)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_working)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_leaving)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_rest)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	if((sem_close(s_ready)) == -1)
		params->error = E_PROBLEM_SEM_CLOSE;
	
	if(params->error != E_PROBLEM_SEM_CLOSE)
		return 1;					// Everything's OK	

	return -1;						// Ooops, there's a problem
}

/**
 * Unlink all semaphores. Returns '1 ' if everthing goes well...
 * @param params - structure where the error warning is set
 **/
int unlink_all_semaphores (TParams *params) {
	if((sem_unlink("/cekarna_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;
	if((sem_unlink("/pocitadlo_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;
	if((sem_unlink("/holic_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;	  	  	  
	if((sem_unlink("/customers_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;	  
	if((sem_unlink("/working_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;	  
	if((sem_unlink("/leaving_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;	  
	if((sem_unlink("/rest_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;
	if((sem_unlink("/ready_xkonar07")) == -1)
		params->error = E_PROBLEM_SEM_UNLINK;
	
	if(params->error != E_PROBLEM_SEM_UNLINK)
		return 1;					// Everything's OK	

	return -1;						// Ooops, there's a problem
}

/**
 * Detach shared memory
 * @param params - structure where the error warning is set
 * @param {2-4} - pointer to shared memory
 **/
int detach_shared_memory (TParams *params, int *sh_chairs, int *sh_line, int *sh_rest) {
  
	if (shmdt(sh_chairs) < 0 )  {
		  params->error = E_PROBLEM_SHMDT;
	}
	if (shmdt(sh_line) < 0 )  {
		  params->error = E_PROBLEM_SHMDT;
	}
	if (shmdt(sh_rest) < 0 )      {
		  params->error = E_PROBLEM_SHMDT;
	}	     
	
        if(params->error != E_PROBLEM_SHMDT)
		return 1;			// everything is OK
	
	return -1;				// Ooops, there's a problem
}

/**
 * Main function 
 * @param argc - number of arguments
 * @param argv - array of inputed arguments
 **/
int main(int argc, char *argv[])
{
   (void) signal(SIGINT,clean_all);
   (void) signal(SIGTERM,clean_all);   
   // Clear the buffer of output
   setbuf(stdout,NULL);


   // Check the parametres
   TParams params = getParams(argc, argv);
      if (params.error != E_OK) {	  
	  print_error(params.error = E_WRONG_PARAM);
	  exit(EXIT_FAILURE);
    }  
    // Declaration od specific variables
    pid_t pid;
    key_t key;
   
    // Array of children proccesses PIDs
    //int children[params.num_proc+1];
    
    // Declaration of variables
    int i, t;       
    int ret = 0, fail = 0;
    int *sh_line;  
    int *sh_chairs;
    int *sh_rest;    
    int shmid_line, shmid_chairs, shmid_rest;
   
   
    // Creating semaphores
	s_cekarna = sem_open("/cekarna_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}

	s_pocitadlo = sem_open("/pocitadlo_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	s_holic = sem_open("/holic_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}

	s_customers = sem_open("/customers_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	s_working = sem_open("/working_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	s_leaving = sem_open("/leaving_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	s_rest = sem_open("/rest_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	s_ready = sem_open("/ready_xkonar07", O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (errno) {
		print_error(params.error = E_PROBLEM_SEMAPHOR);
		clean_all (2);
		exit(EXIT_FAILURE);
	}
	
	// Creating shared memory - line counter
	if((key = ftok("/dev/random", 77)) != -1) {
	    // allocation of shared memory
	    if((shmid_line = shmget(key, sizeof(int), IPC_CREAT | 0666)) != -1 ) {
		if((sh_line = (int *) shmat(shmid_line, NULL, 0)) == (void *) -1) {
		  print_error(params.error = E_PROBLEM_SEMAPHOR);
		  clean_all (2);
		  exit(EXIT_FAILURE);		   
		}
		*sh_line = 0;	// default setting				
	    }
	    else {
		print_error(params.error = E_PROBLEM_SHM);
		clean_all (2);
		return EXIT_FAILURE;
	    }	      
	} else {
	    print_error(params.error = E_PROBLEM_SHM);
	    clean_all (2);
	    return EXIT_FAILURE;
	}
	if((key = ftok("/dev/random", 88)) != -1) {
	    // allocation of shared memory
	    if((shmid_chairs = shmget(key, sizeof(int), IPC_CREAT | 0666)) != -1 ) {
		if((sh_chairs = (int *) shmat(shmid_chairs, NULL, 0)) == (void *) -1) {
		  print_error(params.error = E_PROBLEM_SEMAPHOR);
		  clean_all (2);
		  exit(EXIT_FAILURE);		   
		}
		*sh_chairs = params.chairs;	// default setting - counting lines from 1		
		
		#ifdef DEBUG
		    printf("CHAIRS:->%d-<\n", *sh_chairs);
		#endif	
		
	    }
	    else {
		print_error(params.error = E_PROBLEM_SHM);
		clean_all (2);
		return EXIT_FAILURE;
	    }	      
	} else {
	      print_error(params.error = E_PROBLEM_SHM);
	      clean_all (2);
	      return EXIT_FAILURE;
	}
	if((key = ftok("/dev/random", 99)) != -1) {
	    // allocation of shared memory
	    if((shmid_rest= shmget(key, sizeof(int), IPC_CREAT | 0666)) != -1 ) {
		if((sh_rest = (int *) shmat(shmid_rest, NULL, 0)) == (void *) -1) {
		  print_error(params.error = E_PROBLEM_SEMAPHOR);
		  clean_all (2);
		  exit(EXIT_FAILURE);		   
		}
		*sh_rest = params.num_proc;	// default setting - counting lines from 1		
		#ifdef DEBUG
		    printf("REST:->%d-<\n", *sh_rest);
		#endif
	    }
	    else {
		print_error(params.error = E_PROBLEM_SHM);
		clean_all (2);
		return EXIT_FAILURE;
	    }	      
	} else {
	      print_error(params.error = E_PROBLEM_SHM);
	      clean_all (2);
	      return EXIT_FAILURE;
	}
	#ifdef DEBUG
	    printf("\nLine: %d; Chairs: %d", *sh_line, *sh_chairs);
	#endif
   
	#ifdef DEBUG
	    printf("\n%d: I am parent, my pid: %d (%d)\n", *sh_line,  getpid(), params.num_proc);
	#endif
	
	
	  srand(time(NULL));   
  
	  pid = fork();
	  children[0] = pid;
	  if (pid == 0) {
	      // barber function           
	      while(1) {
		
		  if((sem_wait(s_pocitadlo)) == -1) {
		      clean_all(1);		      
		  }
		    *sh_line = *sh_line + 1;
		    output(&params, 0, *sh_line,"barber: checks", 0, "");		
		  if((sem_post(s_pocitadlo)) == -1) {
		      clean_all(1);		      
		  }
		  
		  // it there is no customer or chair (starting argument for customer == 0), then end;
		  if(params.num_proc == 0 || params.chairs == 0)
		      break;
		  
		  #ifdef DEBUG	      
		      printf("barber cekam (spim) na zakaznika\n");
		  #endif
		  
		  if((sem_wait(s_customers)) == -1) {
		      clean_all(1);		      
		  }
		  
		  
		  if((sem_wait(s_cekarna)) == -1) {
				clean_all(1);		      
		  }
			*sh_chairs = *sh_chairs + 1;
			if((sem_wait(s_pocitadlo)) == -1) {
				clean_all(1);		      
			}
			*sh_line = *sh_line + 1;
			output(&params, 0, *sh_line,"barber: ready", 0, "");		  
		  if((sem_post(s_pocitadlo)) == -1) {
				clean_all(1);		      
			}
			  
		  if((sem_post(s_holic)) == -1) {
				clean_all(1);		      
			}
		  if((sem_post(s_cekarna)) == -1) {
				clean_all(1);		      
			}
		  
		  if((sem_wait(s_ready)) == -1) {
				clean_all(1);		      
			}
		  
		  t = (rand() % (params.genB+1));
		  usleep(t*1000); 	// *1000 = to get miliseconds
		  
		  #ifdef DEBUG
		      printf("working for: %d\n", t);
		  #endif		  
		  
		  
		if((sem_wait(s_pocitadlo)) == -1) {
				clean_all(1);		      
			}
			*sh_line = *sh_line + 1;
			output(&params, 0, *sh_line,"barber: finished", 0, "");		  
		if((sem_post(s_pocitadlo)) == -1) {
				clean_all(1);		      
			}
		  
		if((sem_post(s_working)) == -1) {
				clean_all(1);		      
			}
		if((sem_wait(s_leaving)) == -1) {
				clean_all(1);		      
			}
		  
		if((sem_wait(s_rest)) == -1) {
				clean_all(1);		      
			}
	      
		  if(*sh_rest == 0) {
		      if((sem_post(s_rest)) == -1) {
				clean_all(1);		      
			}
		      break;
		    }		    
		  if((sem_post(s_rest)) == -1) {
				clean_all(1);		      
			}
	      }
	      
	      #ifdef DEBUG
		printf("Barber got out of infinit loop 'while(1)' (all customers have been served or refused).");
	      #endif  
		
		if((ret = close_all_semaphores(&params)) == -1) {
			// problem. Deal with it !			
			fail = 1;
			exit(EXIT_FAILURE);
		}
	      
	      exit(EXIT_SUCCESS);	    
	  }
	  else if (pid < 0) {
	      print_error(params.error = E_PROBLEM_CHILD);
	      clean_all(1);	      
	  }	
	
	// Creating children (customers)
	for (i = 1; i < params.num_proc+1; i++)
	{
	    // create customer and safe its PID
	    t = (rand() % (params.genC+1));		// random generator; makes delay of procceses
		usleep(t*1000); 			// *1000 = to get miliseconds		      
	    
		pid = fork();		

		
		if (pid == 0)
		{
	      // customer procces
			if((sem_wait(s_pocitadlo)) == -1) 
				clean_all(1);		          	
				*sh_line = *sh_line + 1;
				output(&params, 1, *sh_line,"customer", i, "created");
			if((sem_post(s_pocitadlo)) == -1)
				clean_all(1);		      
			
			
			if((sem_wait(s_cekarna)) == -1)
				clean_all(1);		      

			if((sem_wait(s_rest)) == -1)
				clean_all(1);		
		  
			#ifdef DEBUG
				printf("VEVNITR (%d)\n", getpid());
			#endif
			
			if((sem_wait(s_pocitadlo)) == -1)
				clean_all(1);		
			
				    *sh_line = *sh_line + 1;
				    output(&params, 1, *sh_line,"customer", i, "enters");		
			if((sem_post(s_pocitadlo)) == -1)
				clean_all(1);		
	      
			if(*sh_chairs > 0) {
				
		  
				 *sh_chairs = *sh_chairs - 1;
				 #ifdef DEBUG
					printf("Taking chair (%d): %d rest(s)\n", i, *sh_chairs);				
				 #endif

				if((sem_post(s_customers)) == -1)
					  clean_all(1);		
				if((sem_post(s_rest)) == -1)
					  clean_all(1);		
				
				if((sem_post(s_cekarna)) == -1)
					  clean_all(1);		
				
				if((sem_wait(s_holic)) == -1)
					  clean_all(1);		
				
				if((sem_wait(s_pocitadlo)) == -1)
					clean_all(1);		
				
				
					*sh_line = *sh_line + 1;
					output(&params, 1, *sh_line,"customer", i, "ready");		    				
				if((sem_post(s_pocitadlo)) == -1)
					  clean_all(1);		
				
				if((sem_post(s_ready)) == -1)
					  clean_all(1);		
				
				if((sem_wait(s_working)) == -1)			// wait till the barber ends its work
					  clean_all(1);		
				
				if((sem_wait(s_pocitadlo)) == -1)
					  clean_all(1);	
					  
					  
					*sh_line = *sh_line + 1;
					output(&params, 1, *sh_line,"customer", i, "served");		    				
				if((sem_post(s_pocitadlo)) == -1)
					  clean_all(1);	
				
				if((sem_wait(s_rest)) == -1)
					  clean_all(1);	
					  
					  *sh_rest = *sh_rest - 1;	
				if((sem_post(s_rest)) == -1)
					  clean_all(1);	
				
				if((sem_post(s_leaving)) == -1)
					  clean_all(1);	
			}
			else {
				if((sem_wait(s_pocitadlo)) == -1)
					  clean_all(1);	
					
					*sh_line = *sh_line + 1; 		      
					output(&params, 1, *sh_line,"customer", i, "refused");
				if((sem_post(s_pocitadlo)) == -1)
					  clean_all(1);	
				
				
				#ifdef DEBUG
					printf("Refused but recorded in shared memory !\n");				
				 #endif
				    *sh_rest = *sh_rest - 1;	
				if((sem_post(s_rest)) == -1)
					  clean_all(1);	
				
				#ifdef DEBUG
					printf("Leaving waiting room(%d)\n", getpid());   
				#endif
	      
				if((sem_post(s_cekarna)) == -1)
					  clean_all(1);	
			}		

			#ifdef DEBUG
				printf("CUSTOMER: %d:%d dies.\n", i, getpid());
			#endif

			if((ret = close_all_semaphores(&params)) == -1) {
				// problem. Deal with it !
				fail = 1;	
				print_error(params.error);
				exit(EXIT_FAILURE);
			}					
			exit(EXIT_SUCCESS);
		}
		else if (pid == -1)  { // Error - problem during making child	
			  print_error(params.error = E_PROBLEM_CHILD);
			  clean_all(1);
		}
		else { // Parent procces 
	
			children[i] = pid; //Uloží PID potomka do pole.
	      } 
      }
 
   
	      #ifdef DEBUG
		  printf("waiting till the barber kills itself....\n");
	      #endif
	/*for(i = 1; i < params.num_proc+1; i++) {
	      if ((waitpid(children[i],NULL,0)) < 0)
	      {
		      kill(children[i],SIGTERM);
		      return EXIT_FAILURE;
	      }  	  
	}
	*/
	if ((waitpid(children[0],NULL,0)) < 0)
	{
		kill(children[0],SIGTERM);
		clean_all(2);
		exit(EXIT_FAILURE);
	}  
	for(i = 1; i < params.num_proc+1; i++) {
	      if ((waitpid(children[i],NULL,0)) < 0)
	      {
		      kill(children[i],SIGTERM);
		      return EXIT_FAILURE;
	      }  	  
	}
	
  
	// Functions which take care of cleaning at the end of program  
	if((ret = close_all_semaphores(&params)) == -1) {
		// problem. Deal with it !
		print_error(params.error);
		fail = 1;
	}
	
	if((ret = unlink_all_semaphores(&params)) == -1) {
		// problem. Deal with it !
		print_error(params.error);
		fail = 1;
	}
	
	if((ret = detach_shared_memory(&params, sh_chairs, sh_line, sh_rest)) == -1) {
	    // problem. Deal with it !
	    print_error(params.error);
	    fail = 1;	  
	} 	
	      
	if(fail) {
		#ifdef DEBUG
			printf("Program terminated with failure (chec warning messages; use valgrind, ...  Bye\n");
		#endif  
		exit(EXIT_FAILURE);
	}
	
	#ifdef DEBUG
	      printf("Program should have termined all children; unlink all semaphores and clean everything...  Bye\n");
	#endif  
	      
	exit(EXIT_SUCCESS);
}

/******************
 * Cleaning functions
*******************/
void clean_all (int sig) {        
	int i;
	for(i = 0; i < MAX_PROC; i++) {
		kill(children[i], SIGINT);
	}
  
	sem_close(s_cekarna);		
	sem_close(s_pocitadlo);		
	sem_close(s_holic);		
	sem_close(s_customers);		
	sem_close(s_working);		
	sem_close(s_leaving);		
	sem_close(s_rest);		
	sem_close(s_ready);
	sem_unlink("/cekarna_xkonar07");		
	sem_unlink("/pocitadlo_xkonar07");		
	sem_unlink("/holic_xkonar07");		
	sem_unlink("/customers_xkonar07");		
	sem_unlink("/working_xkonar07");		
	sem_unlink("/leaving_xkonar07");		
	sem_unlink("/rest_xkonar07");		
	sem_unlink("/ready_xkonar07");		
	
        exit(sig);
}
