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
#define GET_USER_LIST_CMD 5
#define GET_FILE_LIST_CMD 6
#define UPLOAD_CMD 7
#define DOWNLOAD_CMD 8
#define SHARE_CMD 9
#define UNSHARE_CMD 12
#define DELETE_CMD 13

/*
 * Responses
 */
#define SUCCESS 100000
#define USER_OR_PASS_WRONG -3
#define ALREADY_LOGGED_IN -2
#define INEXISTENT_FILE -4
#define ALREADY_SHARED -6
#define SHARED_SUCCESSFUL 200
#define UNSHARED_SUCCESSFUL 201
#define ALREADY_PRIVATE -7
#define DELETE_SUCCESSFUL 202

#define NOT_LOGGED_IN -10
#define LOGOUT_INVALID_USER -1
#define UNKNOWN_USER -11
#define LOGOUT_SUCCESSFUL 1001
#define GETUSERLIST_SUCCESSFUL 1002
#define GETUSERLIST_EMPTY 1003
#define GETFILELIST_SUCCESSFUL 1004
#define GETFILELIST_FAIL 1005

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

using namespace std;

typedef struct login_params {
	char username[BUFLEN];
	char password[BUFLEN];
} login_params_t;

typedef struct file {
	char filename[BUFLEN];
	long size;
	bool shared;
} file_t;

typedef struct user{
	int fd;
	int files_no;
	char *username;
	file_t **files;
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

/*
 * These are for getting file information 
 * from folders because each user
 * has an array of files, that it can access
 * and by having the array of unique users
 * I can print all the files from a user folder
 */
user_t **users_unique;

int get_command_code(char *command)
{
	if (strcmp(command, "login") == 0)
		return LOGIN_CMD;
	else if (strcmp(command, "quit") == 0)
		return QUIT_CMD;
	else if (strcmp(command, "logout") == 0)
		return LOGOUT_CMD;
	else if (strcmp(command, "getuserlist") == 0)
		return GET_USER_LIST_CMD;
	else if (strcmp(command, "getfilelist") == 0)
		return GET_FILE_LIST_CMD;
	else if (strcmp(command, "upload") == 0)
		return UPLOAD_CMD;
	else if (strcmp(command, "share") == 0)
		return SHARE_CMD;
	else if (strcmp(command, "unshare") == 0)
		return UNSHARE_CMD;
	else if (strcmp(command, "delete") == 0)
		return DELETE_CMD;

	return DEFAULT_CMD;
}

int login(login_params_t *params)
{
	int N; //lines in user_file
	char line[BUFLEN];
	char user[BUFLEN];//read user from file
	char pass[BUFLEN];//read pass from file
	memset(user, 0, BUFLEN);
	memset(pass, 0, BUFLEN);

	fseek(user_file, 0, SEEK_SET);

	fscanf(user_file, "%d", &N);
	for (int i = 0; i < N; ++i) {
		fgets(line, BUFLEN, user_file);
		fscanf(user_file, "%s %s", user, pass);
		printf("user = %s pass = %s\n", user, pass);
		if (strcmp(user, params->username) == 0
		 && strcmp(pass, params->password) == 0) {
			fseek(user_file, 0, SEEK_SET);
		 	return SUCCESS;
		 }
	}
	login_attempt++;
	fseek(user_file, 0, SEEK_SET);
	if (login_attempt >= 3)
		return LOGIN_BRUTE_FORCE;
	return USER_OR_PASS_WRONG;
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
	 free(user->username);
	 user->username = NULL;
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
	int curr = 0;
	char dummy_pass[BUFLEN];
	memset(dummy_pass, 0, BUFLEN);
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
		out[curr] = (user_t *)malloc(1 * sizeof(user_t));
		out[curr]->username = (char *) malloc(BUFLEN * sizeof(char));
		fscanf(user_file, "%s %s", out[curr]->username, dummy_pass);
		printf("Username got is %s\n", out[curr]->username);
		curr++;
	 }
	 printf("Finished reading users from file \n");
}

