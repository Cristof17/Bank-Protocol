
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
#define LOGIN_BRUTE_FORCE -5
#define QUIT_CMD 3
#define LOGOUT_CMD 4
#define GET_MONEY_CMD 6
#define PUT_MONEY_CMD 7
#define UNLOCK_CMD 8
#define LISTSOLD_CMD 9
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
#define UNLOCK_INEXISTENT_CARD_NO -4
#define UNLOCK_WRONG_PIN -7
#define UNLOCK_REQUEST_PIN 10102
#define UNLOCK_BLOKED 1
#define UNLOCK_UNBLOCKED 0
#define UNLOCK_UNBLOCKED_RESPONSE -6
#define LISTSOLD_SUCCESSFUL 12
#define GET_MONEY_NOT_MULTIPLE -9
#define GET_MONEY_SUMM_TOO_LARGE -8
#define GET_MONEY_SUCCESSFUL 1231
#define PUT_MONEY_SUCCESSFUL 1232
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

#define CARD_NO_EXISTS 1
#define CARD_NO_NOT_EXISTS 0

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
	double balance;
	int login_attempts;
	int logged_in;
} user_t;

int server_sock; //TCP Socket
int unlock_sock ; //UDP Socket
int client_sock;
struct sockaddr_in server_addr_tcp;
struct sockaddr_in server_addr_udp;
struct sockaddr_in client_addr_udp;
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
	blocked_cards_no ++;
}

int check_unlock_card_no(char *card_no){
	long card_no_long = 0;
	int i = 0;
	card_no_long = atol(card_no);
	for (i = 0; i < user_count; ++i){
		if (users[i]->card_no == card_no_long)
			return CARD_NO_EXISTS;
	}
	return CARD_NO_NOT_EXISTS;
}

int get_fd_for_card_no(long card_no){
	int i = 0;
	for (i = 0; i < user_count; ++i){
		if (users[i] != NULL){
			if (users[i]->card_no == card_no){
				return users[i]->fd;
			}
		}
	}
	return -1;
}

//to unblock a card you just need to set the value to -1 for a given cardno
int unlock(long card_no, char *password){
	int i = 0;
	int j = 0;
	char password_copy[BUFLEN];
	char *password_tok;
	memset(password_copy, 0, BUFLEN);
	strcpy(password_copy, password);
	if (password != NULL){
		password_tok = strtok(password_copy, " \n\t");	
	}
	if (blocked_cards == NULL)
		return UNLOCK_ERROR;
	for (i = 0; i < blocked_cards_no; ++i){
		//check if the card is blocked
		if (blocked_cards[i] == card_no){
			//iterate through the users to check
			//if the pin is ok for the given card	
			//curr holds the number of users
			for (j = 0; j < user_count; ++j){
				if (users[j]->card_no == card_no){
					if (strcmp(users[j]->password, password_tok) == 0){
						users[j]->login_attempts = 0;
						blocked_cards[i] = -1;
						blocked_cards_no --;
						printf("UNBLOCK_SUCCESSFUL\n");
						return UNLOCK_SUCCESSFUL;
					} else {
						printf("UNBLOCK SUCCESSFUL\n");
						return UNLOCK_WRONG_PIN;
					}		
				}
			}
		}
	}
	return UNLOCK_UNBLOCKED_RESPONSE;
}

int is_blocked(char *card_no){

	int i = 0;
	for (i = 0; i < blocked_cards_no; ++i){
		if (blocked_cards[i] == atol(card_no)){
			return UNLOCK_BLOKED;
		}
	}
	return UNLOCK_UNBLOCKED;
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

	return DEFAULT_CMD;
}

int check_if_unlock_cmd(char *command){
	char copy[BUFLEN];
	memset(copy, 0, BUFLEN);
	strcpy(copy, command);
	char *tok = strtok(copy, " ");
	if (strcmp(tok, "unlock") == 0){
		return 1;
	}
	return 0;
}

