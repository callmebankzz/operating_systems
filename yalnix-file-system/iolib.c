#include <string.h>
#include <stdlib.h>

#include <comp421/iolib.h>
#include <comp421/filesystem.h>
#include <comp421/yalnix.h>

#include "message.h"


/**
 * The YFS file system library provides a high-level interface for client processes to 
 * interact with the file system. The library defines a set of procedures that 
 * correspond to the various file system operations, such as creating a file, reading 
 * or writing data from a file, or listing the contents of a directory. These library 
 * procedures use the IPC mechanisms provided by the Yalnix operating system to send 
 * requests to the YFS file server process and receive responses back.
 */

/* Global Variables */

// Struct that contains information about an open file, 
// including its inode number and the current position in the file.
struct open_file {
    int inodenum;
    int position;
};
// Struct that is an array of pointers to struct open_file with a maximum 
// size of MAX_OPEN_FILES, initialized to NULL.
struct open_file * file_table[MAX_OPEN_FILES] = {NULL};
// integer variable that represents the number of files that are 
// currently open in the system.
int files_open = 0;
// An integer variable stores the inode number of 
// the root directory in a file system.
int current_inode = ROOTINODE;
/**
 * Takes an integer file descriptor as input and 
 * returns a pointer to the corresponding open_file struct from 
 * a file table.
 */
struct open_file * getFile(int fd);


/* Helper Methods */

/**
 * This function returns the length of the given path name.
 * 
 * Inputs:
 *  pathname: a pointer to a string containing the path of the file or directory.
 * 
 * Outputs:
 *  Upon success, returns the length of the path name plus 1.
 *  Otherwise, returns ERROR.
 * 
 */
static int
getLenForPath(char *pathname)
{
    // Check if pathname is NULL
    if (pathname == NULL) {
        return ERROR;
    }
    
    int i;
    // Loop through each character of pathname until we reach the null terminator or the maximum pathname length
    for (i = 0; i < MAXPATHNAMELEN; i++) {
        if (pathname[i] == '\0') {
            break;
        }
    }
    
    // Check if the length of the pathname is 0 or greater than the maximum pathname length
    if (i == 0 || i == MAXPATHNAMELEN) {
        TracePrintf(1, "invalid pathname\n");
        return ERROR;
    }
    
    // Return the length of the pathname plus 1 to account for the null terminator
    return i + 1;
}

/**
 * This function adds a file to the file table and returns the file descriptor 
 * of the newly added file.
 * 
 * Inputs:
 *  inodenum: an integer representing the inode number of the file to be added.
 * 
 * Outputs:
 *  Upon success, returns the file descriptor of the newly added file.
 *  Otherwise, returns ERROR.
 * 
 */
static int
addFile(int inodenum)
{
    // Search for an available file descriptor
    int fd;
    for (fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (file_table[fd] == NULL) {
            break;
        }
    }

    // If file table is full, return error
    if (fd == MAX_OPEN_FILES) {
        TracePrintf(1, "file table full\n");
        return ERROR;
    }

    // Allocate memory for the new file in the file table
    file_table[fd] = malloc(sizeof (struct open_file));

    // If memory allocation fails, return error
    if (file_table[fd] == NULL) {
        TracePrintf(1, "error allocating space for open file\n");
        return ERROR;
    }

    // Initialize the new file with the given inode number and position
    file_table[fd]->inodenum = inodenum;
    file_table[fd]->position = 0;

    return fd;
}

/**
 * This function removes an open file from the file table and frees the 
 * memory occupied by it.
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor of the open file to remove.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR if the file descriptor is invalid.
 * 
 */
static int
removeFile(int fd)
{
    // Get the open_file struct associated with the file descriptor.
    struct open_file * file = getFile(fd);

    // If the file descriptor is invalid, return ERROR.
    if (file == NULL) {
        return ERROR;
    }

    // Free the memory occupied by the open_file struct.
    free(file);

    // Set the file_table entry to NULL
    file_table[fd] = NULL;

    // Return success (0).
    return 0;
}