int get_users_in_order(user_t **users_from_file, user_t ** users, user_t **output_users){

	int N = 0; //number of users
	int curr = 0; //current found users

	fseek(user_file, 0, SEEK_SET);
	fscanf(user_file, "%d", &N);
	for (int i = 0; i < N; ++i) {
		user_t *file_user = users_from_file[i];
		printf("Working with user %s\n", file_user->username);
		for (int j = 0; j < MAX_USERS; ++j) {

			/*
			 * There is no user authenticated
			 */
			if(users == NULL)
			{
				return curr;
			}
			/*
			 * No user has logged in on the j fd.
			 * Remember that users is an array that contains
			 * on the fd position, a structure user_t containing
			 * the username of the username connected on the fd socket id
			 */
			if (users[j] == NULL){
				continue;
			}

			/*
			 * Maybe there has been a deallocation of the username,
			 * but the user may still be referenced by the "array"
			 * thus users[j] != NULL and users[j]->username == NULL
			 */
			if(users[j]->username == NULL){
				continue;
			}

			if (strcmp(file_user->username, users[j]->username) == 0) {
						printf("on fd = %d user = %s\n",j, file_user->username);
					/*
					 * Alloc memory when we have users to add
					 * else the output remains empty meaning that
					 * no user exists in the list of users and this means
					 * no user has authenticated
					 */
					if (output_users == NULL)
						*output_users = (user_t *) malloc (MAX_USERS * sizeof(user_t));
					/*
					 * Add the new user to the ordered list of authenticated users
					 */
					if (output_users[curr] == NULL)
						output_users[curr] = (user_t *) malloc (1 * sizeof(user_t));
					if (output_users[curr]->username == NULL)
						output_users[curr]->username = (char *)malloc(BUFLEN * sizeof(char));
					memcpy(output_users[curr]->username, users[j]->username, BUFLEN);
					curr++;
			}
		}
	}
	return curr;
}

int get_users_from_file_count()
{
	int N;
	fseek(user_file, 0, SEEK_SET);
	fscanf(user_file, "%d", &N);
	return N;
}

void create_user_directories()
{
	int N = get_users_from_file_count();
	user_t **users = (user_t **)malloc(N * sizeof(user_t*));
	get_users_from_file(users);
	for (int i = 0; i < N; ++i) {
		mkdir(users[i]->username, 0777);
		if (errno == EEXIST){
			printf("Directory %s already exists\n", users[i]->username);
			continue;
		}
		if (errno == EACCES){
			printf("You do not have access to create %s\n", users[i]->username);
			continue;
		}
	}
}

int get_file_size(char *folder, char *file)
{

	char *tok;
	char prev_folder[BUFLEN];
	getcwd(prev_folder, BUFLEN);
	chdir(folder);

	struct stat data;
	memset(&data, 0, sizeof(data));

	char test[BUFLEN];
	memset(test, 0, BUFLEN);
	memcpy(test, file, BUFLEN);

	tok = strtok(test, "\n");
	int fd = open(tok, O_RDONLY);
	if (fd < 0)
		return FILE_NOT_FOUND;
	fstat(fd, &data);
	close(fd);
	chdir(prev_folder);
	return data.st_size;
}

/*
 * Add shared files for user
 * from the shared_files file
 */
void add_shared_files(user_t *user) {
	
	if (user->files == NULL)
		user->files = (file_t **)malloc(BUFLEN * sizeof(file_t*));
	
	int M = 0;
	int file_size = 0;
	fscanf(shared_file, "%d", &M);
	char line[BUFLEN];
	memset(line, 0, BUFLEN);
	char *tok;
	/*
	 * Jump over the first line in the file
	 * Parse the lines with username and file
	 */
	fgets(line, BUFLEN, shared_file);

	for (int i = 0; i < M; ++i) {
		fgets(line, BUFLEN, shared_file);	
		tok = strtok(line, ":");
		if (strcmp(user->username, tok) == 0) {
			tok = strtok(NULL, ":\n");
			/*
			 * Check if the file we want to add has been
			 * allocated
			 */
			int num_files = user->files_no;
			if (user->files[num_files] == NULL)// the last file in the list of files
				user->files[num_files] = (file_t *)malloc(1 * sizeof(file_t));

			user->files[user->files_no]->shared = true;
			memcpy(user->files[user->files_no]->filename, tok, strlen(tok));
			user->files_no++;
			/*
			 * Store the size of the files also
			 * The get_file_size function returns 
			 * FILE_NOT_FOUND if the file hasn't been found
			 * but I do not treat this exeption because
			 * the files are supposed to be there
			 */ 
		 	file_size = get_file_size(user->username, tok);
			user->files[num_files]->size = file_size;
			printf("Shared file for %s is %s with size %d\n", user->username, tok, file_size);
		}
	}
	fseek(shared_file, 0, SEEK_SET);
	printf("Exiting add shared files\n");
}

