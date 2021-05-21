#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include "request_data.h"
#include "ioprotocol.h"



/* Maximum number of connections that can be queued
 * on the server TCP socket
 */
#define SERVER_QUEUE_LENGTH 10



static char *catalogue_path;
static char *corelated_servers_file;

static FILE *servers_file_pointer;



/* Default server port
 */
static int32_t SERVER_PORT = 8080;



static
void setup_server_address(struct sockaddr_in *server_addr) {
	server_addr->sin_family = AF_INET;
	server_addr->sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr->sin_port = htons(SERVER_PORT);
}



static
void check_socket_value(int32_t socket_desc) {
	if(socket_desc < 0) {
		perror("Creating socket");
		exit(EXIT_FAILURE);
	}
}



/* Checks whether string passed as str_1 argument is a
 * prefix of string passed as str_2 argument
 */
static
bool is_prefix_of(const char *str_1, const char *str_2) {
	if(strlen(str_1) > strlen(str_2)) {
		return false;
	}
	for(size_t i = 0; i < strlen(str_1); ++i) {
		if(str_1[i] != str_2[i]) {
			return false;
		}
	}
	
	return true;
}



static
void process_request(int32_t client_socket, 
					 ssize_t *bytes_in_buffer,
					 request_data_t *req_data) {

	ssize_t available_bytes = *bytes_in_buffer;
	bool close_request = false;


	parse_request_line(client_socket, &available_bytes, req_data);
	parse_headers(client_socket, &available_bytes, &close_request, req_data);	
	parse_further(client_socket, &available_bytes, req_data);
	
	
	if(get_error_status(req_data) == ERROR_BAD_REQUEST) {
		send_bad_request_message(client_socket);
		return;
	}
	else if(get_error_status(req_data) == ERROR_INTERNAL) {
		send_generic_error_message(client_socket);
		return;
	}
	
	
	/* Either set to 0 by encountering an error (which was treated with appropriate
	 * error message) or by detecting that the client disconnected from the socket
	 */
	if(is_connection_closed(req_data)) {
		return;
	}
	
	if(get_method_type(req_data) == UNKNOWN_METHOD_TYPE) {	
		ssize_t write_val = send_unknown_method_message(client_socket);
		if(write_val < 0) {
			mark_connection_closed(req_data);
			return;
		}
	}
	else if(check_request_path_characters(req_data)) {
		/* Clear errno before executing library realpath() function so as to
		 * grab data about possible ENOMEM
		 */
		errno = 0;
		char *req_resource_realpath = realpath(get_path_string_pointer(req_data), NULL);
		
		/* Memory error occured while resolving full path of the resource requested
		 * by server client
		 */
		if(req_resource_realpath == NULL && errno == ENOMEM) {
			free(req_resource_realpath);
			set_error_status(req_data, ERROR_INTERNAL);
			send_generic_error_message(client_socket);
			return;
		}
		else if(req_resource_realpath == NULL) {

			int32_t ret_val = check_corelated(client_socket, req_data, servers_file_pointer);
			
			/* Available cases of ret_val:
			 * ret_val ==  0 ----> message with address of moved resource has been sent to client
			 * in check_corelated() function (we can neglect this case as we have nothing to do with it)
			 * ret_val == -1 ----> memory error occured while searching the corelated servers file,
			 * so generic server error message should be issued
			 * ret_val == -2 ----> searching the corelated servers file was successful, but the
			 * requested resource path has not been found, 404 not found message should be issued
			 */
			if(ret_val == -2) {
				ssize_t write_val = send_not_found_message(client_socket, close_request);
				if(write_val < 0) {
					mark_connection_closed(req_data);
					return;
				}
			}
			else if(ret_val == -1) {
				free(req_resource_realpath);
				set_error_status(req_data, ERROR_INTERNAL);
				send_generic_error_message(client_socket);
				return;
			}
		}
		else {
			/* Path has been successfully resolved, the only thing that we have to check now is
			 * whether its string representation is a prefix of the server resources directory path\
			 */
			
			bool path_prefix_check = is_prefix_of(catalogue_path, req_resource_realpath);
			
			if(!path_prefix_check) {
				ssize_t write_val = send_not_found_message(client_socket, close_request);
				if(write_val < 0) {
					mark_connection_closed(req_data);
					return;
				}
			}
			else {

				int32_t ret_val = handle_file(client_socket, close_request, req_resource_realpath, (get_method_type(req_data) == HEAD_METHOD));
					
				/* Negative ret_val (exactly: -1) indicates that a file stream error occured while
				 * reading from requested file, issue a generic server error message on such event
				 */
				if(ret_val == -1) {
					free(req_resource_realpath);
					set_error_status(req_data, ERROR_INTERNAL);
					send_generic_error_message(client_socket);
					return;
				}
				else if(ret_val == -2) {
					ssize_t write_val = send_not_found_message(client_socket, close_request);
					if(write_val < 0) {
						mark_connection_closed(req_data);
						return;
					}
				}
			}
		}
		
		/* Deallocate the memory allocated by realpath() function
		 */
		free(req_resource_realpath);
	}
	else {
		send_not_found_message(client_socket, close_request);
	}
	
	
	
	/* Client requested to close the connection (no errors occured)
	 */
	if(close_request) {
		printf("Closing connection on request\n");
		mark_connection_closed(req_data);
	}
	
	
	
	/* Update the number of bytes that are available in server buffer
	 */
	*bytes_in_buffer = available_bytes;
}