/**
 * This function returns a pointer to the open_file struct at the 
 * specified index in the file_table array.
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor of the open file to remove.
 * 
 * Outputs:
 *  Upon success, returns a pointer to the open_file struct at the specified index. 
 *  Otherwise, returns NULL.
 * 
 */
struct open_file *
getFile(int fd)
{

    // Check if the given file descriptor is valid.
    if (fd < 0 || fd >= MAX_OPEN_FILES) 
    {
        // Return NULL if the file descriptor is invalid.
        return NULL;
    }
    // Return a pointer to the open_file struct at the specified index.
    return file_table[fd];
}

/**
 * This function sends a message to the file server with the specified operation and pathname.
 * 
 * Inputs:
 *  operation: an integer indicating the operation to be performed.
 *  pathname: a pointer to a string containing the path of the file or directory.
 * 
 * Outputs:
 *  Upon success, returns an integer indicating the result of the operation. 
 *  Otherwise, returns ERROR.
 * 
 * Note:
 *  The `current_inode` variable used in this function is assumed to be a global variable that contains 
 *  the current inode number of the file system.
 */
static int
sendPathMessage(int operation, char *pathname)
{

    // calls the `getLenForPath` function to determine the length of the pathname.
    int len = getLenForPath(pathname);
    // If `getLenForPath` returns an error, the function returns ERROR.
    if (len == ERROR) {
        return ERROR;
    }
    // allocate memory for a `message_path` struct.
    struct message_path * msg = malloc(sizeof(struct message_path));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for path message\n");
        return ERROR;
    }

    // assigns the operation number, current inode number, the pathname, and length to msg struct.
    msg->num = operation;
    msg->current_inode = current_inode;
    msg->pathname = pathname;
    msg->len = len;

    // sends message to file server. If `Send` returns an error, the function frees 
    // the memory and returns ERROR.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // msg gets overwritten with reply message after return from Send.
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function is used to send a file message with the specified operation 
 * to the file server.
 * 
 * Inputs:
 *  operation: an integer indicating the operation to be performed.
 *  inodenum: an integer indicating the inode number.
 *  buf: a pointer to the buffer containing the file contents.
 *  size: an integer indicating the size of the buffer.
 *  offset: an integer indicating the file offset.
 * 
 * Outputs:
 *  Upon success, returns an integer indicating the result of the operation. 
 *  Otherwise, returns ERROR.
 * 
 */
static int
sendFileMessage(int operation, int inodenum, void *buf, int size, int offset)
{

    // Check if buffer size is negative or if buffer is NULL.
    if (size < 0 || buf == NULL) {
        return ERROR;
    }
    // Allocate space for file message.
    struct message_file * msg = malloc(sizeof(struct message_file));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for file message\n");
        return ERROR;
    }
    // Set msg fields.
    msg->num = operation;
    msg->inodenum = inodenum;
    msg->buf = buf;
    msg->size = size;
    msg->offset = offset;

    // Send message to server and check for errors.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // Store result code and free message. Returns code.
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function is used to send a link message to the file server with the 
 * specified operation, oldname and newname.
 * 
 * Inputs:
 *  operation: an integer indicating the operation to be performed.
 *  oldname: a string containing the path of the file to be linked.
 *  newname: a string containing the path of the newly linked file.
 * 
 * Outputs:
 *  Upon success, returns an integer indicating the result of the operation. 
 *  Otherwise, returns ERROR.
 * 
 */
static int
sendLinkMessage(int operation, char *oldname, char *newname)
{

    // Get lengths of oldname and newname paths.
    int oldlen = getLenForPath(oldname);
    if (oldlen == ERROR) {
        return ERROR;
    }
    int newlen = getLenForPath(newname);
    if (newlen == ERROR) {
        return ERROR;
    }

    // Allocate space for link message.
    struct message_link * msg = malloc(sizeof(struct message_link));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for path message\n");
        return ERROR;
    }

    // Set msg fields.
    msg->num = operation;
    msg->current_inode = current_inode;
    msg->old_name = oldname;
    msg->new_name = newname;
    msg->old_len = oldlen;
    msg->new_len = newlen;

    // Send message to server and check for errors.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // msg gets overwritten with reply message after return from Send.
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function sends a message to the file server requesting to read the contents of the symbolic link at the 
 * specified path and store it in the provided buffer.
 * 
 * Inputs:
 *  pathname: a null-terminated string representing the path to the symbolic link to read.
 *  buf: a pointer to a character buffer that will store the contents of the symbolic link.
 *  len: an integer representing the maximum number of bytes that can be stored in the buffer.
 * 
 * Outputs:
 *  Upon success, returns YFS_READLINK indicating that the operation was successful. Otherwise, returns ERROR 
 *  indicating that an error occurred while sending the message or allocating memory for the message.
 * 
 */ 
