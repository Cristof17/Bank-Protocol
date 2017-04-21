#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>


/*
 * Decoding instructions
 */
#define LOGIN_CMD 10
#define DEFAULT_CMD 1
#define LOGIN_BRUTE_FORCE 2
#define QUIT_CMD 3
#define LOGOUT_CMD 4
#define LISTSOLD_CMD 5
#define GET_MONEY_CMD 6
#define PUT_MONEY_CMD 7
#define UNLOCK_CMD 8

/*
 * Responses
 */
#define SUCCESS 100000
#define ALREADY_LOGGED_IN -2
#define CARD_NO_INEXISTENT -4
#define WRONG_PIN -3

#define NOT_LOGGED_IN -10
#define LOGOUT_INVALID_USER -1
#define LOGOUT_SUCCESSFUL 1001
#define UNLOCK_ERROR 101
#define UNLOCK_SUCCESSFUL 102

/*
 * Globals
 */
#define BUFLEN 255
#define MAX_USERS 200
#define CHUNK_SIZE 4096

/*
 * Erros
 */
#define FILE_NOT_FOUND -20

#define LOGGED_IN 1
#define LOGGED_OUT 0

using namespace std;

typedef struct login_params {
	long card_no;
	int pin;
} login_params_t;

typedef struct file {
	char filename[BUFLEN];
	long size;
	bool shared;
} file_t;

typedef struct user{
	int fd;
	char *name;
	char *surname;
	long card_no;
	int pin;
	char *password;
	float balance;
	int login_attempts;
	int logged_in;
} user_t;

int server_sock;
int client_sock;
struct sockaddr_in server_addr;
struct sockaddr_in client_addr;

/*
 * Result for any opperation for debug purposes
 */
int result;

/*
 * FD_SETS
 */ 
 int fdmax;
 fd_set original;
 fd_set modified;


 FILE *user_file; 
 FILE *shared_file;


 /*
  * Login attempt monitor
  */
int login_attempt;

user_t **users;
int user_count;

/*
 * These are for getting file information 
 * from folders because each user
 * has an array of files, that it can access
 * and by having the array of unique users
 * I can print all the files from a user folder
 */
long *blocked_cards;
int blocked_cards_no;

//put the card_no in the array of blocked cards
void block_card(login_params *params){
	if (blocked_cards == NULL)
		blocked_cards = (long *)calloc(200,sizeof(long));
	blocked_cards[blocked_cards_no] = params->card_no;
}

//to unblock a card you just need to set the value to -1 for a given cardno
int unlock(long card_no, int pin){
	int i = 0;
	int j = 0;
	if (blocked_cards == NULL)
		return UNLOCK_ERROR;
	for (i = 0; i < blocked_cards_no; ++i){
		//check if the card is blocked
		if (blocked_cards[i] == card_no){
			//iterate through the users to check
			//if the pin is ok for the given card	
			//curr holds the number of users
			for (j = 0; j < user_count; ++j){
				if (users[j]->card_no == card_no &&
					users[j]->pin == pin){
					blocked_cards[i] = -1;
					printf("Unblocking card\n");
					return UNLOCK_SUCCESSFUL;		
				}
			}
		}
	}
	return UNLOCK_ERROR;
}

int get_command_code(char *command)
{
	if (strcmp(command, "login") == 0)
		return LOGIN_CMD;
	else if (strcmp(command, "quit") == 0)
		return QUIT_CMD;
	else if (strcmp(command, "logout") == 0)
		return LOGOUT_CMD;
	else if (strcmp(command, "listsold") == 0)
		return LISTSOLD_CMD;
	else if (strcmp(command, "getmoney") == 0)
		return GET_MONEY_CMD;
	else if (strcmp(command, "putmoney") == 0)
		return PUT_MONEY_CMD;
	else if (strcmp(command, "unlock") == 0)
		return UNLOCK_CMD;

	return DEFAULT_CMD;
}

//int the pos variable it is returned the position
//in the array of the user that has recently logged in
int login(login_params_t *params, int *pos)
{
	int i = 0;
	int status = CARD_NO_INEXISTENT;
	for (i = 0; i < user_count; ++i){
		if (users[i]->card_no == params->card_no){
			if (users[i]->pin == params->pin){
				users[i]->logged_in = LOGGED_IN;	
				users[i]->login_attempts = 0;
				if (users[i]->logged_in == LOGGED_IN){
					status = ALREADY_LOGGED_IN;
				} else {
					status = SUCCESS;
					*pos = i;
				}
			} else {
				if (users[i]->login_attempts >= 3) {
					status = LOGIN_BRUTE_FORCE;
				}
				else {
					users[i]->login_attempts++;
					status = WRONG_PIN;
				}
			}
		}
	}
	return status;
}