void add_private_file(user_t *user, char *filename)
{
	/*
	 * Check if files array is NULL
	 */
	if (user->files == NULL)
		user->files = (file_t **)malloc (BUFLEN * sizeof(file_t *));
	int last_file_index = user->files_no;	
	if (user->files[last_file_index] == NULL)
		user->files[last_file_index] = (file_t *)malloc (1* sizeof(file_t));
	memcpy(user->files[last_file_index], filename, strlen(filename));
	user->files[last_file_index]->shared = false;
	char curr_dir[] = "./";
	user->files[last_file_index]->size = get_file_size(curr_dir, filename);
	user->files_no++;
	printf("Added file %s , shared %d, size %ld\n", user->files[last_file_index]->filename,
												   user->files[last_file_index]->shared,
												   user->files[last_file_index]->size);
}

bool file_exists_in_list(user_t *user, char *filename)
{
	/*
	 * Iterate through each file 
	 * and check the name
	 * If strcmp == 0 the file has been
	 * added in the list and there is no point to add it again
	 */
	int n = user->files_no;
	if (n == 0)
		return false;
	if (user == NULL)
		return false;
	for (int i = 0; i < n ; ++i) {
		if (user->files[i] == NULL)
			continue;
		printf("Comparing %s with %s\n", user->files[i]->filename, filename);
		if (strcmp (user->files[i]->filename, filename) == 0)
			return true;
	}
	return false;
}


/*
 * Get into the user folder
 * get each file using readdir function
 * except ./ and ../
 * and add them using add_private_file(user, filename)
 * to the list of files for a given user
 */
void add_private_files(user_t *user)
{
	/*
	 * The directory of the user
	 */
	DIR *dir;
	struct dirent *ent;
	/*
	 * Save the current working directory
	 */
	char prev_cwd[BUFLEN];
	memset(prev_cwd, 0, BUFLEN);
	getcwd(prev_cwd, BUFLEN);
	/*
	 * Get size for a file 
	 */
	dir = opendir(user->username);
	if (dir == NULL){
		printf("ERROR opening folder for reading files from %s\n", user->username);
		return;
	}

	chdir(user->username);
	while ((ent = readdir(dir)) != NULL) {
	
		int fd = open(ent->d_name, O_RDONLY);
		if (fd < 0) {
			printf("Cannot find file %s\n", ent->d_name);
			continue;
		}
		if (strcmp(ent->d_name, ".") == 0)
			continue;
		if (strcmp(ent->d_name, "..") == 0)
			continue;
		if (file_exists_in_list(user, ent->d_name))
			continue;
	
		char folder[] = "./";
		int size = get_file_size(folder,ent->d_name); 
		/*
		 * Create the file and add it to the list
		 */
		if (user->files[user->files_no] == NULL)
			user->files[user->files_no] = (file_t*) malloc (1 * sizeof(file_t));
		memset(user->files[user->files_no], 0, sizeof(file_t));
		memcpy(user->files[user->files_no]->filename, ent->d_name, strlen(ent->d_name));
		user->files[user->files_no]->shared = false;
		user->files[user->files_no]->size = size;
		user->files_no++;

		printf("FILE %s in folder %s with size %d\n", ent->d_name, user->username, size);
	}
	chdir(prev_cwd);
}

user_t *get_user_by_name(char *name)
{
	int users_count = get_users_from_file_count();
	if (name == NULL)
		return NULL;
	for (int i = 0; i < users_count; ++i) {
		if (users_unique[i] == NULL)
			continue;
		if (strcmp(users_unique[i]->username, name) == 0){
			return users_unique[i];
		}
	}
	return NULL;
}