/* Function for serving connection with client via TCP socket
 */
static
void serve_client(int32_t client_socket) {	
	/* Number of bytes that remaing to be parsed in server buffer
	 */
	ssize_t remaining_bytes = 0;
	
	/* Create request data object for client
	 */
	request_data_t *request_data = new_request_data(catalogue_path);
	
	if(request_data == NULL) {
		send_generic_error_message(client_socket);
	}
	else {
		while(!is_connection_closed(request_data)) {
			
			process_request(client_socket, &remaining_bytes, request_data);
			clear_request_data(request_data);
		}
	}
	
	/* Deallocate the client request data buffer
	 */
	delete_request_data(request_data);
	
	
	/* Close the client socket
	 */
	if(close(client_socket) == -1) {
		perror("close");
	}
}




/* Main function of the server, responsible for creating server socket and accepting messages
 * from clients that are connecting to the server
 */
int main(int argc, char *argv[]) {
	/* Check if either too few or too many arguments have been provided
	 * to server exeuction command
	 */
	if(argc < 3 || argc > 4) {
		fprintf(stderr, "Usage: %s <catalogue> <corelated-servers-file> <optional port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	
	/* Save character strings denoting respectively the catalogue path
	 * and name of corelated servers file
	 */
	corelated_servers_file = argv[2];
	
	catalogue_path = realpath(argv[1], NULL);
	
	if(catalogue_path == NULL) {
		perror("Error in resolving catalogue path");
		exit(EXIT_FAILURE);
	}
	
	
	servers_file_pointer = fopen(corelated_servers_file, "r");
	
	
	if(servers_file_pointer == NULL) {
		perror("Opening corelated servers file");
		exit(EXIT_FAILURE);
	}
	
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	act.sa_flags = SA_RESTART;
	
	if(sigaction(SIGPIPE, &act, NULL) != 0) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
		
	/* Override default server port value with the one
	 * provided as program argument
	 */
	if(argc == 4) {
		SERVER_PORT = atoi(argv[3]);
	}
	
	
	/* Address structures for server and client
	 */
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	
	socklen_t client_addr_length;


	/* Initialise server address
	 */
	setup_server_address(&server_address);
	
	
	int32_t server_socket;
	int32_t message_socket;
	
	
	/* Initialise server socket
	 */
	server_socket = socket(PF_INET, SOCK_STREAM, 0);
	check_socket_value(server_socket);
	

	/* Bind obtained socket descriptor to server address
	 */
	if(bind(server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}


	/* Begin listening on the socket for incoming connections
	 * of clients
	 */
	if(listen(server_socket, SERVER_QUEUE_LENGTH) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}


	while(1) {	
		printf("Waiting for client...\n");
		client_addr_length = sizeof(client_address);

		/* Get the descriptor of socket used for communication with incoming client
		 */
		message_socket = accept(server_socket, (struct sockaddr *) &client_address, &client_addr_length);
		
		/* Check the value of socket for possible errors that may have occured
		 */
		check_socket_value(message_socket);
		
		/* Serve connection with accepted server client
		 */
		serve_client(message_socket);
	}
	
	
	/* Deallocate the memory which was allocated by 
	 * execution of realpath() function
	 */
	free(catalogue_path);
	
	
	/* Close the corelated servers file
	 */
	fclose(servers_file_pointer);
	
	
	/* Close the server socket
	 */
	if(close(server_socket) == -1) {
		perror("Closing server socket");
		exit(EXIT_FAILURE);
	}
	

	exit(EXIT_SUCCESS);
}