int logout(int user_connection)
{
	/*
	 * If the user has not been authenticated
	 * it means he does not exist in the users list
	 */
	if (users[user_connection] == NULL)
		return LOGOUT_INVALID_USER;
	user_t *user = users[user_connection];
	/*
	 * The user exists and we must log him out
	 * which means deleting the reference;
	 */
	free(user->name);
	user->name = NULL;
	free(user->surname);
	user->surname = NULL;
	free(user->password);
	user->password = NULL;
	 free(user);
	 user = NULL;
	 users[user_connection] = NULL;
	 return LOGOUT_SUCCESSFUL;
}
void send_client_code(int fd, int code)
{
	char buf[BUFLEN];
	memset(buf, 0, BUFLEN);
	sprintf(buf, "%d", code);
	printf("Sending %d code %s\n", fd, buf);
	send(fd, buf, BUFLEN, 0);
}

void send_client_message(int fd, char *message)
{
	char buf[BUFLEN];
	memset(buf, 0, BUFLEN);
	memcpy(buf, message, BUFLEN);
	send(fd, buf, BUFLEN, 0);
}

/*
 * Get the list of users from the file
 */
void get_users_from_file(user_t **out)
{
	int N = 0;
	/*
	 * Position cursor at the beginning of the file
	 * (Others calls might have moved the cursor)
	 */
	fseek(user_file, 0, SEEK_SET);
	/*
	 * Get the number of users allowed and
	 * alloc an array of size <number_of_users>
	 */
	fscanf(user_file, "%d", &N);
	printf("read N = %d\n", N);
	/*
	 * Read the users. (just the first word of each row)
	 */
	 for (int i = 0; i < N; ++i) {
		out[user_count] = (user_t *)malloc(1 * sizeof(user_t));
		out[user_count]->name = (char *) malloc(BUFLEN * sizeof(char));
		fscanf(user_file, "%s %s %ld %i %s %f", out[user_count]->name, out[user_count]->surname,
											out[user_count]->card_no, out[user_count]->pin, out[user_count]->password,
											out[user_count]->balance);
		printf("Entering user with %s %s %ld %d %s %f\n", 
			out[user_count]->name,
			out[user_count]->surname,
			out[user_count]->card_no,
			out[user_count]->pin,
			out[user_count]->password,
			out[user_count]->balance
		);
		user_count++;
	 }
	 printf("Finished reading users from file \n");
}


int get_users_from_file_count()
{
	int N;
	fseek(user_file, 0, SEEK_SET);
	fscanf(user_file, "%d", &N);
	return N;
}

void create_users()
{
	int N = get_users_from_file_count();
	user_t **users = (user_t **)malloc(N * sizeof(user_t*));
	get_users_from_file(users);
}

user_t *get_user_by_name(char *name)
{
	int users_count = get_users_from_file_count();
	if (name == NULL)
		return NULL;
	for (int i = 0; i < users_count; ++i) {
		if (users[i] == NULL)
			continue;
		if (strcmp(users[i]->name, name) == 0){
			return users[i];
		}
	}
	return NULL;
}

