#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include "request_data.h"
#include "ioprotocol.h"
#include "filesearch.h"



/* Constant representing the size of server buffer
 */
#define BUFFER_SIZE 		 4096



/* Constant which contains string representation of server
 * http version
 */
static const char *HTTP_VERSION = "HTTP/1.1";



/* Constant which contains length of server http version
 * string representation
 */
static const ssize_t HTTP_VERSION_LENGTH = 8;



/* Array storing the string representations of all request headers that are relevant to
 * the server
 */
static const char *headers[] = { "Connection", "Content-Length" };



/* Array storing informations about whether header with corresponding index in headers array
 * has been provided in HTTP request message, filled with (false) values before parsing each
 * request
 */
static bool header_usage[2] = { false };



/* Array storing the string representations of methods implemented by the server
 * (known methods)
 */
static const char *methods[] = { "GET", "HEAD" };



/* Constant representing 'CRLF' string which is concatenation
 * of CR (carriage return) and LF (line feed).
 */
static const char *CRLF = "\r\n";



/* Error message sent to client on server error
 */
static const char *generic_error_message = "HTTP/1.1 500 InternalServerError\r\nConnection: close\r\nServer: SIK_server\r\n\r\n";



/* Error message sent to client on incorrect request
 */
static const char *bad_request_message = "HTTP/1.1 400 BadRequest\r\nConnection: close\r\nServer: SIK_server\r\n\r\n";



/* Message sent to client on encountering an unknown
 * method name (provided that the rest of http request is correct)
 */
static const char *unknown_method_message = "HTTP/1.1 501 UnknownMethod\r\nServer: SIK_server\r\n\r\n";



/* Message sent to client on request for a file that can not be found
 * both in server files directory and as a file located on corelated server
 */
static const char *resource_not_found_message = "HTTP/1.1 404 TargetNotFound\r\nServer: SIK_server\r\n\r\n";
static const char *resource_not_found_close_message = "HTTP/1.1 404 TargetNotFound\r\nServer: SIK_server\r\nConnection: close\r\n\r\n";



/* Part of message which is sent to client when requested file has not been found in server directory
 * but was found on one of corelated servers
 */
static const char *temp_moved_message_part = "HTTP/1.1 302 TemporarilyMoved\r\nServer: SIK_server\r\nLocation: ";



/* Part of message which is sent to client when requested file has been found and its content (whole
 * or part of it) is expected to be passed in message body (GET method from client)
 */
static const char *file_response_part = "HTTP/1.1 200 OK\r\nServer: SIK_server\r\nContent-Type: application/octet-stream\r\nContent-Length: ";
static const char *file_response_part_close = "HTTP/1.1 200 OK\r\nServer: SIK_server\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: ";



/* Buffer for reading input data from client socket
 */
static char server_buffer[BUFFER_SIZE];



/* Buffer for writing file content to client socket
 */
static char file_content_buffer[BUFFER_SIZE];



/* Maximum number of bytes that target resource path
 * or header value can contain
 */
static const size_t bytes_length_limit = 1 << 13;



