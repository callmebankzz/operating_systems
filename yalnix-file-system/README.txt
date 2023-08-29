Tyra Cole (tjc6)
COMP 421 
Lab 3

DESIGN 
Important files to look at when it comes to my lab3 submission are yfs.c (the YFS file server 
process) and iolib.c (the YFS file system library). However, in addition to these files, I 
have included message.c and hash_table.c. While all of these files are well-documented and 
well-commented, I've provided a summary of each file below:

ysf.c -----------------------------------------------------------------------------------------------------
This file includes the implementation of all of the required procedure call requests for the yalnix file 
system. This is the file server process, so it is responsible for managing the file system, including 
handling file system requests from client processes, maintaining file system structures such as directories 
and inodes, and interacting with the underlying storage media. The server process communicates with client 
processes using interprocess communication (IPC) mechanisms provided by the Yalnix operating system.

In addition to the required procedure calls and init() (which just initializes all the file server data 
structures), I included the following helper methods:
    1. removeItemFromFrontOfQueue: removes the first item from the specified queue and returns a pointer to it.
    2. printQueue: prints the contents of the specified queue to the console.
    3. removeItemFromQueue: removes the specified item from the specified queue.
    4. addItemEndQueue: adds the given cache item to the end of the specified queue.
    5. isEqual: checks whether the given path and directory entry name are equal.
    6. saveBlock: marks the cache item associated with the given block number as dirty.
    7. getBlock: retrieve a block of data either from the cache or from disk if the block is not already in 
       the cache. 
    8. saveInode: checks marks the inode as dirty in the cache.
    9. getInode: retrieves an inode from the inode cache or disk, depending on whether or not the inode is 
       currently cached.
    10. destroyCacheItem: deallocates the memory allocated for a cache item. 
    11. getInodeBlockNum: calculates the block number that contains the specified inode by dividing the inode 
        number by the number of inodes per block (INODESPERBLOCK) and adding 1.
    12. getNthBlock: returns the block number of the nth block of the file, allocating the block if necessary 
        and if the allocateIfNeeded parameter is true.
    13. getPathInodeNumber: returns the inode number for the file or directory represented by the path.
    14. freeUpInode: marks the given inode number as free.
    15. getNextFreeInodeNum: gets the number of the next free inode in the file system, and updates the inode 
        structure for that inode to indicate it has been reused.
    16. addFreeInodeToList: adds a new freeInode to the beginning of the linked list of free inodes.
    17. getNextFreeBlockNum: returns the number of the next free block and updates the free list accordingly.
    18. addFreeBlockToList: adds a new free block to the head of the free block list.
    19. buildFreeInodeAndBlockLists: builds the lists of free inodes and free blocks by examining the file 
        system blocks and inodes. 
    20. clearFile: clears the contents of a file.
    21. getDirectoryEntry: retrieves the directory entry of a file or creates it if it does not exist.
    22. getContainingDirectory: gets the inode number of the directory containing a given file, based on its 
        pathname and the inode number of the current working directory.

This file also includes the following data structures (found in yfs.h): 
    1. cachItem: struct for items in the cache.
    2. freeInode: struct for free inodes. 
    3. freeBlock: struct for free blocks of memory.
    4. queue: struct for a queue of cache items.
-----------------------------------------------------------------------------------------------------------

iolib.c ---------------------------------------------------------------------------------------------------
This file includes the implementation of all of the required procedure call requests for the yalnix file 
system. This is only the library, so it functions as a high-level interface for client processes to interact 
with the file system. The library defines a set of procedures that correspond to the various file system 
operations, such as creating a file, reading or writing data from a file, or listing the contents of a 
directory. These library procedures use the IPC mechanisms provided by the Yalnix operating system to send 
requests to the YFS file server process and receive responses back.

In addition to the required procedure calls, I included the following helper methods:
    1. genLenForPath: returns the length of the given path name.
    2. addFile: adds a file to the file table and returns the file descriptor of the newly added file.
    3. removeFile: removes an open file from the file table and frees the memory occupied by it.
    4. getFile: returns a pointer to the open_file struct at the specified index in the file_table array.
    5. sendPathMessage: sends a message to the file server with the specified operation and pathname.
    6. sendFileMessage: sends a file message with the specified operation to the file server.
    7. sendLinkMessage: sends a link message to the file server with the specified operation, oldname 
       and newname.
    8. sendReadLinkMessage: sends a message to the file server requesting to read the contents of the 
       symbolic link at the specified path and store it in the provided buffer.
    9. sendSeekMessage: sends a message to the file server requesting a change in the current file offset 
       for the file associated with the specified inode number. 
    10. sendStatMessage: sends a message to the file server requesting file metadata for the file specified 
        by pathname.
    11. sendGenericMessage: sends a generic message to the file server with the specified operation.

