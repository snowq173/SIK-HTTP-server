#ifndef IOPROTOCOL_H
#define IOPROTOCOL_H



#include <stdbool.h>
#include <sys/types.h>
#include "request_data.h"



/* Useful macros with constants
 * representing appropriate HTTP
 * error codes
 */
#define ERROR_BAD_REQUEST 	 	400
#define ERROR_NOT_FOUND 		404
#define ERROR_INTERNAL 			500




#define GET_METHOD 				  0
#define HEAD_METHOD 			  1
#define UNKNOWN_METHOD_TYPE 	  2


/* Parses request line from client HTTP message 
 */
void parse_request_line(int32_t, ssize_t *, request_data_t *);



/* Parses lines corresponding to headers from client HTTP message 
 */
void parse_headers(int32_t, ssize_t *, bool *, request_data_t *);



/* Parses further data from client HTTP message; includes parsing the 'null line'
 * consisting of the CRLF - carriage-return and line-feed concatenation and message body
 */
void parse_further(int32_t, ssize_t *, request_data_t *);



/* Sends to client socket message indicating generic error of
 * the server (e.g. bad malloc)
 */
ssize_t send_generic_error_message(int32_t);



/* Sends to client socket message indicating that the sent
 * request was incorrect (bad structure etc.)
 */
ssize_t send_bad_request_message(int32_t);



/* Sends to client socket message indicating that the requested 
 * method has not been implemented by the server
 */
ssize_t send_unknown_method_message(int32_t);



/* Sends to client socket message indicating that the requested
 * file has not been found in server resources
 */
ssize_t send_not_found_message(int32_t, bool);



/* Sends to client socket part of message which is response
 * to the GET method requested by client (if required resource
 * was found in server resources directory)
 */
ssize_t send_get_message_part(int32_t, bool);



/* Sends to client string representation of number of bytes that were read
 * from requested file. String passed to the function shall be ended with double
 * CRLF as its interpreted as header "Content-Length" value ended with CRLF
 * and (as in HTTP message format) second CRLF indicating empty line which
 * precedes message body
 */
ssize_t send_content_length(int32_t, const char *);



/* Sends to client bytes stored in static buffer that were obtained
 * from the file requested by client.
 */
ssize_t send_obtained_file_content(int32_t, size_t);



/* Rearranges the buffer content so that the first available
 * byte is at the position 0
 */
void rearrange_buffer(ssize_t, ssize_t);



/* Handles request for file after verification path. If head method was specified,
 * sends (provided that everything goes good) content-type and requested file size.
 * If client requested GET method, file content is also sent.
 * Function return 0 on success and appropriate negative value if it fails. Possible
 * cases of failing are: ( return value <---> case)
 * -2 <---> although the path is correct, it points to a directory (in such case http
 * 404 target not found message is sent to the client)
 * -1 <---> file stream error / memory error / file stat error occurred (in these cases
 * http 500 generic server error message is issued to the client)
 */
int32_t handle_file(int32_t, bool, const char *, bool);



/* Checks in corelated servers file for the resource the path of which is 
 * stored in request_data_t object. Returns:
 * 0 on success (and nothing more has to be done as in such case the function
 * sends http response to the client socket)
 * -1 on error (malloc / getline in nested function) -> issue HTTP 500 generic
 * server error message from calling function
 * -2 when requested file has not been found and no errors occured -> issue
 * HTTP 404 not found message from calling function
 */
int32_t check_corelated(int32_t, request_data_t *, FILE *);



#endif /* IOPROTOCOL_H */
