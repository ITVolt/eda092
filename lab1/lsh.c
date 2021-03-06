/* 
 * Main source code file for lsh shell program
 *
 * You are free to add functions to this file.
 * If you want to add functions in a separate file 
 * you will need to modify Makefile to compile
 * your additional functions.
 *
 * Add appropriate comments in your code to make it
 * easier for us while grading your assignment.
 *
 * Submit the entire lab1 folder as a tar archive (.tgz).
 * Command to create submission archive: 
      $> tar cvf lab1.tgz lab1/
 *
 * All the best 
 */

#define PIPE_READ 0
#define PIPE_WRITE 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "parse.h"
#include "string.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * Function declarations
 */
int RunShellCommand(Pgm *pgm);
void RunCommand(Command cmd);
void RunPipeline(Pgm *pgm, int *fd, char *input, int isBackground);
void ExecuteCommand(Pgm *pgm);
int SetFileAsStdin(char *filename);
int SetFileAsStdout(char *filename);
void PrintCommand(int, Command *);
void PrintPgm(Pgm *);
void stripwhite(char *);
void signal_handle(int sig);

/* When non-zero, this global means the user is done using this program. */
int done = 0;
pid_t parent_pid;
pid_t background_pid;

/*
 * Name: main
 *
 * Description: Gets the ball rolling...
 *
 */
int main(void)
{
  Command cmd;
  int n;

  printf("Kjell has awakened\n");

  parent_pid = getpid();
  signal(SIGINT, signal_handle);  //Catch Ctrl+C signal
  signal(SIGCHLD, signal_handle); //Catch Child termination signal

  while (!done) {
    char *line;
    line = readline("\n> ");

    if (!line) {
      /* Encountered EOF at top level */
      done = 1;
    }
    else {
      /*
       * Remove leading and trailing whitespace from the line
       * Then, if there is anything left, add it to the history list
       * and execute it.
       */
      stripwhite(line);

      if(*line) {
        add_history(line);
        /* execute it */
        n = parse(line, &cmd);
        if (n == -1) {
          PrintCommand(n, &cmd);
          printf("Parsing failed\n");
          // Perhaps print the error from the parsing.
        } else if (!RunShellCommand(cmd.pgm)) { //Checks if the line is a shell command and runs it.
          RunCommand(cmd);                      //Otherwise try to execute it as normal.
          //Wait for all children to finish
          while (waitpid(0, NULL, 0)) {
            if (errno == ECHILD) {
              break;
            }
          }
        }
      }
    }
    
    if(line) {
      free(line);
    }
  }
  return 0;
}

/*
 * Checks if the input is a shell command and executes it if it is.
 * Returns 1 if shell command, 0 if not.
 */
int
RunShellCommand(Pgm *pgm) {
  char **list = pgm->pgmlist;
  if (!strcmp(list[0], "exit")) {       //Exits the shell
    if (pgm->next) {
      fprintf(stderr, "Exit is not compatible with pipes.\n");
      return 1;
    }
    printf("It's time to go night night\n");
    exit(0);
  } else if(!strcmp(list[0] , "cd")){   //Change the active directory
    if (pgm->next) {
      fprintf(stderr, "cd is not compatible with pipes.\n");
    } else {
      if(list[1] == NULL || !strcmp(list[1], "~")) {
        chdir(getenv("HOME"));
      } else if(chdir(list[1]) < 0){
        char *errmsg = strerror( errno );
        fprintf(stderr, "CD failed with meassage: %s\n", errmsg);
      }
    }
    return 1;
  }
  return 0;
}

/*
 * Runs the given command with input-, output-files and pipes if
 * specified. Commands set to be executed in the background will be
 * set to another process group so they Pipes will be recursively executed by the RunPipeline
 * method.
 */
void
RunCommand(Command cmd)
{
  Pgm *pgm = cmd.pgm;
  
  if (pgm->next) {  //If there are several commands we do the necessary piping.
    int fd[2];
    pipe(fd);
    RunPipeline(pgm->next, fd, cmd.rstdin, cmd.bakground);
    pid_t pid = fork();
    if (pid > 0) {  //Parent
      if (cmd.bakground) {
        background_pid = pid;
        setpgid(pid, background_pid);
        printf("Backround process %d started.\n", background_pid);
      }
      close(fd[PIPE_READ]);
      close(fd[PIPE_WRITE]);
    } else if (pid == 0) {  //Child
      if (cmd.bakground) {
        setpgid(0,0);
      }
      dup2(fd[PIPE_READ], 0);
      close(fd[PIPE_WRITE]);
      if (cmd.rstdout) {
        if (SetFileAsStdout(cmd.rstdout) != 1) {
          exit(1); //Does not continue to execute command on error when setting output.
        }
      }
      ExecuteCommand(pgm);
    }
  } else {  //If there are no pipes, just a single command.
    pid_t pid = fork();
    if (pid > 0 && cmd.bakground) {   //Parent (only does something if the command is set as background)
      background_pid = pid;
      setpgid(pid, background_pid);
      printf("Backround process %d started.\n", background_pid);
    } else if (pid == 0) {            //Child
      if (cmd.bakground) {
        setpgid(0,0);
      }
      if (cmd.rstdin) {
        if (SetFileAsStdin(cmd.rstdin) != 1) {
          exit(1); //Does not continue to execute command on error when setting input.
        }
      }
      if (cmd.rstdout) {
        if (SetFileAsStdout(cmd.rstdout) != 1) {
          exit(1); //Does not continue to execute command on error when setting output.
        }
      }
      ExecuteCommand(pgm);
    }
  }
}