This file also includes the following data structures: 
    1. open_file: A struct that contains information about an open file, including its inode number and the 
    current position in the file.
-----------------------------------------------------------------------------------------------------------

message.c -------------------------------------------------------------------------------------------------
This file contains the implementation of the interaction between yfs (a simple file system) and the user 
library. It receives messages from a user process, determines the type of message based on the requested 
operation, extracts the necessary information from the message, and calls the corresponding YFS function 
to handle the operation.

message.c includes a function to get a path from a process, which receives a process ID and a pointer to 
a buffer, and returns the path of the buffer as a string.

The main function processRequest() initializes variables to hold the received message and the return value 
of the YFS function that will handle the request. It then receives the message as a generic type and checks 
if the message was received successfully. The function determines the type of message based on the requested 
operation and extracts the necessary information from the message. Depending on the operation, it calls one 
of the YFS functions to handle the request.

The function handles the following message types:
    - YFS_OPEN: extracts the pathname and calls yfsOpen().
    - YFS_CREATE: extracts the pathname and calls yfsCreate().
    - YFS_READ: extracts the inode number, buffer, size, and offset, and calls yfsRead().
    - YFS_WRITE: extracts the inode number, buffer, size, and offset, and calls yfsWrite().
    - YFS_SEEK: extracts the inode number, offset, whence, and current position, and calls yfsSeek().
    - YFS_LINK: extracts the old and new path names for the link, and calls yfsLink().
    - YFS_UNLINK: extracts the path name for the file to unlink, and calls yfsUnlink().
    - YFS_SYMLINK: extracts the old and new path names for the symbolic link, and calls yfsSymLink().

Overall, this file provides the interface between user processes and yfs, allowing users to interact 
with the file system through a set of predefined message types.
-----------------------------------------------------------------------------------------------------------

hash_table.c ----------------------------------------------------------------------------------------------
This file, along with hash_table.h, come from the hash_table files made publicly-available for the 
COMP 321 labs. They have been modified to take an integer as a key and a size in the constructor.
Hash tables are used in yfs to handle the cache. I used hash tables to allow a
block in the cache to be found quickly given the block’s disk block number and allow an inode in the 
cache to be found quickly given the inode’s inode number.
-----------------------------------------------------------------------------------------------------------

If any details remain unclear, feel free to read the comments and documentation in the appropriate 
file, as they provide better insight on the design of my program.

___________________________________________________________________________________________________________
___________________________________________________________________________________________________________

TESTING
Included in my lab3 folder are the following test programs (all obtained either through the 
samples folder for lab3 or through publicly-available tests for lab3):

1. sample1: This program tests the functionality of a file system by performing various file operations 
   like creating files, writing data to them, closing files, creating directories, and finally shutting 
   down the file system.
2. sample2:  This program tests the functionality of a file system by creating empty files.
3. tcreate: This program tests the functionality of a file system by performing various operations such 
   as creating a directory, syncing the data to disk, delaying execution, and printing output to the console.
4. tcreate2: This program tests the functionality of a file system by performing various directory 
   creation operations and testing the behavior of the system when attempting to create directories with 
   the same name or within a non-existent parent directory.
5. test_mkdir_rmdir: This program tests the functionality of the file system's directory operations by 
   creating and deleting directories in a loop.
6. test_recursive_symlink: This program tests the functionality of the file system's symbolic links 
   and path resolution.
7. test_sym_hard: This program tests the functionality of a file system by creating directories, changing 
   directories, creating files, writing to files, creating hard and symbolic links, and reading from files. 
   It also tests the behavior of the file system when dealing with nested directories and symbolic links.
8. tlink: This program tests the file system's ability to create files and create hard links between them.
9. tls: This program tests the functionality of the file system's directory listing capabilities, similar 
   to the Unix command ls.
10. topen2: This program tests the Open function in the yfs file system.
11. tsymlink: This program tests several file system operations such as creating a file, creating a symbolic 
    link, reading a symbolic link, getting file information (using Stat), opening a file, writing to a file, 
    and reading from a file.
12. tunlink2: This program tests the behavior of the Unlink() function in the file system. 
13. writeread: This program tests the file system's ability to create, write to, read from, and delete a file.
14. test_create_read_write: This program tests the Create(), Read(), and Write() functions on files and directories.
15. test_create_read_write_subdir: This function tests creating, reading, and writing subdirectories.

I ran all of these tests on yalnix in order to test the functionality of my yfs server. In addition to these
tests, as I was writing the server and yfs library, I included TracePrintf statements to track the actions of 
my program. Whenever an error would occur, these print statements made it easier for me to go back into my code 
and find the last line that functioned correctly, and as a result where the program began going wrong.