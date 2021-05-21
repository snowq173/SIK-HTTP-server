#ifndef REQUEST_DATA_H
#define REQUEST_DATA_H



#include <stdint.h>
#include <stdbool.h>



#define 	HYPHEN 				45
#define 	DOT 				46
#define 	SLASH 				47



typedef struct request_data_t request_data_t;



/* Creates new request_data_t object; initializes
 * the char array within it with DEFAULT_SIZE
 * returns pointer to created structure on success
 * and NULL on failure of either allocating the memory
 * for structure or on failure of allocating memory space
 * for characters array. In case of bad characters array
 * allocation the memory allocated for object is freed
 */
request_data_t *new_request_data();



/* Appends character to the resource path
 * stored in request data.
 */
void append_char(request_data_t *, char);



/* Gets length of target resource path that is
 * stored within request data
 */
size_t get_path_length(request_data_t *);



size_t get_array_size(request_data_t *);



/* Returns the pointer to first element of char array
 * which stores the path of request target resource
 */
char *get_path_string_pointer(request_data_t *);
char *get_original_path_string_pointer(request_data_t *);


/* Clears request data (no memory is freed on call)
 * by setting i = 0 where i denotes the index of first
 * non-used cell of char array stored in structure
 */
void clear_request_data(request_data_t *);



/* Deallocates the object; frees the char array
 * associated with request data and the request data
 * object itself. Better not use the passed pointer without
 * calling new_request_data() after calling this function
 */
void delete_request_data(request_data_t *);



/* Sets method type field in request data object
 */
void set_method_type(request_data_t *, int32_t);



/* Returns the integer value corresponding to 
 * method type field in the object
 */
int32_t get_method_type(request_data_t *);



/* Returns the char that is stored at passed index
 * in request data's target resource path. If the index
 * is too big ( >= path length), function returns ASCII NUL.
 * In other cases returns the character stored at specified
 * index.
 */
char get_path_char_at(request_data_t *, size_t);



/* Iterates through the path of request resource and
 * checks for each character if it is in set [A-Za-z0-9./].
 * Returns true if each character fulfills the condition
 * and false in other cases.
 */
bool check_request_path_characters(request_data_t *);



/* Sets error status associated with specified
 * client request to provided integer value
 */
void set_error_status(request_data_t *, int32_t);



/* Gets integer value indicating the error
 * status of specified request
 */
int32_t get_error_status(request_data_t *);



/* Marks connection status in request
 * as disconnected
 */
void mark_connection_closed(request_data_t *);



/* Checks whether connection has been closed during processing the
 * specified request
 */
bool is_connection_closed(request_data_t *);



#endif /* REQUEST_DATA_H */