bool file_exists(char *username, char *filename)
{	
	DIR *dir = opendir(username);
	if (dir == NULL)
		return false;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strcmp (ent->d_name, filename) == 0) {
			return true;
		}
	}
	return false;	
	closedir(dir);
}

void drop_file_if_exists(char *username, char *filename)
{
	char prev_cwd[BUFLEN];
	getcwd(prev_cwd, BUFLEN);
	if (file_exists(username, filename)){
		chdir(username);
		unlink(filename);
		chdir(prev_cwd);
	}
}

int open_file_for_writing(char *username, char *filename)
{
	char prev_cwd[BUFLEN];
	memset(prev_cwd, 0, BUFLEN);
	getcwd(prev_cwd, BUFLEN);
	chdir(username);
	struct stat stats;
	memset(&stats, 0, sizeof(struct stat));
	stat(filename, &stats); 
	if (stats.st_size != 0) {
		getcwd(prev_cwd, BUFLEN);
		printf("CWD = %s\n", prev_cwd);
		printf("-9 File already exists\n");
		return -1;
	}
	int fd = open(filename, O_CREAT | O_WRONLY, 0666 );
	perror("Cannot create file\n");
	close(fd);
	chdir(prev_cwd);
	return fd;
}

file_t *get_file_by_name(user_t *user, char *filename)
{
	int files_no = user->files_no;	
	if (files_no == 0)
		return NULL;
	for (int i = 0; i < files_no; ++i) {
		if (strcmp(user->files[i]->filename, filename) == 0) {
			return user->files[i];
		}
	}
	return NULL;
}

void delete_file_from_disk(char *username, char *filename)
{
	char prev_cwd[BUFLEN];
	memset(prev_cwd, 0, BUFLEN);
	getcwd(prev_cwd, BUFLEN);
	chdir(username);
	unlink(filename);
	chdir(prev_cwd);
}

