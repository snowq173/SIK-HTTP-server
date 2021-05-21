#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "filesearch.h"


#define 	HORIZONTAL_TAB 			9

static const char *http_header = "http://";

int32_t set_address(char **buffer, const char *path, size_t path_length, FILE *fp) {
	int32_t error_status = 0;
	
	char *b = NULL;
	size_t n;
	
	size_t resource_lo = 0;
	size_t resource_hi = 0;
	size_t server_lo = 0;
	size_t server_hi = 0;
	size_t port_lo = 0;
	size_t port_hi = 0;
	
	bool port = false;
	bool serv_address = false;
	
	bool first_tab = false;
	bool second_tab = false;
	
	/* Move file pointer to the beginning of the file */
	rewind(fp);

	errno = 0;
	while(getline(&b, &n, fp) != -1) {
		port = false;
		serv_address = false;
		first_tab = false;
		second_tab = false;
		
		for(size_t i = 0; i < strlen(b); ++i) {
			if(b[i] == '\n') {
				continue;
			}
			
			if(b[i] != ' ' && b[i] != HORIZONTAL_TAB) {
				if(second_tab) {
					if(!port) {
						port_lo = i;
					}
					
					port_hi = i;
					port = true;
				}
				else if(first_tab) {
					if(!serv_address) {
						server_lo = i;
					}
					
					server_hi = i;
					serv_address = true;
				}
				else {
					resource_hi = i;
				}
			}
			else {
				if(serv_address) {
					second_tab = true;
				}
				else {
					first_tab = true;
				}
			}
		}
		
		size_t address_size = 30 + resource_hi + server_hi + port_hi - resource_lo - server_lo - port_lo;
		
		
		bool ok_path = (path_length == resource_hi + 1);
		
		for(size_t i = 0; i <= resource_hi && ok_path; ++i) {
			if(b[i] != path[i]) {
				ok_path = false;
			}
		}
		
		if(ok_path) {	
			*buffer = malloc(address_size * sizeof(char));
			
			char *array = *buffer;
			
			if(*buffer == NULL) {
				error_status = 1;
				break;
			}
			
			memset(array, 0, address_size);
			
			size_t pos = 0;
			
			for(size_t i = 0; i < strlen(http_header); ++i) {
				array[i] = http_header[i];
				pos++;
			}
			
			size_t save_pos = pos;
			
			for(size_t i = pos; i <= pos + server_hi - server_lo; ++i) {
				array[i] = (b + server_lo)[i - pos];
				save_pos++;
			}
			
			pos = save_pos;
			
			array[pos] = ':';
			pos++;
			
			save_pos = pos;
			
			for(size_t i = pos; i <= pos + port_hi - port_lo; ++i) {
				array[i] = (b + port_lo)[i - pos];
				save_pos++;
			}
			
			pos = save_pos;

			for(size_t i = pos; i < pos + resource_hi - resource_lo + 1; ++i) {
				array[i] = (b + resource_lo)[i - pos];
			}
			
			break;
		}	
	}
	
	free(b);

	/* Memory error with either getline or malloc, return -1
	 * so as to send generic server error message to the client
	 * in client request handling function
	 */
	if(errno == ENOMEM || error_status) {
		return -1;
	}
	
	return 0;
}
