#include <iostream>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

#define BUFLEN 255


/*
 * Responses
 */
#define SUCCESS 100000
#define LOGIN_BRUTE_FORCE -5
#define ALREADY_LOGGED_IN -2
#define NOT_LOGGED_IN -10

#define QUIT_CMD 10
#define DEFAULT_CMD 1
#define UPLOAD_CMD 2
#define LOGIN_CMD 11
#define UNLOCK_CMD 8
#define LISTSOLD_CMD 9
#define CARD_NO_INEXISTENT -4
#define WRONG_PIN -3
#define UNLOCK_ERROR 101
#define UNLOCK_SUCCESSFUL 102
#define UNLOCK_INEXISTENT_CARD_NO -4
#define UNLOCK_WRONG_PIN -7
#define UNLOCK_REQUEST_PIN 10102
#define UNLOCK_UNBLOCKED_CARD -6
#define LISTSOLD_SUCCESSFUL 12
#define GET_MONEY_NOT_MULTIPLE -9                                                  
#define GET_MONEY_SUMM_TOO_LARGE -8 
#define GET_MONEY_SUCCESSFUL 1231
#define PUT_MONEY_SUCCESSFUL 1232


#define LOGOUT_INVALID_USER -1
#define LOGOUT_SUCCESSFUL 1001
#define UNKNOWN_USER -11

#define LOGGED_IN 1
#define LOGGED_OUT 0

/*
 * All about server socket
 */
int sockfd;
int unlock_fd;
int client_file_fd;
struct sockaddr_in server_addr_tcp;
struct sockaddr_in server_addr_udp;
struct sockaddr_in receive_addr_udp;

/*
 * Variables for local purposes
 */
int result;
char buffer[BUFLEN];
/*
 * Open session
 */
char card_no[BUFLEN];
char pin[BUFLEN];
char credentials[BUFLEN];
int logged_in;

/*
 * For select call
 */
int fdmax;
fd_set original;
fd_set modified;


int get_command_code(char *command)
{
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	memcpy(copy, command, BUFLEN);
	tok = strtok(copy, " \n");
	if (strcmp (tok, "quit") == 0)
		return QUIT_CMD;
	if (strcmp (tok, "upload") == 0)
		return UPLOAD_CMD;
	if (strcmp (tok, "unlock") == 0)
		return UNLOCK_CMD;
	if (strcmp(tok, "login") == 0)
		return LOGIN_CMD;
	return DEFAULT_CMD;
}

void get_login_credentials(char *command, char *out_card_no, char *out_pin){
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	memcpy(copy, command, BUFLEN);
	//that is the command name
	tok = strtok(copy, "\t\n ");
	//that is the first argument
	tok = strtok(NULL, "\t\n ");
	if (tok != NULL){
		strcpy(out_card_no, tok);
	} else {
		return;
	}
	//that is the second argument
	tok = strtok(NULL, "\n\t ");
	if (tok != NULL){
		strcpy(out_pin, tok);
	}
	return;
}

int get_unlock_response(char *message){
	char copy[BUFLEN];
}

void get_argument(char *command, char **out){
	if (*out == NULL)
		(*out) = (char *)malloc(BUFLEN * sizeof(char));
	memset(*out, 0, BUFLEN);
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	memcpy(copy, command, BUFLEN);
	tok = strtok(copy, " \n");
	tok = strtok(NULL, " \n");
	memcpy((*out),tok, strlen(tok)); 
}

bool check_buffer_empty(char *buffer)
{
	char copy[BUFLEN];
	char *tok;

	memset(copy, 0, BUFLEN);
	memcpy(copy, buffer, BUFLEN);
	tok = strtok(copy, " \n");
	if (tok == NULL)
		return true;
	return false;
}

void get_unlock_response_code(char *buffer, char *code){
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	strcpy(copy, buffer);
	/*
	 * Get the first number that appears in the buffer
	 */
	tok = strtok(copy, " \n\t");	
	strcpy(code, tok);
}