static int
sendReadLinkMessage(char *pathname, char *buf, int len)
{

    // Ensure buffer is valid and length is non-negative.
    if (buf == NULL || len < 0) {
        return ERROR;
    }

    // Get the length of the pathname and ensure it is valid.
    int path_len = getLenForPath(pathname);
    if (path_len == ERROR) {
        return ERROR;
    }

    // Allocate space for the message to be sent to the file server.
    struct message_readlink * msg = malloc(sizeof(struct message_readlink));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for read link message\n");
        return ERROR;
    }

    // Set msg fields.
    msg->num = YFS_READLINK;
    msg->current_inode = current_inode;
    msg->pathname = pathname;
    msg->path_len = path_len;
    msg->buf = buf;
    msg->len = len;

    // Send the message to the file server and handle any errors that may occur.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // msg gets overwritten with reply message after return from Send.
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function sends a message to the file server requesting a change in the current file offset for the file 
 * associated with the specified inode number. 
 * 
 * Inputs:
 *  inodenum: an integer representing the inode number of the file to seek.
 *  current_position: an integer representing the current offset of the file.
 *  offset: an integer representing the number of bytes to seek forward or backward from the current position.
 *  whence: an integer indicating the reference point used to calculate the new offset. Possible values are:
 *          - SEEK_SET: the new position is offset bytes from the beginning of the file.
 *          - SEEK_CUR: the new position is offset bytes from the current position.
 *          - SEEK_END: the new position is offset bytes from the end of the file.
 * 
 * Outputs:
 *  Upon success, returns YFS_SEEK indicating that the operation was successful. Otherwise, returns ERROR 
 *  indicating that an error occurred while sending the message or allocating memory for the message.
 * 
 */ 
static int
sendSeekMessage(int inodenum, int current_position, int offset, int whence)
{

    // Check for invalid inputs.
    if (inodenum <= 0) {
        return ERROR;
    }

    // Allocate memory for the message.
    struct message_seek * msg = malloc(sizeof(struct message_seek));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for seek message\n");
        return ERROR;
    }

    // Set msg fields.
    msg->num = YFS_SEEK;
    msg->inodenum = inodenum;
    msg->current_position = current_position;
    msg->offset = offset;
    msg->whence = whence;

    // Send the message to the file server.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // The reply message overwrites the input message, 
    // so we can read it after the Send() call.
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function sends a message to the file server requesting file metadata for the file specified by pathname. 
 * 
 * Inputs:
 *  pathname: a pointer to a character string that represents the path name of the file to be queried.
 *  statbuf: a pointer to a struct Stat that will be filled with the metadata of the file.
 * 
 * Outputs:
 *  Upon success, returns YFS_STAT indicating that the operation was successful, and the metadata of the file can 
 *  be found in the statbuf parameter. Otherwise, returns ERROR indicating that an error occurred while sending the 
 *  message or allocating memory for the message.
 * 
 */ 
static int
sendStatMessage(char *pathname, struct Stat *statbuf)
{

    // check if statbuf is null.
    if (statbuf == NULL) {
        return ERROR;
    }

    // get the length of the pathname.
    int len = getLenForPath(pathname);
    if (len == ERROR) {
        return ERROR;
    }

    // allocate space for the message.
    struct message_stat * msg = malloc(sizeof(struct message_stat));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for path message\n");
        return ERROR;
    }

    // Set msg fields.
    msg->num = YFS_STAT;
    msg->current_inode = current_inode;
    msg->pathname = pathname;
    msg->len = len;
    msg->statbuf = statbuf;

    // send the message to the file server.
    if (Send(msg, -FILE_SERVER) != 0) {
        TracePrintf(1, "error sending message to server\n");
        free(msg);
        return ERROR;
    }
    // msg gets overwritten with reply message after return from Send
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}