/*
 * Recursive function execute piped commands.
 * Commands take their inputs from "deeper" into the pipeline
 * and sends the output upwards.
 * Parameters:
 * pgm   - The program to execute
 * fd    - The pipe to the program listening to this one
 * input - If this is set then the last program will use it as it's input
 * isBackground - With this set the main thread will not wait for the
 *                program to finish and will set it to another process group
 */
void
RunPipeline(Pgm *pgm, int *fd, char *input, int isBackground) {
  if (pgm->next) {  //If there are more commands we keep calling this method.
    int fd2[2];
    pipe(fd2);
    RunPipeline(pgm->next, fd2, input, isBackground);
    pid_t pid = fork();
    if (pid > 0) {  //Parent
      if (isBackground) {
        setpgid(pid, background_pid);
      }
      close(fd2[PIPE_READ]);
      close(fd2[PIPE_WRITE]);
    } else if (pid == 0) {  //Child
      if (isBackground) {
        setpgid(0, background_pid);
      }
      /* Makes this thread take input from below it
         and sends the output upwards */
      dup2(fd2[PIPE_READ], 0);
      dup2(fd[PIPE_WRITE], 1);
      close(fd2[PIPE_WRITE]);
      close(fd[PIPE_READ]);
      ExecuteCommand(pgm);
    }
  } else {  //This is the last command.
    pid_t pid = fork();
    if (pid > 0 && isBackground) {  //Parent
      setpgid(pid, background_pid);
    } else if (pid == 0) {  //Child
      if (isBackground) {
        setpgid(0, background_pid);
      }
      dup2(fd[PIPE_WRITE], 1);
      close(fd[PIPE_READ]);
      if (input) {       //If there is a specified input we use it as stdin.
        if (SetFileAsStdin(input) != 1) {
          exit(1);  //Does not continue to execute command on error when setting input.
        }
      }
      ExecuteCommand(pgm);
    }
  }
}

/*
 * Executes a command with all its parameters.
 * Exits with code 1 if failure to execute the command.
 */
void
ExecuteCommand(Pgm *pgm) {
  char **list = pgm->pgmlist;
  int result;
  if (result = execvp(list[0], list) < 0) {
    char *errmsg = strerror( errno );
    fprintf(stderr, "Error with command '%s': %s\n", list[0], errmsg);
    exit(1);
  }
}

/*
 * Help method for using a file as standard input.
 */
int
SetFileAsStdin(char *filename) {
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    char *errmsg = strerror( errno );
    fprintf(stderr, "%s %s\n", errmsg, filename);
    return -1;
  }
  close(0);
  dup2(fd, STDIN_FILENO);
  close(fd);
  return 1;
}

/*
 * Help method for using a file as standard output.
 */
int
SetFileAsStdout(char *filename) {
  FILE *f = fopen(filename, "w");
  if (f == NULL) {  // fopen( ) failed, fp is set to NULL
    char *errmsg = strerror( errno );
    fprintf(stderr, "%s %s\n", errmsg, filename);
    return -1;
  }
  close(1);
  dup2(fileno(f), STDOUT_FILENO);
  fclose(f);
  return 1;
}

/*
 * Will be called when the process recieves specific signals.
 */
void 
signal_handle(int sig){
  if(sig == SIGINT){  //Handle Ctrl+C so it doesn't kill the main thread.
    printf("\n> ");
  } else if (sig == SIGCHLD) { //Child temrminated.
    waitpid(-1, 0, WNOHANG);
  }
}

/*
 * Name: PrintCommand
 *
 * Description: Prints a Command structure as returned by parse on stdout.
 *
 */
void
PrintCommand (int n, Command *cmd)
{
  printf("Parse returned %d:\n", n);
  printf("   stdin : %s\n", cmd->rstdin  ? cmd->rstdin  : "<none>" );
  printf("   stdout: %s\n", cmd->rstdout ? cmd->rstdout : "<none>" );
  printf("   bg    : %s\n", cmd->bakground ? "yes" : "no");
  PrintPgm(cmd->pgm);
}

/*
 * Name: PrintPgm
 *
 * Description: Prints a list of Pgm:s
 *
 */
void
PrintPgm (Pgm *p)
{
  if (p == NULL) {
    return;
  }
  else {
    char **pl = p->pgmlist;

    /* The list is in reversed order so print
     * it reversed to get right
     */
    PrintPgm(p->next);
    printf("    [");
    while (*pl) {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}

/*
 * Name: stripwhite
 *
 * Description: Strip whitespace from the start and end of STRING.
 */
void
stripwhite (char *string)
{
  register int i = 0;

  while (whitespace( string[i] )) {
    i++;
  }
  
  if (i) {
    strcpy (string, string + i);
  }

  i = strlen( string ) - 1;
  while (i> 0 && whitespace (string[i])) {
    i--;
  }

  string [++i] = '\0';
}
