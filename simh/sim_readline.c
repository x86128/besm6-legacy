/* sim_readline.c: libreadline wrapper

   Written by Philipp Hachtmann
   
   12-MAR-09    PH     Completely rebuilt readline integration
   11-MAR-09    PH     Some corrections to double entry detection
   01-OCT-08    PH     Include stdio.h
   19-SEP-08    PH     Initial version


   Why dlopen() the libraries:
   libreadline uses libncurses. If both are happily loaded at
   startup time, they will mess up global symbols like "PC".
   By "manually" using libreadline and libhistory, this problem
   seems to be defeated.
*/


/* Disable saved history when there's no readline */
#ifdef READLINE_SAVED_HISTORY
#ifndef HAVE_READLINE
#undef READLINE_SAVED_HISTORY
#endif
#endif

#ifdef HAVE_READLINE

#include <stdio.h>
#include <dlfcn.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "sim_defs.h"

#ifdef READLINE_SAVED_HISTORY
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define HIST_PATH_BUF_LEN 240
#define FNAME_LEN 50

/* Get simulator name from rest of SIMH */
extern char sim_name[];
static char hist_path_buf[HIST_PATH_BUF_LEN];
#endif

#define LIBREADLINE_SO "libreadline.so.5"
#define LIBHISTORY_SO  "libhistory.so.5"

/* Function pointers to readline and history functions */
static char *        (*p_readline)       (const char *);
static void          (*p_using_history)  (void);
static int           (*p_read_history)   (const char *);
static int           (*p_write_history)  (const char *);
static HIST_ENTRY ** (*p_history_list)   (void);
static void          (*p_add_history)    (const char *);
static HIST_ENTRY *  (*p_remove_history) (int);

static int initialized=0;
static void * readline_handle=NULL;

int sim_readline_start(){

#ifdef READLINE_SAVED_HISTORY
  char fname[FNAME_LEN];
  int c;
#endif

  initialized=1;
  
  /* Now open the library (history lib comes in trailing readline)  */
  readline_handle=dlopen(LIBREADLINE_SO,RTLD_NOW|RTLD_GLOBAL);
  if (!readline_handle) {
    fprintf(stderr, "Error while loading library: %s\n",dlerror());
    initialized=0;
    return initialized;
  } 

  p_readline=dlsym(readline_handle,"readline");
  if (!p_readline) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_using_history=dlsym(readline_handle,"using_history");
  if (!p_using_history) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_read_history=dlsym(readline_handle,"read_history");
  if (!p_read_history) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_write_history=dlsym(readline_handle,"write_history");
  if (!p_write_history) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_history_list=dlsym(readline_handle,"history_list");
  if (!p_history_list) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_add_history=dlsym(readline_handle,"add_history");
  if (!p_add_history) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_remove_history=dlsym(readline_handle,"remove_history");
  if (!p_remove_history) {
    printf("Error: %s\n",dlerror());
    initialized=0;
    return initialized;
  }

  p_using_history();

#ifdef READLINE_SAVED_HISTORY

  /* Generate lower case file name like ".nova_history" */
  strcpy(fname,".");
  strncat (fname,sim_name,FNAME_LEN);
  fname[FNAME_LEN-1]=0;
  for (c=0;c<strlen(fname);c++)
    fname[c]=tolower(fname[c]);
  strcat(fname,"_history");
  
  char * tmp=getenv("HOME");
  hist_path_buf[0]=hist_path_buf[HIST_PATH_BUF_LEN-1]=0;

  if (tmp!=NULL){
    strncat(hist_path_buf,tmp,HIST_PATH_BUF_LEN-1);
    strncat(hist_path_buf,"/",HIST_PATH_BUF_LEN-1);
  }
  
  strncat(hist_path_buf,fname,HIST_PATH_BUF_LEN-1);
  p_read_history(hist_path_buf);

#endif

  return initialized;
}

int sim_readline_stop(){
#ifdef READLINE_SAVED_HISTORY
  p_write_history(hist_path_buf);
#endif

  if (readline_handle) dlclose(readline_handle);
  return 1;
}

/******************************************************************************

   sim_readline_readline            read line

   Inputs:
        buffer  =       pointer to buffer
   buffer_size  =       maximum size
        prompt  =       prompt string to use
   Outputs:
        optr    =       pointer to first non-blank character
                        NULL if EOF

*******************************************************************************/

char * sim_readline_readline(char *buffer, int32 buffer_size, const char * prompt){

   char * newline=NULL;
   
   if (!initialized) return NULL;
 
   newline=p_readline(prompt);
   
   /* Action for EOF */
   if (newline==NULL) return NULL;
  
   /* For security reasons */
   buffer[buffer_size-1]=0; 

   /* Copy the line data to the user-provided buffer */
   strncpy(buffer,newline,buffer_size-1);
   
   free(newline);
 
   /* Add current line to history - And don't add repetitions! */
   if ((strlen(buffer))&&(strncmp("quit",buffer,strlen(buffer))!=0)){
    
     HIST_ENTRY ** the_list=p_history_list();
     int i;
    
     if (the_list) for (i = 0; the_list[i]; i++){
       if (strcmp(buffer,the_list[i]->line)==0){
	 HIST_ENTRY * entry=p_remove_history(i);
	 free(entry->line);
	 free(entry);
	 i--;
	 the_list = p_history_list();
	 if (!the_list) break;
       }
     }
     p_add_history(buffer);
   }

   /* Skip leading whitespace */
   while(isspace(*buffer)) buffer++;
  
   /* Make a comment line an empty line */
   if (*buffer == ';') *buffer=0;
  
   /* We're finished here */
   return buffer;
}

#endif
