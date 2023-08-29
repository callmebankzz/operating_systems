#include <stdbool.h>
#include <comp421/iolib.h>

#define INODESPERBLOCK (BLOCKSIZE / INODESIZE)
#define CREATE_NEW -1

/* Defining Struct Types. */
typedef struct freeInode freeInode;
typedef struct freeBlock freeBlock;
typedef struct cacheItem cacheItem;
typedef struct queue queue;

/* Struct for items in the cache. */ 
struct cacheItem {
    int number; // block number
    bool dirty; // dirty flag to track changes to the block
    void *addr; // pointer to block data
    cacheItem *prevItem; // pointer to previous item in the cache
    cacheItem *nextItem; // pointer to next item in the cache
};

/* Struct for free inodes. */
struct freeInode {
    int inodeNumber; // inode number
    freeInode *next; // pointer to next free inode in the list
};

/* Struct for free blocks. */
struct freeBlock {
    int blockNumber; // block number
    freeBlock *next; // pointer to next free block in the list
};

/* Struct for a queue of cache items. */
struct queue {
    cacheItem *firstItem; // pointer to first item in the queue
    cacheItem *lastItem; // pointer to last item in the queue
};

/* Function Prototypes. */
void *getBlock(int blockNumber);
void destroyCacheItem(cacheItem *item);
struct inode* getInode(int inodeNum);
void addFreeInodeToList(int inodeNum);
void buildFreeInodeAndBlockLists();
int getNextFreeBlockNum();
int getDirectoryEntry(char *pathname, int inodeStartNumber, int *blockNumPtr, bool createIfNeeded);
int yfsCreate(char *pathname, int currentInode, int inodeNumToSet);
int yfsOpen(char *pathname, int currentInode);
int yfsRead(int inodeNum, void *buf, int size, int byteOffset, int pid);
int yfsWrite(int inodeNum, void *buf, int size, int byteOffset, int pid);
int yfsLink(char *oldName, char *newName, int currentInode);
int yfsUnlink(char *pathname, int currentInode);
int yfsSymLink(char *oldname, char *newname, int currentInode);
int yfsReadLink(char *pathname, char *buf, int len, int currentInode, int pid);
int yfsMkDir(char *pathname, int currentInode);
int yfsRmDir(char *pathname, int currentInode);
int yfsChDir(char *pathname, int currentInode);
int yfsStat(char *pathname, int currentInode, struct Stat *statbuf, int pid);
int yfsSync(void);
int yfsShutdown(void);
int yfsSeek(int inodeNum, int offset, int whence, int currentPosition);