void delete_file_from_register(user_t *user, char *filename)
{
	int files_no = user->files_no;
	if(files_no == 0)
		return;
	for (int i = 0; i < files_no; ++i) {
		file_t *file = user->files[i];
		if(strcmp(file->filename, filename) == 0) {
			 printf("I = %d,files_no = %d\n", i, files_no);
			/*
			 * Move everything from the right 
			 * one position to the left
			 */
			for(int j = i; j < files_no - 1; ++j) {
				file_t *destination = user->files[j];
				file_t *source = user->files[j +1];
			 	memcpy(destination, source, sizeof(file_t));
			}
			memset(user->files[files_no-1], 0, sizeof(file_t));
			user->files_no--;
			return;
		}
	}
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
	create_user_directories();	

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
	listen(server_sock, MAX_USERS);


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
	if (users_unique == NULL)
		users_unique = (user_t **)malloc(MAX_USERS * sizeof(user_t *));
	get_users_from_file(users_unique);
	int user_count = get_users_from_file_count();
	for (int j = 0; j < user_count; ++j) {
		add_shared_files(users_unique[j]);
		add_private_files(users_unique[j]);
	}
	

	
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
						switch (code) {
							case LOGIN_CMD:
							{
								login_params_t params;
								memset(&params, 0, sizeof(login_params_t));
								char *tok = strtok(NULL, " \n");
								memcpy(params.username, tok, strlen(tok));
								tok = strtok(NULL, " \n");
								memcpy(params.password, tok, strlen(tok));
								result = login(&params);
								switch (result) {
									case SUCCESS:
									{	
										if (users != NULL && users[i] != NULL) {
											send_client_code(i, ALREADY_LOGGED_IN); 
											printf("Sent -2 already authenticated\n");
											send(i, users[i]->username, BUFLEN, 0);
											break;
										} else {
											send_client_code(i, SUCCESS);
											if (users == NULL)
												users = (user_t **)calloc(MAX_USERS, sizeof(user_t*));

											user_t *new_user = (user_t *)malloc(1 * sizeof(user_t));
											new_user->username = (char *)malloc(BUFLEN *sizeof(char));
											new_user->fd = i;
											new_user->files_no = 0;
											new_user->files = NULL;
											memcpy(new_user->username, params.username, BUFLEN);
											users[i] = new_user;
											add_shared_files(users[i]);
											add_private_files(users[i]);
											printf("New user->fd = %d\n", new_user->fd);
											printf("New user->username = %s\n", new_user->username);
											send(i, params.username, BUFLEN, 0);
											printf("sending %d message %s\n",i , params.username);
											printf("Login successful \n");
											break;
										}
										break;
									}

									case USER_OR_PASS_WRONG:
									{
										printf("-3 User/parola gresita \n");
										send_client_code(i, -3);
										break;
									}

									case LOGIN_BRUTE_FORCE:
									{
										printf("-8 Brute force detectat\n");
										send_client_code(i, -8);
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
							case GET_USER_LIST_CMD:
							{
								int N; //alloc space for output users
								int user_no = 0;

								N = get_users_from_file_count();
								/*
								 * Get the users from file
								 */
								user_t **users_from_file;
								user_t **output_users = NULL;
								if (users_from_file == NULL)
									printf("Users from file is null\n");

								output_users = (user_t **) malloc(N * sizeof(user_t *));
								get_users_from_file(users_from_file);
								/*
								 * Get logged in users
								 */
								user_no = get_users_in_order(users_from_file, users, output_users);
								printf("No of users = %d\n", user_no);
								if (output_users == NULL){
									printf("No user has been authenticated\n");
									break;
								} else {
									/*
									 * Check if there is no user logged in 
									 */
									if (user_no == 0) {
										send_client_code(i, GETUSERLIST_EMPTY);
										break;
									}
									/*
									 * Send code for successfull command
									 */
									send_client_code(i, GETUSERLIST_SUCCESSFUL);
									/*
									 * Send the number of users first
									 */
									char message[BUFLEN] = "";
									sprintf(message, "%d", user_no);
									send_client_message(i, message);
									for (int j = 0; j < user_no; ++j){
										printf("Got user %s\n", output_users[j]->username);
										/*
										 * Send the username of each logged in user
										 */
										send_client_message(i, output_users[j]->username);
									}
								}
								break;
							}
							case GET_FILE_LIST_CMD:
							{
								char *username = strtok(NULL, " \n");	
								printf("Username to get files from is %s\n", username);
								/*
								 * Get the user
								 */
								user_t *user;
								user = get_user_by_name(username);
								if (user == NULL) {
									send_client_code(i, UNKNOWN_USER);
									break;
								}
								/*
								 * Send the number of files
								 */
								int files_no = user->files_no;
								char message[BUFLEN];
								memset(message, 0, BUFLEN);
								/*
								 * No file has been added to a user
								 */
								if (files_no == 0){
									/*
									 * Send fail message
									 */
									send_client_code(i, GETFILELIST_FAIL);
									break;
								}

								/*
								 * Send fail message
								 */
								send_client_code(i, GETFILELIST_SUCCESSFUL);
								/*
								 * Send files count
								 */ 
								sprintf(message, "%d", files_no);
								send_client_message(i, message);

								printf("Sending number of files\n");
								/*
								 * Send each file name + size + SHARED/PRIVATE
								 */
								printf("Requested %d files\n", files_no);
								for(int j = 0; j < files_no; ++j) {
									char shared_text[BUFLEN];
									memset(shared_text, 0, BUFLEN);
									int shared = user->files[j]->shared;	
									if (shared)
										sprintf(shared_text, "SHARED");
									else
										sprintf(shared_text, "PRIVATE");
									memset(message, 0, BUFLEN);
									sprintf(message, "%s\t %ld bytes\t", user->files[j]->filename,
																  user->files[j]->size);
									strcat(message, shared_text);
									printf("Sending client message: %s\n", message);
									send_client_message(i, message);
								}
								break;
							}
							case UPLOAD_CMD:
							{
								char message[CHUNK_SIZE];
								memset(message, 0, CHUNK_SIZE);
								if (users == NULL) {
									printf("Cannot download, not logged in\n");
									send_client_code(i, NOT_LOGGED_IN); 
									break;
								}
								user_t *user = users[i];
								if (user == NULL) {
									printf("Cannot download, not logged in\n");
									send_client_code(i, NOT_LOGGED_IN); 
									break;
								}

								char filename[BUFLEN];
								memset(filename, 0, BUFLEN);
								char *tok;
								tok = strtok(NULL, " \n");
								memcpy(filename, tok, strlen(tok));
								printf("Filename = %s\n", filename);
								//drop_file_if_exists(user->username,message);
								int fd = open_file_for_writing(user->username,message);
								if (fd != -1) {
									/*
									 * Start writing
									 */
									printf("File is not null\n");
									close(fd);
								}else {
									printf("Cannot create file\n");
								}
								memset(buffer, 0, BUFLEN);
								break;
							}
							case SHARE_CMD:
							{
								/*
								 * Get filename 
								 */
								char filename[BUFLEN];
								memset(filename, 0, BUFLEN);
								char *tok = strtok(NULL, " \n");
								memcpy(filename, tok, strlen(tok));
								
								/*
								 * Get current user
								 */
								user_t * user = users[i];
								if (user == NULL) {
									send_client_code(i, UNKNOWN_USER);
									break;
								}

								/*
								 * Find unique user which contains 
								 * pointers to all the files
								 * from it's home directory
								 */
								user = get_user_by_name(user->username);
								if (! file_exists(user->username, filename)) {
									printf("-4 File not found\n");
									send_client_code(i, INEXISTENT_FILE);
									break;
								}else {
									file_t *file = get_file_by_name(user, filename);
										
									if (file->shared == true) {
										printf("-6 Fisierul este deja partajat\n");
										send_client_code(i, ALREADY_SHARED); 
										break;
									}
									file->shared = true;
									char message[BUFLEN];
									sprintf(message, "200 Fisierul %s a fost partajat.", filename);
									send_client_code(i, SHARED_SUCCESSFUL);
									send_client_message(i, message);
									break;
								}
									
								break;
							}
							case UNSHARE_CMD:
							{
								/*
								 * Get filename 
								 */
								char filename[BUFLEN];
								memset(filename, 0, BUFLEN);
								char *tok = strtok(NULL, " \n");
								memcpy(filename, tok, strlen(tok));
								
								/*
								 * Get current user
								 */
								user_t * user = users[i];
								if (user == NULL) {
									send_client_code(i, UNKNOWN_USER);
									break;
								}

								/*
								 * Find unique user which contains 
								 * pointers to all the files
								 * from it's home directory
								 */
								user = get_user_by_name(user->username);
								if (! file_exists(user->username, filename)) {
									printf("-4 File not found\n");
									send_client_code(i, INEXISTENT_FILE);
									break;
								}else {
									file_t *file = get_file_by_name(user, filename);
										
									if (file->shared == false) {
										printf("-7 Fisier deja privat\n");
										send_client_code(i, ALREADY_PRIVATE); 
										break;
									}
									file->shared = false;
									char message[BUFLEN];
									sprintf(message, "200 Fisierul %s a fost setat ca PRIVATE", filename);
									send_client_code(i, UNSHARED_SUCCESSFUL);
									send_client_message(i, message);
									break;
								}
									
								break;
							}
							case DELETE_CMD:
							{
								char *filename = strtok(NULL, " \n");
								char message[] = "200 Fisier sters";
								/*
								 * Get current user by translating
								 * the fd to username
								 */
								if (users == NULL) {
									printf("User is null\n");
									send_client_code(i, UNKNOWN_USER);
									break;
								}

								user_t *user = users[i];
								printf("Getting user%s\n", user->username);
								if (user == NULL) {
									printf("User is null\n");
									send_client_code(i, UNKNOWN_USER);
									break;
								}
								/*
								 * Get the user struct user_t from the unique_users which
								 * holds references to all the files
								 * from the user home dir
								 */
								user = get_user_by_name(user->username);
								if (user == NULL) {
									send_client_code(i, UNKNOWN_USER);
									break;
								}
								if (! file_exists(user->username, filename)) {
									send_client_code(i, INEXISTENT_FILE);	
									break;
								}
								delete_file_from_disk(user->username, filename);
								delete_file_from_register(user, filename);
								printf("Ajung aici \n");
								send_client_code(i, DELETE_SUCCESSFUL); 
								send_client_message(i, message);
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
