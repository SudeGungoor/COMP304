#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h> 
#include <termios.h> 
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h> 
#include <stdint.h> 
#include <ctype.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
const char *sysname = "Shellect";
// ANSI color codes
#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '?'; // autocomplete
			break;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}
int process_command(struct command_t *command);

int file_exists(const char *cmdName) {
	struct stat buffer;
	int exist = stat(cmdName, &buffer);
	if (exist == 0) {
		return 1;
	} else {
		return 0;
	}

}
int generateRandomNumber() {
	return rand() % 10000000 + 1;
}

void exec_command(struct command_t *command) {
	//part 2
	//check for redirection
	if (command->redirects[0] != NULL) {
		int fd0 = open(command->redirects[0], O_RDONLY);
		if (fd0 < 0) {
			printf("-%s: %s: %s\n", sysname, command->name,
				   strerror(errno));
			exit(0);
		}
		

		dup2(fd0, STDIN_FILENO);// dup2 duplicates the file descriptor fd0
		//printf( "fd0: %d\n",  fd0);
		//printf( "dup0: %d\n",  STDIN_FILENO);
		close(fd0); // closes the old file descriptor
	}
	if (command->redirects[1] != NULL) {
		int fd1 = open(command->redirects[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (fd1 < 0) {
			printf("-%s: %s: %s\n", sysname, command->name,
				   strerror(errno));

			exit(0);
		}
		//printf( "fd1: %d\n",  fd1);
		//printf( "dup1: %d\n",  STDOUT_FILENO);
		dup2(fd1, STDOUT_FILENO);
		close(fd1);
	}
	if (command->redirects[2] != NULL) {
		int fd2 = open(command->redirects[2], O_CREAT | O_WRONLY | O_APPEND, 0644);
		if (fd2 < 0) {
			printf("-%s: %s: %s\n", sysname, command->name,
				   strerror(errno));
			exit(0);
		}
		//printf( "fd2: %d\n",  fd2);
		//printf( "dup2: %d\n",  STDOUT_FILENO);
		dup2(fd2, STDOUT_FILENO);
		close(fd2);
	}
	// end of part 2
	
	// part 1
	// checks for executable file in current directory
	if(command->args[0][0] == '.' && command->args[0][1] == '/') {
		if (file_exists(command->name)) {
			//get current working directory
			execv(command->name, command->args);
		}
	}
	else
	{
		
		char *path = getenv("PATH"); 
		char *dupPath = strdup(path); // for tokenization created duplicate of path variable
		// path variable is a string of directories separated by colon
		// Separated them to get each directory
		char *dir = strtok(dupPath, ":"); // strtok function is used to separate string using delimiter
		char cmdPath[1000]; // upper limit for the length of an exec path is 1000
		while (dir) {
			sprintf(cmdPath, "%s/%s", dir, command->name);
			if (access(cmdPath, X_OK) == 0) {
				break;
			}
			dir = strtok(NULL, ":");
		}
		execv(cmdPath, command->args);
		//free(path);
		
	}
	printf("-%s: %s: command not found\n", sysname, command->name);
	// end of part 1
}

void alias(struct command_t *command) {
	char alias[100]; 
	strcpy(alias, command->args[1]); // Copy the first argument to the buffer
    for (int i = 2; i < command->arg_count-1; i++) {
        if (i != 1) {
            strcat(alias, " "); // add a space separator
        }
        strncat(alias, command->args[i], 100 - strlen(alias) - 1); // concatenate the argument
    }
	
	FILE *fp = fopen("aliases.txt", "r+");
	if (fp == NULL) {
        // file doesn't exist, create it and write the alias
        fp = fopen("aliases.txt", "w");
        fprintf(fp, "%s\n", alias);
        fclose(fp);
        return;
    }
	char line[100];
	

	while(fgets(line, 100, fp) != NULL) {
		char *token_line = strtok(line, "/n");
		//remove the whitespace from the end of the token line
		token_line[strcspn(token_line, "\n")] = 0;
		// check if the alias already exists
		//printf("%s-\n", alias);
		//printf("%s-\n", token_line);
		
		//printf("%ld\n", strlen(alias));
		//printf("%ld\n", strlen(token_line));

		if (strcmp(token_line, alias) == 0) {
			printf("alias already exists\n");
			fclose(fp);
			return;
		}
	}
	fclose(fp);
	fp = fopen("aliases.txt", "a");
	fprintf(fp, "%s\n", alias);
	fclose(fp);
	
}
void psvis(struct command_t *command){

	// Construct the command to visualize the process tree
	char l1[1024];
	char l2[1024];
	if(fork() == 0) { // child process

		sprintf(l1, "sudo dmesg -C; sudo insmod psvis.ko PID=%d", atoi(command->args[1])); //construct the command to visualize the process tree 
		system(l1); //execute the command
		FILE *fp = fopen("output.txt", "w"); //open the file
		system("echo \"digraph G {\" >> output.txt"); //write the first line of the file to output.txt
		system("sudo dmesg | grep \"psvis\" | grep \"child\" | awk '{print $3, $4, $5, $6, $7, $8, $9, $10, $11, $12}' >> output.txt"); //write the process tree to output.txt
        sprintf(l2, "cat output.txt | dot -Tpng > %s.png", command->args[2]); //convert the output.txt to png
		fclose(fp);
		system(l2);
		exit(0);
	}
	else{
		wait(NULL);//wait for the child process to finish
	}
}

void lara()
{
	printf("Hello %s, welcome to the multiplication game!\n", getenv("USER"));
	printf("You will be given 10 questions to answer.\n");
	printf("You have 5 seconds to answer each question.\n");
	printf("If you answer correctly, you will get a star.\n");
	printf("If you answer incorrectly, you will not get a star.\n");
	printf("If you answer all 10 questions correctly, you will get a prize!\n");
	printf("Good luck!\n");
	printf("Press enter to start the game.\n");
	getchar();
	int score = 0;
	int answer;
	int correctAnswer;
	int randomNumber1;
	int randomNumber2;
	int i;
	for (i = 0; i < 10; i++) {
		randomNumber1 = rand() % 10 + 1;
		randomNumber2 = rand() % 10 + 1;
		correctAnswer = randomNumber1 * randomNumber2;
		printf("Question %d: %d x %d = ", i + 1, randomNumber1, randomNumber2);
		fflush(stdout);
		sleep(5); //waits 5 seconds for user to answer
		printf("\033[H\033[J");
		scanf("%d", &answer);
		if (answer == correctAnswer) {
			printf("Correct!\n");
			score++;
		}
		else {
			printf("Incorrect!\n");
		}
	}
	printf("Your final score is: %d\n", score);
	if (score == 10) {
		printf("Congratulations! You've completed 10 rounds.\n");
		printf("    .-\"\"\"-.\n");
		printf("   /       \\\\n");
		printf("  |         |\n");
		printf("  | o      o |\\n");
		printf("  |     ∆    |\n");
		printf("  \\ ~~  /\n");
		printf("   \\       /\n");
		printf("    '-...-'\n");


	}
	else {
		printf("Thanks for playing!\n");
		printf("  /////\n");
    	printf(" /     \\\n");
    	printf("|  O O  |\n");
   	printf("|   ∆   |\n");
    	printf(" \\ ~  /\n");
    	printf("  \\_/\n");
	}

}
void sude(struct command_t *command) {
    srand(time(NULL));
    int randomNumber, lives = 3, score = 0;
    int correctCount = 0;
    int theme_index = 0;
    char input[100];
    char *themes[5] = {"Cat", "Dog", "Penguin", "Frog", "Owl"};
    int sleeper = 5;
    clock_t start_time, current_time;

    if (command->arg_count > 1) {		
        theme_index = atoi(command->args[1]);
        if (theme_index >= 0 && theme_index < 5) {
            printf("Welcome to the Number Memory Game with theme: %s\n", themes[theme_index]);
			sleep(3);
        } else {
            printf("Invalid theme index. Please enter a number between 0 and 4 for theme selection.\n");
            sleep(3);
			return;
        }
    } else {
        printf("Welcome to the Number Memory Game with default theme: %s\n", themes[theme_index]);
		sleep(3);
	}

    while (lives > 0 && correctCount < 10) {
        for (int i = 0; i < 10; i++) {
            randomNumber = generateRandomNumber();
            // start the timer
            for (int seconds_left = sleeper; seconds_left > 0; seconds_left--) {
				printf("\033[H\033[J"); // clear the screen
				// display lives and score
				printf(YEL "Lives: %d\t\t" RESET, lives);
                printf(GRN "Score: %d\n\n" RESET, score);
				if (theme_index == 0) {
					//cat
					printf(MAG " /\\_/\\\n" RESET);
            		printf(MAG "( o.o )\n" RESET);
            		printf(MAG " > ^ <\n\n" RESET);

				} else if (theme_index == 1) {
					// dog
					printf(BLU " /\\_/\\\n" RESET);
            		printf(BLU "( ' ' )\n" RESET);
            		printf(BLU "  (U)\n\n" RESET);
				}
				 else if (theme_index == 2) {
					// penguin
					printf(YEL "   __\n" RESET);
					printf(YEL " -=(o '.)o\n" RESET);
					printf(YEL "   (    (_)\n" RESET);
					printf(YEL "   /|   |\n" RESET);
					printf(YEL "  ^ |___|\n\n" RESET);

				} else if (theme_index == 3) {
					// frog
					printf(GRN "  @..@\n" RESET);
					printf(GRN " (----)\n" RESET);
					printf(GRN "( >__< )\n" RESET);
					printf(GRN "^^ ~~ ^^ \n\n" RESET);
				} else if (theme_index == 4) {
					// owl
					printf(CYN "   ___   \n" RESET);
					printf(CYN "  (o,o)  \n" RESET);
					printf(CYN "  {\"\"}\"  \n" RESET);
					printf(CYN " -\"-\"-- \n\n" RESET);

				}
				
				printf(MAG "Number: %d\n\n" RESET, randomNumber);
				printf(CYN "You will be asked to enter the number in: %d seconds\n" RESET, seconds_left);
                sleep(1);
            }
			

            // clear the screen
            printf("\033[H\033[J");
			
			printf(YEL "Lives: %d\t\t" RESET, lives);
            printf(GRN "Score: %d\n\n" RESET, score);
			printf(MAG "You have %d seconds left to enter the number. Press enter to continue \n" RESET, sleeper);
            // wait for user input
            start_time = clock();
			//clear stdin
			fflush(stdin);
            fgets(input, sizeof(input), stdin);
			current_time = clock();

            // check user's input
			if (current_time - start_time < sleeper * CLOCKS_PER_SEC) {
				int userNumber = atoi(input);
				if (userNumber == randomNumber) {
					printf("Correct!\n");
					score++;
					correctCount++;
					if (correctCount % 5 == 0) {
						sleeper--;
					}
				} else {
					printf("Incorrect!\n");
					lives--;
					if (lives <= 0) {
						printf("Game Over! You have run out of lives.\n");
						break;
					}
				}
				sleep(1);

			} else {
				// player didn't enter a number within the time limit
				printf("Time's up! You didn't enter a number in time.\n");
				lives--;
				score--;  // Decrease the score by 1
				if (lives <= 0) {
					printf("Game Over! You have run out of lives.\n");
					break;
				}

				// display the correct number and wait for a short duration
				printf("The correct number was: %d\n", randomNumber);
				printf("Preparing for the next round...\n");
				fflush(stdout);
				sleep(2);
				continue;
			}
            if (correctCount >= 10) {
                printf("Congratulations! You've completed 10 rounds.)\n");
				break;

			}
		}
	}
}		

void good_morning(struct command_t *command) {

	if(command->arg_count != 4) {
		printf("-%s: %s: %s\n", sysname, command->name,
			strerror(errno));
		printf("Syntax: good_morning <minutes> <path/to/audio>\n");
		return;
	}
	int minutes = atoi(command->args[1]); 
	char *audio = command->args[2];
	
	//printf("%d\n", minutes);
	//printf("%s\n", audio);
	
	FILE *audio_file = fopen(audio, "r");
    if (!audio_file) {
        fprintf(stderr, "Audio file not found: %s\n", audio);
        return;
    }
	
	// Construct the command to play the audio file
	char command1[1024];
	sprintf(command1, "mpv \"%s\"", audio);

	// Construct the cron command to schedule the audio play
	char cron_command[2048];
	sprintf(cron_command, "DISPLAY=:0 && PULSE_SERVER=tcp:127.0.0.1 && (crontab -l ; echo \"*/%d * * * * %s\") | crontab -", minutes, command1);

	system(cron_command);
	
}

void hexdump(struct command_t *command) {
    // Read the input from stdin 
    int fd;
	int group_size;
    if (command->arg_count == 0) {
        fd = STDIN_FILENO;
    } else {
        fd = open(command->args[3], O_RDONLY);
        if (fd < 0) {
			printf("%s", command->args[3]);
            printf("-%s: %s: %s: %s\n", sysname, command->name, "input.txt",
				   strerror(errno));
            exit(0);
        }
    }
    // parse the command 
    if (command->arg_count > 1) {
        group_size = atoi(command->args[2]);
    }
	// group size equal to 16 divided by the given number
	// 16 is the number of bytes represented in each line
	group_size = 16 / atoi(command->args[2]);
	uint8_t buffer[group_size];
	int bytes_read;
	int offset = 0;
	while ((bytes_read = read(fd, buffer, group_size)) > 0) {
		// prints the offset of the first byte in each line
		printf("%08x ", offset); // prints hexadecimal numbers

		offset += bytes_read;
		for (int i = 0; i < bytes_read; i++) {
			// 02 means that the number will be printed with at least 2 digits
			printf("%02x ", buffer[i]);// 02x is the format specifier for printing hexadecimal numbers
		}
		printf("|\n");
	}
	close(fd);
}

int main() {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}
		
		free_command(command);

	}
	printf("\n");
	return 0;
}