void get_unlock_card_no(char *command, char *card_no){
	char copy[BUFLEN];
	memset(copy, 0, BUFLEN);
	char *tok;
	strcpy(copy, command);
	tok = strtok(copy, " \t\n");
	if (tok != NULL){
		tok = strtok(NULL, " \t\n");
		if (tok != NULL)
			strcpy(card_no, tok);
	}
}

//int the pos variable it is returned the position
//in the array of the user that has recently logged in
int login(login_params_t *params, int *pos)
{
	int i = 0;
	int status = CARD_NO_INEXISTENT;
	for (i = 0; i < user_count; ++i){
		if (users[i]->card_no == params->card_no){
			if (users[i]->logged_in == LOGGED_IN){
				status = ALREADY_LOGGED_IN;
			} else {
				if (users[i]->pin == params->pin){
						users[i]->logged_in = LOGGED_IN;	
					users[i]->login_attempts = 0;
					status = SUCCESS;
					*pos = i;
					printf("Scanning user %s %s\n", users[i]->name, users[i]->surname);
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
	}
	return status;
}

int logout(int user_connection)
{
	/*
	 * If the user has not been authenticated
	 * it means he does not exist in the users list
	 */
	user_t *user = NULL;
	int i = 0;
	for (i = 0; i < user_count; ++i){
		if (users[i] != NULL){
			if (users[i]->fd == user_connection)
				user = users[i];
		}
	}
	if (user == NULL)
		return LOGOUT_INVALID_USER;
	if (user->logged_in == LOGGED_OUT){
		return LOGOUT_INVALID_USER;
	}
	/*
	 * The user exists and we must log him out
	 * which means deleting the reference;
	 */
	user->logged_in = LOGGED_OUT;
	user->login_attempts = 0;
	user->fd = -1;
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

void send_udp_client_code(struct sockaddr *client_address, int fd, int code){
	char buffer[BUFLEN];
	memset(buffer, 0, BUFLEN);
	sprintf(buffer, "%d", code);
	int saddr_size = sizeof(struct sockaddr);
	sendto(fd, buffer, BUFLEN, 0, client_address, saddr_size); 
	return;
}

void send_client_message(int fd, char *message)
{
	char buf[BUFLEN];
	memset(buf, 0, BUFLEN);
	memcpy(buf, message, BUFLEN);
	send(fd, buf, BUFLEN, 0);
}

int get_user_pos_by_fd(int fd){
	int i = 0;
	for (i = 0; i < user_count; ++i){
		if (users[i]->fd == fd){
			return i;
		}
	}
	return -1;
}

int substract_from_balance(int fd, long summ){
	/*
	 * Get the user for the given fd
	 */
	int pos = get_user_pos_by_fd(fd);
	if (pos == -1)
		return NOT_LOGGED_IN;
	if (users[pos] == NULL){
		return NOT_LOGGED_IN;
	}
	if (summ % 10 != 0){
		return GET_MONEY_NOT_MULTIPLE;
	}
	if (summ > users[pos]->balance){
		return GET_MONEY_SUMM_TOO_LARGE;
	}
	users[pos]->balance = users[pos]->balance - summ;
	return GET_MONEY_SUCCESSFUL;
}

int put_summ_in_balance(int fd, long summ){
	/*
	 * Get the user for the given fd
	 */
	int pos = get_user_pos_by_fd(fd);
	if (pos == -1)
			return NOT_LOGGED_IN;
	if (users[pos] == NULL)
		return NOT_LOGGED_IN;
	users[pos]->balance += summ;
	return PUT_MONEY_SUCCESSFUL;
}

/*
 * Get the list of users from the file
 */
void get_users_from_file(user_t **out)
{
	int N = 0;
	char line[BUFLEN];
	char *tok;
	/*
	 * Position cursor at the beginning of the file
	 * (Others calls might have moved the cursor)
	 */
	fseek(user_file, 0, SEEK_SET);
	/*
	 * Get the number of users allowed and
	 * alloc an array of size <number_of_users>
	 */
	fscanf(user_file, "%d\n", &N);
	printf("read N = %d\n", N);
	/*
	 * Read the users. (just the first word of each row)
	 */
	 for (int i = 0; i < N; ++i) {
		fgets(line, BUFLEN, user_file);
		out[user_count] = (user_t *)malloc(1 * sizeof(user_t));
		out[user_count]->name = (char *)malloc(BUFLEN * sizeof(char));
		out[user_count]->surname = (char *)malloc(BUFLEN * sizeof(char));
		out[user_count]->password = (char *)malloc(BUFLEN * sizeof(char));
		//process the current line 
		//get the name
		tok = strtok(line, "\n\t ");
		strcpy(out[user_count]->name, tok);	
		//get the surname
		tok = strtok(NULL, "\n\t ");
		strcpy(out[user_count]->surname, tok);
		//get the card_no
		tok = strtok(NULL, "\n\t ");
		out[user_count]->card_no = atol(tok);
		//get the pin
		tok = strtok(NULL, "\n\t ");
		out[user_count]->pin = atoi(tok);
		//get the password
		tok = strtok(NULL, "\n\t ");
		strcpy(out[user_count]->password, tok);
		//get the balance
		tok = strtok(NULL, "\n\t ");
		out[user_count]->balance = atof(tok);
		

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
	 printf("Finished reading users from file user_count = %d \n", user_count);
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
	users = (user_t **)malloc(N * sizeof(user_t*));
	get_users_from_file(users);
}

int get_user_pos_by_name(char *name)
{
	int users_count = get_users_from_file_count();
	for (int i = 0; i < users_count; ++i) {
		if (users[i] == NULL)
			continue;
		if (strcmp(users[i]->name, name) == 0){
			return i;
		}
	}
	return -1;
}

void get_unlock_credentials(char *command, long *card_no, char *password)
{
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	strcpy(copy, command);
	tok = strtok(copy, " \n");
	if (tok != NULL){
		*card_no = atol(tok);
	}
	tok = strtok(NULL, "\n ");
	if (tok != NULL){	
		strcpy(password, tok);
	}
}

void set_fd_for_card_no(long card_no, int fd){
	int i = 0;
	for (i = 0; i < user_count; ++i){
		if (users[i]->card_no == card_no){
			users[i]->fd = fd;
		}
	}
}

void get_summ_from_command(char *command, long *summ){
	char copy[BUFLEN];
	char *tok;
	memset(copy, 0, BUFLEN);
	strcpy(copy, command);
	tok = strtok(copy, " \n\t");	
	if (tok != NULL){
		tok = strtok(NULL, " \n\t");
		if (tok != NULL){
			*summ = atol(tok);
		}
	}
}

int main(int argc, char ** argv)
{
	if (argc <= 2){
		perror("./server <port> <user_file>");
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
	 * Open TCP socket
	 */
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == 0) {
		perror("Cannot open first socket \n");
		exit(1);
	}
	
	/*
	 * Open UDP Socket for unlock calls
	 */
	unlock_sock = socket(AF_INET,SOCK_DGRAM,0);
	if (unlock_sock < 0){
		perror("Cannot open UDP Socket");	
		exit(1);
	}
		


	/*
	 * Setup the address and port for server to listen on
	 */
	 memset((char *) &server_addr_tcp, 0, sizeof(server_addr_tcp));
	 server_addr_tcp.sin_family = AF_INET;
	 server_addr_tcp.sin_addr.s_addr = INADDR_ANY;
	 server_addr_tcp.sin_port = htons (atoi(argv[1]));

	/*
	 * Setup the address and port for server to listen on
	 */
	 memset((char *) &server_addr_udp, 0, sizeof(server_addr_udp));
	 server_addr_udp.sin_family = AF_INET;
	 server_addr_udp.sin_addr.s_addr = INADDR_ANY;
	 server_addr_udp.sin_port = htons (atoi(argv[1]));
	
	/*
	 * Set clent address full of 0s
	 */
	 memset((char *)&client_addr_udp, 0, sizeof(client_addr_udp));
	 	
	/*
	 * Bind TCP socket to address and  port
	 */

	result = bind(server_sock, (struct sockaddr *) &server_addr_tcp, sizeof(server_addr_tcp));
	if (result < 0) {
		perror( "Cannot bind TCP socket to address \n");
		exit(1);
	}

	/*
	 * Bind UDP socket to address and port
	 */
	result = bind(unlock_sock, (struct sockaddr *) &server_addr_udp, sizeof(server_addr_udp));
	if (result < 0){
		perror("Cannot bind UDP Socket");
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
	if (unlock_sock > fdmax)
		fdmax = unlock_sock;
	
	FD_SET(server_sock, &original);
	FD_SET(unlock_sock, &original);
	FD_SET(STDIN_FILENO, &original);



	/*
	 * Create unique users list
	 */
	if (users == NULL)
		users = (user_t **)malloc(MAX_USERS * sizeof(user_t *));
	
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
				} else if (i == unlock_sock){
					printf("Aud ceva pe UDP \n");
					//unlock logic
					char buffer[BUFLEN];
					char card_no[BUFLEN];
					memset(buffer, 0, BUFLEN);
					socklen_t addr_size = (socklen_t)sizeof(server_addr_udp);
					result = recvfrom(unlock_sock, buffer, BUFLEN, 0,
							 (struct sockaddr*) &client_addr_udp, &addr_size);
					if (result < 0){
						perror("Did not receive on UDP socket");
						continue;
					} 
					/*
					 * Check if command is Unlock
					 */
					if (check_if_unlock_cmd(buffer)){
						printf("am primit %s\n", buffer);
						get_unlock_card_no(buffer, card_no);
						printf("Card no = %s\n", card_no);
						/*
						 * Check if the card_no exists
						 */
						if (check_unlock_card_no(card_no) == CARD_NO_EXISTS){
							//send UNLOCK_PASS_REQEUST
							printf("Trimit pe UDP cod de pin\n");
							if (!is_blocked(card_no)){
								printf("Card is not blocked");
								send_udp_client_code((struct sockaddr*)&client_addr_udp,
													unlock_sock, UNLOCK_UNBLOCKED_RESPONSE);
								continue;
							}
							send_udp_client_code((struct sockaddr*) &client_addr_udp,
												unlock_sock, UNLOCK_REQUEST_PIN);
							//use recvfrom to ge the card_no and pass
							memset(buffer, 0, BUFLEN);
							result = recvfrom(unlock_sock, buffer, BUFLEN, 0,
									 (struct sockaddr*) &client_addr_udp, &addr_size);
							if (result < 0){
								perror("Did not receive on UDP socket");
								continue;
							} 								//parse card_no and pin
							long card_no = 0;
							char password[BUFLEN];
							get_unlock_credentials(buffer, &card_no, password);
							printf("Received unlock credentials %ld %s \n", card_no, password);
							result = unlock(card_no, password);
							switch(result) {
								case UNLOCK_SUCCESSFUL:
								{
									send_udp_client_code((struct sockaddr*)&client_addr_udp, unlock_sock,
																		UNLOCK_SUCCESSFUL);
									printf("Sending unlock successful %d\n", UNLOCK_SUCCESSFUL);
									break;
								}
								case UNLOCK_ERROR:
								{
									send_udp_client_code((struct sockaddr*)&client_addr_udp, unlock_sock,
																	UNLOCK_ERROR);
									printf("Sending unlock error %d\n", UNLOCK_ERROR);
									break;
								}
								case UNLOCK_WRONG_PIN:
								{
									send_udp_client_code((struct sockaddr*)&client_addr_udp, unlock_sock,
																UNLOCK_WRONG_PIN);
									break;
								}
								case UNLOCK_UNBLOCKED_RESPONSE:
									send_udp_client_code((struct sockaddr *)&client_addr_udp, unlock_sock,
																		UNLOCK_UNBLOCKED_RESPONSE);
									break;
								default:
								{
									printf("UNLOCK default error code\n");
									send_client_code(i, UNLOCK_ERROR);
									break;
								}
							}
							
						} else {
							//send -4 message
							send_udp_client_code((struct sockaddr*)&client_addr_udp,
												unlock_sock, UNLOCK_INEXISTENT_CARD_NO);
							printf("Am trimis pe UDP -5\n");
						}
						break;
					}
				} else if (i == STDIN_FILENO) {
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
								long card_no;
								int pin;
								memset(&params, 0, sizeof(login_params_t));
								char *tok = strtok(NULL, " \t\n");
								printf("Tok = %s\n", tok);
								if (tok != NULL){
									card_no = atol(tok);
									params.card_no = card_no;
									tok = strtok(NULL, " \t\n");
									printf("Tok = %s\n", tok);
									if (tok != NULL){
										pin = atoi(tok);
										params.pin = pin;
									}
								}
								printf("Login params = %ld %d\n", params.card_no, params.pin);
								result = login(&params, &user_fd);
								switch (result) {
									case SUCCESS:
									{	
										send_client_code(i, SUCCESS); 
										set_fd_for_card_no(card_no, i);
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
										printf("%d -4 Card no not existent\n", CARD_NO_INEXISTENT);
										send_client_code(i, CARD_NO_INEXISTENT);
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
										send_client_code(i, LOGOUT_INVALID_USER);
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
							case LISTSOLD_CMD:
							{
								printf("List sold received\n");
								char message[BUFLEN];
								user_t *curr_user;
								int curr_user_pos = get_user_pos_by_fd(i);
								printf("current user pos = %d\n", curr_user_pos);
								/*
								 * There is no logged in user
								 */ 
								if (curr_user_pos == -1){
									send_client_code(i, NOT_LOGGED_IN);
									break;
								}
								curr_user = users[curr_user_pos];
								if (curr_user != NULL){
									memset(message, 0, BUFLEN);
									sprintf(message, "%.2lf", curr_user->balance);
									send_client_code(i, LISTSOLD_SUCCESSFUL);
									send(i, message, BUFLEN, 0);
								} else {
									send_client_code(i, NOT_LOGGED_IN);	
								}
								break;
							}
							case GET_MONEY_CMD:
							{
								/*
								 * Get the summ of money to extract
								 */
								double summ = 0;
								char payload[BUFLEN];
								memset(payload, 0, BUFLEN);
								char *tok = strtok(NULL, " \n\t");
								sscanf(tok, "%lf\n", &summ); 
								result = substract_from_balance(i, (long)summ);
								switch (result){
									case NOT_LOGGED_IN:
									{
										send_client_code(i, NOT_LOGGED_IN);
										break;
									}
									case GET_MONEY_NOT_MULTIPLE:
									{
										send_client_code(i, GET_MONEY_NOT_MULTIPLE);
										break;
									}
									case GET_MONEY_SUMM_TOO_LARGE:
									{
										send_client_code(i, GET_MONEY_SUMM_TOO_LARGE);
										break;
									}
									case GET_MONEY_SUCCESSFUL:
									{
										send_client_code(i, GET_MONEY_SUCCESSFUL);
										break;
									}
								}
								break;
							}
							case PUT_MONEY_CMD:
							{
								double summ=0;
								char payload[BUFLEN];
								memset(payload, 0, BUFLEN);
								char *tok = strtok(NULL, " \n\t");
								sscanf(tok, "%lf\n", &summ); 
								printf("summ from command = %lf\n", summ);
								result = put_summ_in_balance(i, (long)summ);
								switch (result) {
									case NOT_LOGGED_IN:
									{
										send_client_code(i, NOT_LOGGED_IN);
										break;
									}
									case PUT_MONEY_SUCCESSFUL:
									{
										send_client_code(i, PUT_MONEY_SUCCESSFUL);
										break;
									}
									default:
									{
										break;
									}
								}

								break;
							}
							case QUIT_CMD:
							{

								close (server_sock);
								exit(0);
								break;
							}
							default:
							{
								printf("%d Default command\n", DEFAULT_CMD);
								send_client_code(i, DEFAULT_CMD);
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
	close(unlock_sock);
	return 0;	
}