/**
 * This function is used to send a generic message to the file server with the 
 * specified operation.
 * 
 * Inputs:
 *  operation: an integer indicating the operation to be performed.
 * 
 * Outputs:
 *  Upon success, returns an integer indicating the result of the operation. 
 *  Otherwise, returns ERROR.
 * 
 */
static int
sendGenericMessage(int operation) {

    // Allocate space for the message. 
    struct message_generic * msg = malloc(sizeof(struct message_generic));
    if (msg == NULL) {
        TracePrintf(1, "error allocating space for path message\n");
        return ERROR;
    }

    // Set the operation number.
    msg->num = operation;
    // Send the message to the file server.
    if (Send(msg, -FILE_SERVER) != 0) {
        // If there was an error sending the message, free the memory 
        // allocated for the message and return an error code.
        if (operation != YFS_SHUTDOWN) {
            TracePrintf(1, "error sending message to server\n");
        }
        // free memory allocated for msg and return ERROR.
        free(msg);
        return ERROR;
    }
    // msg gets overwritten with reply message after return from Send
    int code = msg->num;
    // free memory allocated for msg and return code.
    free(msg);
    return code;
}


/* Required Procedure Call Requests */

/**
 * This function opens a file specified by the pathname and returns a file 
 * descriptor number that can be used for future requests on this opened file. 
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the pathname of the file to be opened.
 * 
 * Outputs:
 *  Upon success, returns an integer representing the file descriptor number for the 
 *  opened file. Otherwise, returns ERROR.
 * 
 */