void parse_request_line(int32_t client_socket, 
                        ssize_t *bytes_in_buffer,
                        request_data_t *request_data) {

	/* Forfeit parsing if disconnected
	 */
	if(is_connection_closed(request_data)) {
		return;
	}
	
	bool method_detected = false;
	bool check_method = false;
	bool check_first_space = false;
	bool check_request_target = false;
	bool check_second_space = false;
	bool check_http_version = false;
	
	bool finish_request_line = false;
	
	ssize_t buffer_iter = 0;
	ssize_t remaining = *bytes_in_buffer;
	
	ssize_t http_ver_pos = 0;
	ssize_t method_pos = 0;
	
	ssize_t crlf_off = 0;
	
	char method_name_buffer[10];
	memset(method_name_buffer, 0, 10);
	
	while(!finish_request_line) {
		if(remaining == 0) {
			remaining = read(client_socket, server_buffer, sizeof(server_buffer));
			
			if(remaining == 0) {
				mark_connection_closed(request_data);
				return;
			}

			buffer_iter = 0;
		}
		
		if(!check_method) {
			if(server_buffer[buffer_iter] == ' ') {
				if(!method_detected) {
					set_error_status(request_data, ERROR_BAD_REQUEST);
				}
				else {
					check_method = true;
				}
			}
			else {
				if(!isalpha(server_buffer[buffer_iter])) {
					set_error_status(request_data, ERROR_BAD_REQUEST);
					return;
				}
				else {
					method_detected = true;
					
					if(method_pos < 9) {
						method_name_buffer[method_pos] = server_buffer[buffer_iter];
						method_pos++;
					}
					
					remaining--;
					buffer_iter++;
				}
			}
		}
		else if(!check_first_space) {
			if(strcmp(method_name_buffer, methods[GET_METHOD]) == 0) {
				set_method_type(request_data, GET_METHOD);
			}
			else if(strcmp(method_name_buffer, methods[HEAD_METHOD]) == 0) {
				set_method_type(request_data, HEAD_METHOD);
			}
			else {
				set_method_type(request_data, UNKNOWN_METHOD_TYPE);
			}
			
			if(server_buffer[buffer_iter] != ' ') {

				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			
			buffer_iter++;
			remaining--;
			
			check_first_space = true;
		}
		else if(!check_request_target) {

			if(server_buffer[buffer_iter] != ' ') {
				append_char(request_data, server_buffer[buffer_iter]);
				
				/* Too long resource path (answer to one of questions to the task
				 * proposes sensible limit to be 2^13 bytes), request can be rejected
				 * as bad
				 */
				if(get_path_length(request_data) > bytes_length_limit) {

					set_error_status(request_data, ERROR_BAD_REQUEST);
					return;
				}
				
				buffer_iter++;
				remaining--;
			}
			else {
				/* Empty path of requested target or path does not begin with slash, 
				 * server can repsond with error code 400 (ERROR_BAD_REQUEST)
				 */
				if(!get_path_length(request_data) || get_path_char_at(request_data, 0) != '/') {

					set_error_status(request_data, ERROR_BAD_REQUEST);
					return;
				}

				check_request_target = true;
			}
		}
		else if(!check_second_space) {
			if(server_buffer[buffer_iter] != ' ') {

				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			
			remaining--;
			buffer_iter++;
			
			check_second_space = true;
		}
		else if(!check_http_version) {
			/* Incorrect HTTP version provided (or no http version provided but
			 * something that is far different from it, indeed a bad request
			 */
			if(server_buffer[buffer_iter] != HTTP_VERSION[http_ver_pos]) {

				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			
			/* OK HTTP version good */
			if(http_ver_pos == HTTP_VERSION_LENGTH - 1) {
				check_http_version = true;
			}

			http_ver_pos++;

			remaining--;
			buffer_iter++;
		}
		else {
			/* Not a CRLF at the end of request line -> bad request
			 */
			if(server_buffer[buffer_iter] != CRLF[crlf_off]) {

				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			
			if(crlf_off == 1) {
			 	/* OK request line good
			 	 */
				finish_request_line = true;
			}

			crlf_off++;

			remaining--;
			buffer_iter++;
		}
	}

	rearrange_buffer(buffer_iter, remaining);
	*bytes_in_buffer = remaining;
}



/* Compares two strings stores in str_1 and str_2 ignoring whether compared
 * letters are either lowercase or uppercase
 */
static
int32_t cmp_insensitive(const char *str_1,
					    const char *str_2) {

	if(strlen(str_1) != strlen(str_2)) {
		return -1;
	}
	else {
		for(size_t i = 0; i < strlen(str_1); ++i) {
			if(tolower(str_1[i]) != tolower(str_2[i])) {
				return -1;
			}
		}
		
		return 0;
	}
}



/* Updates the array which stores usage of headers in client request
 */
static
int32_t update_header_status(const char *header_string, 
							 ssize_t *header_index) {
							 
	for(ssize_t i = 0; i < 2; ++i) {
		if(cmp_insensitive(header_string, headers[i]) == 0) {
			if(header_usage[i]) {
				return -1;
			}
			
			*header_index = i;
			header_usage[i] = true;
		}
	}
	
	return 0;
}



static
bool correct_header_name_char(char c) {
	return (isalpha(c) || c == '-' || c == '_');
}



/* Parses headers and corresponding values from client request
 */
void parse_headers(int32_t client_socket, 
                   ssize_t *bytes_in_buffer,
                   bool *close_connection,
                   request_data_t *request_data) {

	if(is_connection_closed(request_data)) {
		return;
	}
	
	for(size_t i = 0; i < 2; ++i) {
		header_usage[i] = false;
	}
	
	bool finish_headers = false;
	
	char header_buffer[24];
	char value_buffer[24];
	
	memset(header_buffer, 0, sizeof(header_buffer));
	memset(value_buffer, 0, sizeof(value_buffer));
	
	ssize_t buffer_iter = 0;
	ssize_t header_field_iter = 0;
	ssize_t header_value_iter = 0;
	
	ssize_t remaining = *bytes_in_buffer;
	
	ssize_t crlf_off = 0;
	ssize_t current_header_index = -1;

	bool parsed_header = false; /* Flag indicating whether header (name and value) has been parsed */
	bool parsed_name = false; /* Flag indicating whether header name has been parsed */
	bool parsed_first_ows = false; /* Flag indicating whether first OWS has been parsed */
	bool parsed_value = false; /* Flag indicating whether header value has been parsed */
	bool parsed_second_ows = false; /* Flag indicating whether second OWS has been parsed */
	bool colon = false; /* Flag indicating whether colon occured in header */
	
	while(!finish_headers) {
		if(remaining == 0) {
			remaining = read(client_socket, server_buffer, sizeof(server_buffer));
			
			if(remaining == 0) {
				mark_connection_closed(request_data);
				return;
			}

			buffer_iter = 0;
		}
		
		if(parsed_header) {
			/* Restore the parsing flags to their default values
			 */
			parsed_header = false;
			parsed_name = false;
			parsed_first_ows = false;
			parsed_value = false;
			parsed_second_ows = false;
			
			colon = false;
			
			crlf_off = 0;
			
			header_field_iter = 0;
			header_value_iter = 0;
			/* Fill the appropriate buffer with NUL characters
			 */
			memset(header_buffer, 0, sizeof(header_buffer));
			memset(value_buffer, 0, sizeof(value_buffer));
		}
		
		if(!parsed_name) {
		
			if(server_buffer[buffer_iter] == '\r' || server_buffer[buffer_iter] == '\n') { 
				if(header_field_iter > 0 && !colon) {
					set_error_status(request_data, ERROR_BAD_REQUEST);
					return;
				}
				else {
					finish_headers = true;
					continue;
				}
			}
			else if(!correct_header_name_char(server_buffer[buffer_iter]) && 
					server_buffer[buffer_iter] != ':') {
				/* Name parsing has not been finished and non-letter character (different from colon)
				 * has been detected -> request is bad
				 */
				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			else if(server_buffer[buffer_iter] == ':') {
				if(header_field_iter == 0) {
					set_error_status(request_data, ERROR_BAD_REQUEST);
					return;
				}
				
				colon = true;
				remaining--;
				buffer_iter++;
			
				if(header_field_iter < 20) {

					int32_t error_check = update_header_status(header_buffer, &current_header_index);
					
					memset(header_buffer, 0, sizeof(header_buffer));
					header_field_iter = 0;
					
					if(error_check < 0 || header_usage[1]) {
						/* Either error occured (double use of some non-ignored header) or
						 * client specified content-length header which allows us to reject
						 * his request with http error code 400
						 */
						set_error_status(request_data, ERROR_BAD_REQUEST);
						return;
					}
				}

				parsed_name = true;
			}
			else {
				/* Store up to 20 characters (the only important for us header names length is at most 14):
				 * Content-Length
				 */
				if(header_field_iter < 20) {
					header_buffer[header_field_iter] = server_buffer[buffer_iter];
					header_field_iter++;
				}
				
				remaining--;
				buffer_iter++;
			}					
		}
		
		else if(!parsed_first_ows) {
			if(server_buffer[buffer_iter] == ' ') {
				remaining--;
				buffer_iter++;
			}
			else {
				parsed_first_ows = true;
			}		
		}
		
		else if(!parsed_value) {

		   	/* End of string corresponding to value of header, mark parsed_value flag as true
		   	 * and proceed with possible second optional whitespace section parsing
		   	 */
			if(server_buffer[buffer_iter] == ' ' || server_buffer[buffer_iter] == '\r') {
				/* Check if current header-line corresponds 'Connection' header and provided
				 * header value is equal to 'close'. If so, mark close_connection flag as true
				 * denoting that the client requested to end the connection with the server
				 */
				if(!strcmp(value_buffer, "close") && !current_header_index) {
					*close_connection = true;
				}
				
				current_header_index = -1;
				parsed_value = true;
				continue;
			}
			
			/* If the header value is short enough, append current character to header_value
			 */
			if(header_value_iter < 20) {
				value_buffer[header_value_iter] = server_buffer[buffer_iter];
				header_value_iter++;
			}
			
			remaining--;
			buffer_iter++;
		}
		
		else if(!parsed_second_ows) {

			if(server_buffer[buffer_iter] == ' ') {
				remaining--;
				buffer_iter++;
			}
			/* We are parsing the second optional whitespace section. The only
			 * character different from whitespace that we should expect here is
			 * carriage return, if it is not, the request is bad
			 */
			else if(server_buffer[buffer_iter] != '\r') {
				/* As the only character which we should expect after
				 * the second optional whitespaces is carriage return
				 * character
				 */
				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			else {
				parsed_second_ows = true;
			}
		}
		
		else {
			if(server_buffer[buffer_iter] != CRLF[crlf_off]) {
				/* Bad CRLF on end of header line
				 */
				set_error_status(request_data, ERROR_BAD_REQUEST);
				return;
			}
			
			if(crlf_off == 1) {
				parsed_header = true;
			}
			
			crlf_off++;
			
			remaining--;
			buffer_iter++;
		}
	}

	rearrange_buffer(buffer_iter, remaining);
	*bytes_in_buffer = remaining;
}



void parse_further(int32_t client_socket, 
				   ssize_t *bytes_in_buffer, 
				   request_data_t *request_data) {

	if(is_connection_closed(request_data)) {
		return;
	}
		
	bool finish_further_parsing = false;
	

	ssize_t buffer_iter = 0;
	ssize_t remaining = *bytes_in_buffer;
	ssize_t crlf_off = 0;
	ssize_t crlf_count = 0;
	
	while(!finish_further_parsing) {

		if(remaining == 0) {
			remaining = read(client_socket, server_buffer, sizeof(server_buffer));
			
			if(remaining == 0) {
				mark_connection_closed(request_data);
				return;
			}
			
			buffer_iter = 0;
		}
		
		if(server_buffer[buffer_iter] != CRLF[crlf_off]) {
			/* Bad CRLF on end of header line
			 */
			set_error_status(request_data, ERROR_BAD_REQUEST);
			return;
		}

		crlf_off++;
			
		remaining--;
		buffer_iter++;
			
		if(crlf_off > 1) {
			crlf_off = 0;
			crlf_count++;
		}
		
		/* Further parsing finished - concatenation of two CRLF combinations
		 * has been detected - the request is correct and further actions
		 * concerning it can be taken
		 */
		if(crlf_count == 1) {
			finish_further_parsing = true;
		}
	}
	
	rearrange_buffer(buffer_iter, remaining);
	*bytes_in_buffer = remaining;
}



ssize_t send_generic_error_message(int32_t client_socket) {
	return write(client_socket, generic_error_message, strlen(generic_error_message));
}



ssize_t send_bad_request_message(int32_t client_socket) {
	return write(client_socket, bad_request_message, strlen(bad_request_message));
}



ssize_t send_unknown_method_message(int32_t client_socket) {
	return write(client_socket, unknown_method_message, strlen(unknown_method_message));
}



ssize_t send_not_found_message(int32_t client_socket, bool include_close) {
	if(include_close) {
		return write(client_socket, resource_not_found_close_message, strlen(resource_not_found_close_message));
	}
	else { 
		return write(client_socket, resource_not_found_message, strlen(resource_not_found_message));
	}
}



ssize_t send_get_message_part(int32_t client_socket, bool include_close) {
	if(include_close) {
		return write(client_socket, file_response_part_close, strlen(file_response_part_close));
	}
	else {
		return write(client_socket, file_response_part, strlen(file_response_part));
	}
}



ssize_t send_content_length(int32_t client_socket, const char *content_length_buffer) {
	return write(client_socket, content_length_buffer, strlen(content_length_buffer));
}



ssize_t send_obtained_file_content(int32_t client_socket, size_t bytes) {
	return write(client_socket, file_content_buffer, bytes);
}



void rearrange_buffer(ssize_t buffer_pos, ssize_t remaining_bytes) {
	for(ssize_t i = 0; i < remaining_bytes; ++i) {
		server_buffer[i] = server_buffer[i + buffer_pos];
	}
}



/* Utility function for returning the number of digits in decimal
 * representation of passed integer
 */
static
size_t digits(size_t val) {
	if(val == 0) {
		return 1;
	}
	
	size_t ret = 0;
	while(val) {
		val /= 10;
		ret++;
	}
	
	return ret;
}



/* Utility function for appending double CRLF to the passed string
 * at specified offset (position from the beginning of the string)
 */
static
void append_double_crlf(char *target_string, size_t offset) {
	target_string[offset]     = '\r';
	target_string[offset + 1] = '\n';
	target_string[offset + 2] = '\r';
	target_string[offset + 3] = '\n';
}



int32_t handle_file(int32_t client_socket, bool close_conn, const char *path, bool head) {
	/* For determining the useful data about file (or directory) to which
	 * the passed path points
	 */
	struct stat statbuf;
	int32_t ret_val = stat(path, &statbuf);
	
	/* Error in stat - treating it as internal server error
	 */
	if(ret_val < 0) {
		return -1;
	}
	/* Correct path, but pointing to directory... do not open it (fopen would not return NULL ...) and
	 * forfeit sending by returning -2 value which will indicate to the server that it has to send
	 * 404 not found message to the cilent
	 */
	if(S_ISDIR(statbuf.st_mode)) {
		return -2;
	}
	
	size_t file_size = statbuf.st_size;
	
	FILE *fp = fopen(path, "r");
	if(fp == NULL) {
		return -1;
	}

	char bytes_number_buf[digits(file_size) + 10];
	memset(bytes_number_buf, 0, sizeof(bytes_number_buf));
	
	bool continue_sending = true;
	
	size_t read_bytes = 0;

	ssize_t write_val;
	
	sprintf(bytes_number_buf, "%lu", file_size);
	append_double_crlf(bytes_number_buf, strlen(bytes_number_buf));
	
	write_val = send_get_message_part(client_socket, close_conn);
	/* Check write() return value */
	if(write_val < 0) {
		fclose(fp);
		return -1;
	}
	
	write_val = send_content_length(client_socket, bytes_number_buf);
	/* Check write() return value */
	if(write_val < 0) {
		fclose(fp);
		return -1;
	}
	
	if(head) {
		fclose(fp);
		return 0;
	}
	

	while(continue_sending) {
		errno = 0;
		read_bytes = fread(file_content_buffer, 1, sizeof(file_content_buffer) - 1, fp);
		
		if(ferror(fp)) {
			fclose(fp);
			return -1;
		}
		/* Do we need to continue sending? */
		continue_sending = (feof(fp) ? false : true);
		
		write_val = send_obtained_file_content(client_socket, read_bytes);
		
		/* Negative return value from write indicates that client has closed
		 * the socket, return with -1 value indicating error of function execution
		 */
		if(write_val < 0) {
			fclose(fp);
			return -1;
		}
	}
	
	/* Close the file pointer after processing its content
	 */
	fclose(fp);
	return 0;
}



int32_t check_corelated(int32_t client_socket, request_data_t *req_data, FILE *file_corelated) {
	printf("Checking corelated servers file...\n");
	
	/* Buffer for string moved resource address (if it exists in corelated servers file)
	 */
	char *address_buffer = NULL;
	
	/* Get original path of resource, that is, the path which was requested by client
	 */
	char *path_pointer = get_original_path_string_pointer(req_data);
	size_t path_length = get_path_length(req_data);
	
	int32_t ret_val = set_address(&address_buffer, path_pointer, path_length, file_corelated);
	

	/* Negative return value (-1) indicates error (malloc / getline)
	 */
	if(ret_val < 0) {
		return ret_val;
	}
	
	/* Target resource path has not been found in corelated servers
	 * file
	 */
	if(address_buffer == NULL) {
		return -2;
	}
	
	append_double_crlf(address_buffer, strlen(address_buffer));
	
	/* Write first part of message 
	 	*/
	ret_val = write(client_socket, temp_moved_message_part, strlen(temp_moved_message_part));
	if(ret_val < 0) {
		free(address_buffer);
		return -1;
	}
		
	
	/* Write moved resource address to the client socket 
	 */
	ret_val = write(client_socket, address_buffer, strlen(address_buffer));
	if(ret_val < 0) {
		free(address_buffer);
		return -1;
	}
	
	/* Deallocate the memory which was allocated by set_address() function to store
	 * the string representation of the address that was sent to the client
	 */
	free(address_buffer);
	
	return 0;
}