int process_command(struct command_t *command) {
	int r;
	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 0) {
			r = chdir(command->args[0]);
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}

			return SUCCESS;
		}
	}
	if(strcmp(command->name, "alias") == 0) {
		alias(command);
		return SUCCESS;
	}
	if(strcmp(command->name, "hexdump") == 0) {
		hexdump(command);
		return SUCCESS;	
	}
	if(strcmp(command->name, "game") == 0) {
		sude(command);
		return SUCCESS;	
	}
	if(strcmp(command->name, "good_morning") == 0) {
		good_morning(command);
		return SUCCESS;	
	}
	if(strcmp(command->name, "lara") == 0) {
		lara();
		return SUCCESS;	
	}
	if(strcmp(command->name, "psvis") == 0) {
		psvis(command);
		return SUCCESS;	
	}
	
	FILE *fp = fopen("aliases.txt", "r");
	if (fp == NULL) {
		fp = fopen("aliases.txt", "w");
		fclose(fp);
	}
	else {
		char line[100];
		while(fgets(line, 100, fp) != NULL) {
			char *token_line = strtok(line, "/n");
			char *token = strtok(token_line, " ");
			//printf("%s\n", token_line);
			if (strcmp(token, command->name) == 0) {
				struct command_t *command_alias = malloc(sizeof(struct command_t));
				memset(command_alias, 0, sizeof(struct command_t));
				char *command_after_token = strtok(NULL, "\n");
				//printf("%s\n", token_line);
				//printf("%s\n", command_after_token);
                parse_command(command_after_token, command_alias);
				pid_t pid = fork();
				if (pid == 0) {
					exec_command(command_alias);
					exit(0);

				} else {
					wait(0);
					return SUCCESS;
				}
			}
		}
	}
	fclose(fp);
	pid_t pid = fork();
	if (pid == 0) {
		exec_command(command);
		
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// TODO: do your own exec with path resolving using execv()
		// do so by replacing the execvp call below
		//execvp(command->name, command->args); // exec+args+path
		exit(0);

	} else {
		// TODO: implement background processes here

		wait(0);
		return SUCCESS;
	}



	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}