void write_log(char *buff)
{
	write(client_file_fd, buff, strlen(buff));		
}

int main(int argc, char ** argv)
{
	logged_in = LOGGED_OUT;
	if (argc <= 2){
		perror("./client <server IP> <server PORT> \n");
		exit(1);
	}


	/*
	 * Socket open
	 */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd <= 0) {
		perror ("Cannot open client socket");
		exit(1);
	}

	/*
	 * Socket open
	 */
	unlock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd <= 0) {
		perror ("Cannot open client socket");
		exit(1);
	}

	/*
	 * Setup the socket
	 */
	memset(&server_addr_tcp, 0, sizeof(server_addr_tcp));
	server_addr_tcp.sin_family = AF_INET;
	server_addr_tcp.sin_addr.s_addr = inet_addr(argv[1]);
	server_addr_tcp.sin_port = htons(atoi(argv[2]));

	/*
	 * Setup the socket
	 */
	memset(&server_addr_udp, 0, sizeof(server_addr_udp));
	server_addr_udp.sin_family = AF_INET;
	server_addr_udp.sin_addr.s_addr = inet_addr(argv[1]);
	server_addr_udp.sin_port = htons(atoi(argv[2]));

	memset(&receive_addr_udp, 0, sizeof(receive_addr_udp));


	/*
	 * Connect to server
	 */ 

	int size = sizeof(server_addr_tcp);
	result = connect(sockfd, (struct sockaddr *) &server_addr_tcp, size);
	if (result < 0) {
		perror("Cannot connect to server ");
		exit(1);
	}

	/*
	 * Create the client file
	 */

	int pid = getpid();
	char name[BUFLEN];
	sprintf(name, "client-%d.log", pid);
	client_file_fd = open(name, O_CREAT | O_TRUNC | O_RDWR | O_APPEND , 0644);
	FILE * client_file = fdopen(client_file_fd, "rw");
	if (client_file == NULL)
		perror("Cannot create file");

	memset(buffer, 0 , sizeof(buffer));
	memset(credentials, 0, sizeof(credentials));

	/*
	 * Do what select is meant to do
	 */
	FD_SET(STDIN_FILENO, &original);
	FD_SET(sockfd, &original);
	FD_SET(unlock_fd, &original);

	if (sockfd > fdmax)
		fdmax = sockfd;
	if (unlock_fd > fdmax)
		fdmax = unlock_fd;

	while(1) {

		modified = original;
		fputs(">", stdout);
		fflush(stdout);
		/*
		 * Listen for commands
		 */
		result = select(fdmax + 1, &modified, NULL, NULL, NULL);
		if (result < 0) {
			perror("Error in select \n");
			exit(0);
		}
		
		for (int i = 0; i <= fdmax; ++i) {
			/*
			 * Ifock_sizeI need to read from stdin
			 */
			if (i == STDIN_FILENO && FD_ISSET(i, &modified)) {
				fgets(buffer, BUFLEN, stdin);
				if (check_buffer_empty(buffer) || (strlen(buffer) <= 1)) {
					printf("Buffer is empty\n");
					memset(buffer, 0 , BUFLEN);
					continue;
				}
				/*
				 * Quit
				 */
				if (get_command_code(buffer) == QUIT_CMD) {
					/*
					 * Let the server know you want to close the
					 * connection
					 */
					send(sockfd, buffer, BUFLEN, 0);
					/*
					 * Close local connection
					 */
					close(sockfd);
					exit(0);
				}
				if (get_command_code(buffer) == LOGIN_CMD){
					//get the credentials so that unlock
					//would know what user is going to connect
					get_login_credentials(buffer, card_no, pin);
					printf("Login credentials = %s %s\n", card_no, pin);
					char command[BUFLEN];
				}
				if (get_command_code(buffer) == UNLOCK_CMD){
					//put " " instead of \n
					buffer[strlen(buffer) -1] =' ';	
					strcat(buffer, card_no);
					//send_to on udp sock	et
					socklen_t sock_size = (socklen_t) sizeof(server_addr_udp);
					sendto(unlock_fd, buffer, BUFLEN, 0,
						(struct sockaddr *)&server_addr_udp, sock_size); 
					/*
					 * get the response for the unlock command
					 */
					recvfrom(unlock_fd, buffer, BUFLEN, 0,
							(struct sockaddr*)& receive_addr_udp, &sock_size); 
					/*
					 * the buffer should contain just a number
					 */
					char code[BUFLEN];
					memset(code, 0, BUFLEN);
					get_unlock_response_code(buffer, code);
					switch (atoi(code)){
						case UNLOCK_REQUEST_PIN:
						{
							/* 
							 * Request pin
							 */
							char message[] = "UNLOCK> Trimite parola secreta\n>";
							char pin[BUFLEN];
							fputs(message, stdout);
							write_log(message);
							/*
							 * Get pin from input
							 */
							memset(buffer, 0, BUFLEN);
							strcat(buffer, card_no); 
							strcat(buffer, " ");
							/*
							 * Replace \0 with a space 
							 */
							fgets(pin, BUFLEN, stdin);
							/*
							 * Append it to the credentials
							 */
							strcat(buffer, pin);
							/*
							 * Send pin to server
							 */
							sendto(unlock_fd, buffer, BUFLEN, 0,
									(struct sockaddr *)&server_addr_udp,
									sizeof(struct sockaddr));
							/*
							 * Wait to check if the pin is ok
							 */
							 memset(buffer, 0, BUFLEN);
							 recvfrom(unlock_fd, buffer, BUFLEN, 0,
							 		(struct sockaddr *)&receive_addr_udp,
									&sock_size);
							/*
							 * Get the response code
							 */ 
							memset(code, 0, BUFLEN);
							get_unlock_response_code(buffer, code);
							switch(atoi (code)){
								case UNLOCK_SUCCESSFUL:
								{
									
									char message[] = "UNLOCK> Client deblocat\n";
									write_log(message);
									fputs(message, stdout);
									memset(buffer, 0, BUFLEN);
									break;
								}
								case UNLOCK_WRONG_PIN:
								{
									char message[] = "UNLOCK> -7 : Deblocare esuata\n";
									write_log(message);
									fputs(message, stdout);
									memset(buffer, 0, BUFLEN);
									break;
								}
								case UNLOCK_ERROR:
								{
									char message[] = "UNLOCK> -6 : Operatie esuata\n";
									write_log(message);
									fputs(message, stdout);
									memset(buffer, 0, BUFLEN);
									break;
								}
							}
							break;
						}
						case UNLOCK_UNBLOCKED_CARD:
						{
							/*
							 * Card is not blocked
							 */
							char message[] = "UNLOCK> -6 : Operatie esuata\n";
							write_log(message);
							fputs(message, stdout);
							memset(buffer, 0, BUFLEN);
							break;
						}
					}
					continue;
				}
				/*
				 * Send the command to server
				 */
				send(sockfd, buffer, BUFLEN, 0);
				/*
				 * Log the command
				 */
				write_log(buffer);
				memset(buffer, 0, BUFLEN);
			}

			else if (i == unlock_fd && FD_ISSET(i, &modified)) {
				printf("Am ceva pe UDP În client\n");
				memset(buffer, 0, BUFLEN);
				socklen_t struct_size =(socklen_t)sizeof(receive_addr_udp);
				recvfrom(unlock_fd, buffer, BUFLEN, 0,
						(struct sockaddr *)&receive_addr_udp, &struct_size);
				printf("Am primit raspuns pe udp %s\n", buffer);
				//check the response

			}
			
			else if (i == sockfd && FD_ISSET(i, &modified)) {
				printf("am ceva in sockfd\n");
				/*
				 * If the buffer is empty, it means
				 * I have not send any information and that
				 * what I received from stdin was empty
				 */
				if (check_buffer_empty(buffer) || (strlen(buffer) <= 1)) {
					memset(buffer, 0 , BUFLEN);
					continue;
				}
				memset(buffer, 0, BUFLEN);
				result = recv(sockfd, buffer, BUFLEN, 0);
				printf("Received %d bytes\n", result);
				printf("Received = %s on %d\n", buffer, i);
				printf("Atoi(buffer) = %d\n", atoi(buffer));
				switch (atoi(buffer)) {
					case SUCCESS:
					{
						printf("Login successful \n");
						/*
						 * Get username from server for prompt
						 */
						char message[] = "ATM> Login successful\n";
						write_log(message);
						logged_in = LOGGED_IN;
						memset(buffer, 0, BUFLEN);
						break;
					}
					case LOGIN_BRUTE_FORCE:
					{
						printf("ATM> -5 : Card blocat\n");
						char message[] = "ATM> -5 : Card blocat\n";
						write_log(message);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case ALREADY_LOGGED_IN:
					{
						char message[] = "ATM> -2 : Sesiune deja deschisa\n";
						fputs(message, stdout);
						write_log(message);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case LOGOUT_INVALID_USER:
					{
						char message[] = "ATM> -1 : Clientul nu e autentificat\n";
						fputs(message, stdout);
						write_log(message);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case LOGOUT_SUCCESSFUL:
					{
						memset(buffer, 0, BUFLEN);
						//clear the saved credentials
						memset(card_no, 0, BUFLEN);
						memset(pin, 0, BUFLEN);
						logged_in = LOGGED_OUT;
						memset(buffer, 0, BUFLEN);
						break;
					}
					case NOT_LOGGED_IN:
					{
						char message[] = "ATM> -11 : Utilizator inexistent\n";
						fputs(message, stdout);
						write_log(message);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case CARD_NO_INEXISTENT:
					{
						char message[] = "ATM> -4 : Numar card inexistent\n";
						fputs(message, stdout);;
						write_log(message);
						fputs(message, stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case WRONG_PIN:
					{
						char message[] = "ATM> -3 : Pin gresit\n";
						fputs(message, stdout);
						write_log(message);
						fputs(message, stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case LISTSOLD_SUCCESSFUL:
					{
						/*
						 * Append ATM at the begining og the message
						 */
						memset(buffer, 0, BUFLEN);
						char message[BUFLEN];
						memset(message, 0, BUFLEN);
						sprintf(message, "ATM> ");
						/*
						 * Get the numbers from the server
						 */
						recv(i, buffer, BUFLEN, 0);
						strcat(message, buffer);
						strcat(message, "\n");
						/*
						 * Log and print
						 */
						write_log(message);
						fputs(message,stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case GET_MONEY_NOT_MULTIPLE:
					{
						char message[] = "ATM> -9 : Suma nu e multiplu de 10\n";
						write_log(message);
						fputs(message, stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case GET_MONEY_SUMM_TOO_LARGE:
					{
						char message[] = "ATM> -8 : Fonduri insuficiente\n";
						write_log(message);
						fputs(message, stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case GET_MONEY_SUCCESSFUL:
					{
						char message[BUFLEN];
						memset(message, 0, BUFLEN);
						sprintf(message, "ATM> Suma ");
						strcat(message, " retrasa cu succes\n");
						write_log(message);
						fputs(message, stdout);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case PUT_MONEY_SUCCESSFUL:
					{
						char message[] = "Suma depusa cu succes\n";
						write_log(message);
						fputs(message, stdout);
						memset(message, 0, BUFLEN);
						memset(buffer, 0, BUFLEN);
						break;
					}
					case DEFAULT_CMD:
					{
						printf("Command not recognized\n");
						memset(buffer, 0, BUFLEN);
						break;
					}
					default:
						break;
				}
			}

			else {
				//result = read(sockfd, buffer, BUFLEN);
				if (result <= 0) {
					perror("Error when client receiving\n");
				}
				//printf("Received %s from %d \n", buffer, i);
			}
		}
	}

	fclose(client_file);
	close(client_file_fd);

	return 0;

}

