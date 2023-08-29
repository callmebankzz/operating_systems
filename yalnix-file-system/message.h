#include <comp421/iolib.h>


/* message types and IPC API */
#define YFS_OPEN        0
#define YFS_CREATE      1
#define YFS_READ        2
#define YFS_WRITE       3
#define YFS_SEEK        4
#define YFS_LINK        5
#define YFS_UNLINK      6
#define YFS_SYMLINK     7
#define YFS_READLINK    8
#define YFS_MKDIR       9
#define YFS_RMDIR       10
#define YFS_CHDIR       11
#define YFS_STAT        12
#define YFS_SYNC        13
#define YFS_SHUTDOWN    14

/*
 * Structure for a generic message that can only hold only
 * a single integer. 
 * 
 * Notes:
 *  - Useful for Sync and Shutdown operations.
 */
struct message_generic {
    int num;
    char padding[28];
};

/* Structure for messages useful for sending a pathname. */
struct message_path {
    int num;
    int current_inode;
    char *pathname;
    int len;
    char padding[12];
};

/* Structure for messages useful for requesting file access. */
struct message_file {
    int num;
    int inodenum;
    void *buf;
    int size;
    int offset;
    char padding[8];
};

/* Structure for a Link operation message. */
struct message_link {
    int num;
    int current_inode;
    char *old_name;
    char *new_name;
    int old_len;
    int new_len;
};

/* Structure for a ReadLink operation message. */
struct message_readlink {
    int num;
    int current_inode;
    char *pathname;
    char *buf;
    int path_len;
    int len;
};

/* Structure for a Seek operation message. */
struct message_seek {
    int num;
    int inodenum;
    int current_position;
    int offset;
    int whence;
    char padding[12];
};

/* Structure for a Stat operation message. */
struct message_stat {
    int num;
    int current_inode;
    char *pathname;
    int len;
    struct Stat *statbuf;
};

/* Function to process message requests. */
void processRequest(void);
