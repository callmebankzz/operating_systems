#include <stdlib.h>
#include <comp421/filesystem.h>
#include <comp421/yalnix.h>

#include "message.h"
#include "yfs.h"


/*
 * Interaction between yfs and user library is implemented here.
 */

/* Function to get a path from a process. */
static char * getPathFromProcess(int pid, char *pathname, int len);

/**
 * This function processes a request received from a user-space process.
 * It determines the message type based on the requested operation, extracts the
 * necessary information from the message, and calls the corresponding YFS
 * function to handle the operation.
 *
 * Inputs: None.
 * 
 * Outputs: None.
 */
void processRequest(void) 
{

    // A variable to hold the return value of the YFS function that will handle the request.
    int return_value;

    // A struct to hold the message received from the user-space process.
    struct message_generic msg_rcv;
    
    // Receive the message as a generic type first.
    int pid = Receive(&msg_rcv);

    // Check if the message was received successfully.
    if (pid == ERROR) {
        // If the message could not be received, print an error message and
        // shut down the system.
        TracePrintf(1, "unable to receive message, shutting down\n");
        yfsShutdown();
    }

    // Determine the type of message based on the requested operation.
    if (msg_rcv.num == YFS_OPEN) {
        // If the operation is YFS_OPEN, extract the necessary information
        // from the message and call the YFS function yfsOpen() to handle
        // the operation.
        struct message_path * msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsOpen(pathname, msg->current_inode);
        free(pathname);
    } else if (msg_rcv.num == YFS_CREATE) {
        // If the operation is YFS_CREATE, extract the necessary information
        // from the message and call the YFS function yfsCreate() to handle
        // the operation.
        struct message_path * msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsCreate(pathname, msg->current_inode, CREATE_NEW);
        free(pathname);
    } else if (msg_rcv.num == YFS_READ) {
        // If the operation is YFS_READ, extract the necessary information
        // from the message and call the YFS function yfsRead() to handle
        // the operation.
        struct message_file * msg = (struct message_file *) &msg_rcv;
        return_value = yfsRead(msg->inodenum, msg->buf, msg->size, msg->offset, pid);
    } else if (msg_rcv.num == YFS_WRITE) {
        // If the operation is YFS_WRITE, extract the necessary information
        // from the message and call the YFS function yfsWrite() to handle
        // the operation.
        struct message_file * msg = (struct message_file *) &msg_rcv;
        return_value = yfsWrite(msg->inodenum, msg->buf, msg->size, msg->offset, pid);
    } else if (msg_rcv.num == YFS_SEEK) {
        // If the operation is YFS_SEEK, extract the necessary information
        // from the message and call the YFS function yfsSeek() to handle
        // the operation.
        struct message_seek * msg = (struct message_seek *) &msg_rcv;
        return_value = yfsSeek(msg->inodenum, msg->offset, msg->whence, msg->current_position);
    } else if (msg_rcv.num == YFS_LINK) {
        // If the message type is YFS_LINK, cast the received message to a 
        // message_link struct, get the old and new path names for the link, and
        // call yfsLink function to create the link and save the return value.
        struct message_link * msg = (struct message_link *) &msg_rcv;
        char *oldname = getPathFromProcess(pid, msg->old_name, msg->old_len);
        char *newname = getPathFromProcess(pid, msg->new_name, msg->new_len);
        return_value = yfsLink(oldname, newname, msg->current_inode);
        free(oldname);
        free(newname);
    } else if (msg_rcv.num == YFS_UNLINK) {
        // If the message type is YFS_UNLINK, cast the received message to a 
        // message_path struct, get the path name for the file to unlink, and
        // call yfsUnlink function to unlink the file and save the return value.
        struct message_path * msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsUnlink(pathname, msg->current_inode);
        free(pathname);
    } else if (msg_rcv.num == YFS_SYMLINK) {
        // If the message type is YFS_SYMLINK, cast the received message to a 
        // message_link struct, get the old and new path names for the symbolic link,
        // and call yfsSymLink function to create the link and save the return value.
        struct message_link * msg = (struct message_link *) &msg_rcv;
        char *oldname = getPathFromProcess(pid, msg->old_name, msg->old_len);
        char *newname = getPathFromProcess(pid, msg->new_name, msg->new_len);
        return_value = yfsSymLink(oldname, newname, msg->current_inode);
        free(oldname);
        free(newname);
    } else if (msg_rcv.num == YFS_READLINK) {
        // If the message type is YFS_READLINK, cast the received message to a 
        // message_readlink struct, get the path name for the symbolic link,
        // and call yfsReadLink function to read the link and save the return value.
        struct message_readlink * msg = (struct message_readlink *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->path_len);
        return_value = yfsReadLink(pathname, msg->buf, msg->len, msg->current_inode, pid);
        free(pathname);
    } else if (msg_rcv.num == YFS_MKDIR) {
        // If the message type is YFS_MKDIR, cast the received message to a 
        // message_path struct, get the path name for the new directory,
        // and call yfsMkDir function to create the directory and save the return value.
        struct message_path * msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsMkDir(pathname, msg->current_inode);
        free(pathname);
    } else if (msg_rcv.num == YFS_RMDIR) {
        // If the message type is YFS_RMDIR, cast the received message to a 
        // message_path struct, get the path name for the directory to remove,
        // and call yfsRmDir function to remove the directory and save the return value.
        struct message_path * msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsRmDir(pathname, msg->current_inode);
        free(pathname);
    } else if (msg_rcv.num == YFS_CHDIR) {
        // If the message type is YFS_CHDIR, cast the received message to a
        // message_path struct, extract the path name, and call yfsChDir function
        // to change the current working directory of the process.
        struct message_path *msg = (struct message_path *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsChDir(pathname, msg->current_inode);
        free(pathname);
    } else if (msg_rcv.num == YFS_STAT) {
        // If the message type is YFS_STAT, cast the received message to a
        // message_stat struct, extract the path name, and call yfsStat function
        // to get the status of the file or directory specified by the path name.
        struct message_stat *msg = (struct message_stat *) &msg_rcv;
        char *pathname = getPathFromProcess(pid, msg->pathname, msg->len);
        return_value = yfsStat(pathname, msg->current_inode, msg->statbuf, pid);
        free(pathname);
    } else if (msg_rcv.num == YFS_SYNC) {
        // If the message type is YFS_SYNC, call yfsSync function to flush
        // all modified data blocks and inodes to disk.
        return_value = yfsSync();
    } else if (msg_rcv.num == YFS_SHUTDOWN) {
        // If the message type is YFS_SHUTDOWN, call yfsShutdown function
        // to shut down the file system cleanly and exit.
        return_value = yfsShutdown();
    } else {
        // If the message type is unknown, print a message to the console and
        // return an error value.
        TracePrintf(1, "unknown operation %d\n", msg_rcv.num);
        return_value = ERROR;
    }

    // Send reply.
    struct message_generic msg_rply;
    msg_rply.num = return_value;
    if (Reply(&msg_rply, pid) != 0) {
        TracePrintf(1, "error sending reply to pid %d\n", pid);
    }
}

/**
 * This function retrieves the pathname of a specified process and stores it
 * locally in a newly allocated buffer.
 * 
 * Inputs:
 *  pid: an integer representing the process ID of the target process.
 *  pathname: a pointer to the pathname buffer to be copied from.
 *  len: an integer representing the length of the pathname buffer.
 * 
 * Outputs:
 *  Upon success, returns a pointer to the newly allocated buffer containing the 
 *  retrieved pathname. Otherwise, returns NULL if memory allocation fails or if 
 *  the copy operation fails.
 * 
 * Notes:
 *  - The caller is responsible for freeing the memory allocated by this function.
 *  - This function uses the CopyFrom system call to copy data from the target process.
 *  - If the buffer provided by the caller is not large enough to hold the entire pathname, 
 *    the copy operation will fail.
 * 
 */
static char *
getPathFromProcess(int pid, char *pathname, int len)
{
    // Allocate a new buffer to store the retrieved pathname.
    char *local_pathname = malloc(len * sizeof (char));
    if (local_pathname == NULL) {
        // If the allocation fails, print an error message and return NULL.
        TracePrintf(1, "error allocating memory for pathname\n");
        return NULL;
    }
    // Copy the contents of the pathname buffer from the target process to the newly allocated buffer.
    if (CopyFrom(pid, local_pathname, pathname, len) != 0) {
        // If the copy operation fails, print an error message and return NULL.
        TracePrintf(1, "error copying %d bytes from %p in pid %d to %p locally\n", 
                len, pathname, pid, local_pathname);
        return NULL;
    }
    // If the operation is successful, return a pointer to the new buffer.
    return local_pathname;
}