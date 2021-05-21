#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "request_data.h"



#define ARRAY_SIZE 8220



struct request_data_t {
	size_t work_dir_path_len;
	size_t resource_path_array_size;
	size_t resource_path_length;
	int32_t method_type;
	int32_t connection_status;
	int32_t error_status;
	
	char *array_ptr;
};



request_data_t *new_request_data(const char *work_path) {
	request_data_t *req_data_ptr = malloc(sizeof(request_data_t));
	
	if(req_data_ptr == NULL)
		return NULL;
	
	size_t work_path_len = strlen(work_path);
	size_t allocation_size = work_path_len + ARRAY_SIZE;
	
	char *array_alloc_ptr = malloc(allocation_size * sizeof(char));
	
	if(array_alloc_ptr == NULL) {
		free(req_data_ptr);
		return NULL;
	}
	
	memset(array_alloc_ptr, 0, allocation_size);
	
	/* Copy working path string
	 */
	for(size_t i = 0; i < work_path_len; ++i) {
		array_alloc_ptr[i] = work_path[i];
	}
	

	
	req_data_ptr->array_ptr = array_alloc_ptr;
	req_data_ptr->work_dir_path_len = work_path_len;
	req_data_ptr->resource_path_length = 0;
	req_data_ptr->resource_path_array_size = allocation_size;
	req_data_ptr->method_type = -1;
	req_data_ptr->connection_status = 1;
	req_data_ptr->error_status = 0;
	
	return req_data_ptr;
}


	
void append_char(request_data_t *req_data, char c) {
	req_data->array_ptr[req_data->work_dir_path_len+req_data->resource_path_length] = c;
	(req_data->resource_path_length)++;
}



void clear_request_data(request_data_t *req_data) {
	size_t iteration_start = req_data->work_dir_path_len;
	size_t iteration_end = req_data->resource_path_array_size;
	
	for(size_t i = iteration_start; i < iteration_end; ++i) {
		(req_data->array_ptr)[i] = '\0';
	}
	
	req_data->resource_path_length = 0;
	req_data->error_status = 0;
	req_data->method_type = -1;
}



size_t get_path_length(request_data_t *req_data) {
	return req_data->resource_path_length;
}



size_t get_array_size(request_data_t *req_data) {
	return req_data->resource_path_array_size;
}



char *get_path_string_pointer(request_data_t *req_data) {
	return req_data->array_ptr;
}



char *get_original_path_string_pointer(request_data_t *req_data) {
	return (req_data->array_ptr + req_data->work_dir_path_len);
}



void delete_request_data(request_data_t *req_data) {
	free(req_data->array_ptr);
	free(req_data);
}



void set_method_type(request_data_t *req_data, int32_t value) {
	req_data->method_type = value;
}



int32_t get_method_type(request_data_t *req_data) {
	return req_data->method_type;
}



char get_path_char_at(request_data_t *req_data, size_t pos) {
	if(pos >= req_data->resource_path_length) {
		return 0;
	}
	
	return req_data->array_ptr[req_data->work_dir_path_len + pos];
}



static
bool check_path_character(char c) {
	if(isalnum(c) 								||
	   (c == DOT) 								||
	   (c == SLASH) 							||
	   (c == HYPHEN)) {
	   
		return true;
	}
	
	return false;
}
	


bool check_request_path_characters(request_data_t *req_data) {
	size_t iteration_start = req_data->work_dir_path_len;
	size_t iteration_limit = iteration_start + req_data->resource_path_length;

	for(size_t i = iteration_start; i < iteration_limit; ++i) {
		if(!check_path_character(req_data->array_ptr[i])) {
			return false;
		}
	}
	
	return true;	
}



void set_error_status(request_data_t *req_data, int32_t status) {
	req_data->error_status = status;
	req_data->connection_status = 0;
}



int32_t get_error_status(request_data_t *req_data) {
	return req_data->error_status;
}



void mark_connection_closed(request_data_t *req_data) {
	req_data->connection_status = 0;
}



bool is_connection_closed(request_data_t *req_data) {
	return (req_data->connection_status == 0);
}
	