int
Open(char *pathname)
{

    // send a message to the server requesting to open the file.
    int inodenum = sendPathMessage(YFS_OPEN, pathname);
    if (inodenum == ERROR) {
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    // try to add a file to the array and return fd or ERROR.
    TracePrintf(2, "inode num %d\n", inodenum);
    return addFile(inodenum);
}

/**
 * This function closes the open file specified by the file descriptor number fd. If fd 
 * is not the descriptor number of a file currently open in this process, this request returns ERROR; 
 * otherwise, it returns 0.
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor number of the file to be closed.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR if fd is not the 
 *  descriptor number of a file currently open in this process.
 * 
 */
int
Close(int fd)
{
    // calls helper function used to remove files, removeFile
    return removeFile(fd);
}

/**
 * This function creates and opens the new file named pathname. 
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the pathname of the file to be created.
 * 
 * Outputs:
 *  Upon success, returns an integer representing the file descriptor number for the opened file. 
 *  Otherwise, returns ERROR.
 * 
 */ 
int
Create(char *pathname)
{

    // send a message to the server requesting to open the file..
    int inodenum = sendPathMessage(YFS_CREATE, pathname);
    if (inodenum == ERROR) {
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    // try to add a file to the array and return fd or error
    TracePrintf(2, "inode num %d\n", inodenum);
    return addFile(inodenum);
}

/**
 * This function reads data from an open file, beginning at the current position in 
 * the file as represented by the given file descriptor fd. 
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor number of the file to be read.
 *  buf: a pointer to the buffer in the requesting process into which to perform the read.
 *  size: an integer representing the number of bytes to be read from the file.
 * 
 * Outputs:
 *  Upon success, returns an integer representing the number of bytes read. If reading at the end-of-file, 
 *  returns 0. Otherwise, returns ERROR.
 * 
 */ 
int
Read(int fd, void *buf, int size)
{

    // Retrieve the open file corresponding to the given file descriptor.
    struct open_file * file = getFile(fd);
    if (file == NULL) {
        return ERROR;
    }
    // Send a message to the file server to read data from the file.
    int bytes = sendFileMessage(YFS_READ, file->inodenum, buf, size, file->position);
    if (bytes == ERROR) {
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    // Update the file position and return the number of bytes read.
    file->position += bytes;
    return bytes;
}

/**
 * This function writes data to an open file, beginning at the current position in the file 
 * as represented by the given file descriptor fd.
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor number of the file to be written.
 *  buf: a pointer to a buffer in the requesting process from which to perform the write.
 *  size: an integer representing the number of bytes to be written to the file.
 * 
 * Outputs:
 *  Upon success, returns an integer representing the number of bytes written.
 *  Otherwise, returns ERROR.
 * 
 */ 
int
Write(int fd, void *buf, int size)
{

    // get the open file struct associated with the given file descriptor.
    struct open_file * file = getFile(fd);
    if (file == NULL) {
        // return ERROR if file descriptor is invalid.
        return ERROR;
    }
    // send a YFS_WRITE message to the server to write data to the file.
    int bytes = sendFileMessage(YFS_WRITE, file->inodenum, buf, size, file->position);
    if (bytes == ERROR) {
        // print error message if server returns an error.
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    // update the position of the file pointer.
    file->position += bytes;
    // return the number of bytes written.
    return bytes;
}

/**
 * This function changes the current file position of the open file specified by file 
 * descriptor number fd. The argument offset specifies a byte offset in the file relative to the 
 * position indicated by whence. The value of offset may be positive, negative, or zero. The value 
 * of whence must be one of the following three codes defined in comp421/iolib.h:
 * - SEEK_SET: Set the current position of the file to be offset bytes after the beginning of the file.
 * - SEEK_CUR: Set the current position of the file to be offset bytes after the current position in 
 *   the open file indicated by file descriptor fd.
 * - SEEK_END: Set the current position of the file to be offset bytes after the current end of the file.
 * 
 * Inputs:
 *  fd: an integer representing the file descriptor number of the file to be written.
 *  offset: an integer specifying a byte offset in the file relative to the position indicated by 
 *          whence.
 *  whence: an integer indicating the reference position for offset. It must be one of the following 
 *          codes: SEEK_SET, SEEK_CUR, or SEEK_END.
 * 
 * Outputs:
 *  Upon success, returns an integer representing the new position (offset) in the open file. 
 *  Otherwise, returns ERROR.
 * 
 */ 
int
Seek(int fd, int offset, int whence)
{

    // Check if the value of whence is valid.
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return ERROR;
    }
    // Retrieve the open file specified by the file descriptor.
    struct open_file * file = getFile(fd);
    if (file == NULL) {
        return ERROR;
    }
    // Send a message to the server to perform the seek operation.
    int position = sendSeekMessage(file->inodenum, file->position, offset, whence);
    if (position == ERROR) {
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    // Update the position in the open file and return the new position.
    return (file->position = position);
}

/**
 * This function creates a hard link from the new file name newname to the existing 
 * file oldname. The files oldname and newname need not be in the same directory. The file oldname 
 * must not be a directory. 
 * 
 * Inputs:
 *  oldname: a pointer to a string representing the name of the existing file to be linked.
 *  newname: a pointer to a string representing the name of the new file to be created.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */ 
int
Link(char *oldname, char *newname)
{

    // Call the sendLinkMessage function with the YFS_LINK command and the given old and new names.
    int code = sendLinkMessage(YFS_LINK, oldname, newname);
    // If the return code indicates an error, print a message to the console.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function removes the directory entry for pathname, and if this is the last link 
 * to a file, the file itself should be deleted by freeing its inode. The file pathname must not be 
 * a directory. 
 *
 * Inputs:
 *  pathname: a pointer to a string representing the pathname of the file to be unlinked.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
Unlink(char *pathname)
{
    // Send an unlink message to the server with the given pathname.
    int code = sendPathMessage(YFS_UNLINK, pathname);\
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function creates a symbolic link from the new file name newname to the file name oldname. 
 * The files oldname and newname need not be in the same directory. It is an error if the file 
 * newname already exists. The file oldname need not currently exist in order to create a symbolic 
 * link to this name. 
 * 
 * Inputs:
 *  oldname: a pointer to a string representing the name of the target file for the symbolic link.
 *  newname: a pointer to a string representing the name of the new file to be created as the symbolic 
 *           link to oldname.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
SymLink(char *oldname, char *newname)
{

    // Send a symbolic link message to the server with the given oldname and newname.
    int code = sendLinkMessage(YFS_SYMLINK, oldname, newname);
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function reads the name of the file that the symbolic link pathname is linked to.
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the name of the symbolic link.
 *  buf: a pointer to a buffer where the name of the file that the symbolic link points to will be stored.
 *  len: an integer representing the maximum number of characters to be read from the name of the file.
 * 
 * Outputs:
 *  Upon success, returns the length (number of characters) of the name that the symbolic link pathname 
 *  points to (or the value len, whichever is smaller). Otherwise, returns ERROR.
 */
int
ReadLink(char *pathname, char *buf, int len)
{

    // Send a readlink message to the server with the given pathname, buffer, and length.
    int code = sendReadLinkMessage(pathname, buf, len);
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either the length of the name or ERROR for failure).
    return code;
}

/**
 * This function creates a new directory named pathname, including the "." and ".." entries
 * within the new directory. It is an error if the file pathname exists.
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the name of the directory to be created.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 */ 
int
MkDir(char *pathname)
{

    // Send a make directory message to the server with the given pathname.
    int code = sendPathMessage(YFS_MKDIR, pathname);
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function deletes the existing directory named pathname. The directory must contain no 
 * directory entries other than the “.” and “..” entries and possibly some free entries.
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the name of the directory to be removed.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 * Note: It is an error to attempt to remove the root directory; it is also an error to attempt to 
 * remove individually the “.” or “..” entry from a directory.
 * 
 */
int
RmDir(char *pathname)
{

    // Send an fstat message to the server with the given file descriptor.
    int code = sendPathMessage(YFS_RMDIR, pathname);
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function changes the current directory within the requesting process to be the directory indicated
 * by pathname. The current directory of a process should be remembered by the inode number of
 * that directory, within the file system library in that process. That current directory inode number
 * should then be passed to the file server on each request that takes any file name arguments. The file
 * pathname on this request must be a directory. 
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the name of the directory to change to.
 * 
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */ 
int
ChDir(char *pathname)
{
    int inodenum = sendPathMessage(YFS_CHDIR, pathname);
    if (inodenum == ERROR) {
        TracePrintf(1, "received error from server\n");
        return ERROR;
    }
    current_inode = inodenum;
    return 0;
}

/**
 * This function returns information about the file indicated by pathname in the information
 * structure at address statbuf.
 *
 * Inputs:
 *  pathname: a pointer to a string representing the name of the file to retrieve information about.
 *  statbuf: a pointer to a Stat structure where the information about the file will be stored.
 *
 * Outputs:
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
Stat(char *pathname, struct Stat *statbuf)
{

    // Send a stat message to the server with the given pathname and statbuf.
    int code = sendStatMessage(pathname, statbuf);
    // If the code is an error, print an error message.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (either 0 for success or ERROR for failure).
    return code;
}

/**
 * This function writes all dirty cached inodes back to their corresponding disk blocks
 * (in the cache) and then writes all dirty cached disk blocks to the disk. The request does
 * not complete until all dirty inodes and disk blocks have been written to the disk; this
 * function always then returns the value 0.
 * 
 * Inputs: None
 * 
 * Outputs:
 *  Upon success, returns 0.
 * 
 */ 
int
Sync()
{

    // Send a generic message with the YFS_SYNC opcode to the server.
    int code = sendGenericMessage(YFS_SYNC);
    // Print message if ERROR.
    if (code == ERROR) {
        TracePrintf(1, "received error from server\n");
    }
    // Return the code (which should be 0).
    return code;
}

/**
 * This function performs an orderly shutdown of the file server process. All dirty cached inodes and
 * disk blocks should be written back to the disk (as in a Sync request), and the file server should
 * then call Exit to complete its shutdown. As part of a Shutdown request, the server should
 * print an informative message indicating that it is shutting down. This request always returns the
 * value 0.
 * 
 * Inputs: None
 * 
 * Outputs:
 *  Upon success, returns 0.
 * 
 */ 
int
Shutdown()
{
    // Send a generic message with the YFS_SHUTDOWN type to initiate the shutdown process.
    sendGenericMessage(YFS_SHUTDOWN);
    // Return 0 upon success.
    return 0;
}