int main(int argc, char ** argv)
{
	if (argc <= 3){
		perror("./server <port> <user_file> <shared_file>");
		exit(1);
	}
	
	/*
	 * Open files
	 */
	user_file = fopen(argv[2], "r");
	if (user_file == NULL)
		perror("Cannot open user_file");
	shared_file = fopen(argv[3],"r");
	if(shared_file == NULL)
		perror("Cannot open shared_file");
	
	/*
	 * Create user files
	 */
	create_users();	

	/*
	 * Open socket
	 */
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == 0) {
		perror("Cannot open first socket \n");
		exit(1);
	}


	/*
	 * Setup the address and port for server to listen on
	 */
	 memset((char *) &server_addr, 0, sizeof(server_addr));
	 server_addr.sin_family = AF_INET;
	 server_addr.sin_addr.s_addr = INADDR_ANY;
	 server_addr.sin_port = htons (atoi(argv[1]));
	
	/*
	 * Bind socket to address and  port
	 */

	result = bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (result < 0) {
		perror( "Cannot bind socket to address \n");
		exit(1);
	}
	
	/*
	 * Call listen call
	 */
	result = listen(server_sock, MAX_USERS);
	if (result < 0){
		perror("Cannot listen on socket");
		exit(1);
	}


	/*
	 * Do the select statement
	 */
 	FD_ZERO(&original);
 	FD_ZERO(&modified);

 	if(server_sock > fdmax)
		fdmax = server_sock;
	
	FD_SET(fdmax, &original);
	FD_SET(STDIN_FILENO, &original);



	/*
	 * Create unique users list
	 */
	if (users == NULL)
		users = (user_t **)malloc(MAX_USERS * sizeof(user_t *));
	get_users_from_file(users);
	
	/*
	 * Listen for incoming connections
	 */
	while (1) {
		modified = original;
		if (select(fdmax + 1, &modified, NULL, NULL, NULL) == -1) 
			perror("ERROR in select");
	
		int i;
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &modified)) {
			
				if (i == server_sock) {
					// a venit ceva pe socketul inactiv(cel cu listen) = o noua conexiune
					// actiunea serverului: accept()
					int clilen = sizeof(client_addr);
					int newsockfd;
					if ((newsockfd = accept(server_sock, (struct sockaddr *)&client_addr,(unsigned int *) &clilen)) == -1) {
						perror("ERROR in accept");
					} 
					else {
						//adaug noul socket intors de accept() la multimea descriptorilor de citire
						FD_SET(newsockfd, &original);
						if (newsockfd > fdmax) { 
							fdmax = newsockfd;
						}
					}
					printf("Noua conexiune la server \n");
				}

				else if (i == STDIN_FILENO) {
					char buffer[BUFLEN];
					memset(buffer, 0, BUFLEN);
					fgets(buffer, BUFLEN, stdin);
					if (strcmp(buffer, ""))
						continue;
					char *tok = strtok(buffer, " \n");
					result = get_command_code(tok);
					switch (result) {
						case QUIT_CMD:
						{
							close (server_sock);
							exit(0);
							break;
						}
						default:
							break;
					}
					printf("Command %s came \n", buffer);
				}
					
				else {
					// am primit date pe unul din socketii cu care vorbesc cu clientii
					//actiunea serverului: recv()
					char buffer[BUFLEN];
					memset(buffer, 0, BUFLEN);
					if ((result = recv(i, buffer, sizeof(buffer), 0)) <= 0) {
						if (result == 0) {
							//conexiunea s-a inchis
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("ERROR in recv");
						}
						close(i); 
						FD_CLR(i, &original); // scoatem din multimea de citire socketul pe care 
					} 
					
					else { //recv intoarce >0
						printf ("\nAm primit de la clientul de pe socketul %d, mesajul: %s", i, buffer);
						char *command = strtok(buffer, " \n");
						printf("command = %s\n", command);
						int code = get_command_code(command);
						int user_fd = 0;
						switch (code) {
							case LOGIN_CMD:
							{
								login_params_t params;
								memset(&params, 0, sizeof(login_params_t));
								char *tok = strtok(NULL, " \n");
								if (tok != NULL){
									long card_no = atol(tok);
									params.card_no = card_no;
									tok = strtok(NULL, " \n");
									if (tok != NULL){
										int pin = atoi(tok);
										params.pin = pin;
									}
								}
								result = login(&params, &user_fd);
								switch (result) {
									case SUCCESS:
									{	
										send_client_code(i, SUCCESS); 
										users[user_fd]->fd = i;
										printf("New user->fd = %d\n", users[i]->fd);
										break;
									}

									case ALREADY_LOGGED_IN:
									{
										printf("-2 Sesiune deja deschisa \n");
										send_client_code(i, ALREADY_LOGGED_IN);
										break;
									}

									case LOGIN_BRUTE_FORCE:
									{
										printf("%d Brute force detectat\n", LOGIN_BRUTE_FORCE);
										send_client_code(i, LOGIN_BRUTE_FORCE);
										block_card(&params);
										break;
									}
									case WRONG_PIN:
									{
										printf("%d -3 Pin Gresit\n", WRONG_PIN);
										send_client_code(i, WRONG_PIN);
										break;
									}
									default:
										break;
								}
								break;
							} //endcase LOGIN_CMD;
							case LOGOUT_CMD:
							{
								/*
								 *
								 */
								 result = logout(i);
								 switch(result) {
									case LOGOUT_INVALID_USER:
									{
										printf("-1 Clientul nu este autentificat");
										send_client_code(i, -1);
										break;
									}
									case LOGOUT_SUCCESSFUL:
									{
										printf("Logout successfull\n");
										send_client_code(i, LOGOUT_SUCCESSFUL);
										break;
									}
									default:
										break;
								 }
								 break;
							}
							default:
							{
								break;
							}
						} //endswitch code
					}//end else recv
				} 
			}
		}
     }
	
	/*
	 * Closing remarks
	 */
	close(server_sock);
	return 0;	
}
