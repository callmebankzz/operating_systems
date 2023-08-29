#include <stdlib.h>
#include <string.h>

#include <comp421/filesystem.h>
#include <comp421/iolib.h>
#include <comp421/yalnix.h>

#include "hash_table.h"
#include "message.h"
#include "yfs.h"


/**
 * The YFS file server process is responsible for managing the file system, including 
 * handling file system requests from client processes, maintaining file system 
 * structures such as directories and inodes, and interacting with the underlying 
 * storage media. The server process communicates with client processes using inter-
 * process communication (IPC) mechanisms provided by the Yalnix operating system.
 */

/* Global Variables */

// Define a load factor for the hash tables
#define LOADFACTOR 1.5 

// Initialize pointers to the first free inode and block
freeInode *firstFreeInode = NULL;
freeBlock *firstFreeBlock = NULL;

// Initialize counts for the number of free inodes and blocks

int freeInodeCount = 0;
int freeBlockCount = 0;
int currentInode = ROOTINODE;

int numSymLinks = 0;

// Initialize queues and hash tables for caching inode and block data

queue *cacheInodeQueue;
struct hash_table *inodeTable;
int inodeCacheSize = 0;

queue *cacheBlockQueue;
struct hash_table *blockTable;
int blockCacheSize = 0;


/**
 * This function initializes the file server data structures.
 * 
 * Inputs: None.
 * 
 * Outputs: None.
 * 
 */
void 
init(void) 
{
    cacheInodeQueue = malloc(sizeof(queue));
    cacheInodeQueue->firstItem = NULL;
    cacheInodeQueue->lastItem = NULL;
    
    cacheBlockQueue = malloc(sizeof(queue));
    cacheBlockQueue->firstItem = NULL;
    cacheBlockQueue->lastItem = NULL;
    inodeTable = hash_table_create(LOADFACTOR, INODE_CACHESIZE + 1);
    blockTable = hash_table_create(LOADFACTOR, BLOCK_CACHESIZE + 1);
    buildFreeInodeAndBlockLists();
    
    if (Register(FILE_SERVER) != 0) {
        TracePrintf(1, "error registering file server as a service\n");
        Exit(1);
    };
    
}


/* Helper Functions */


/**
 * This function removes the first item from the specified queue and 
 * returns a pointer to it.
 * 
 * Inputs:
 *  queue: a pointer to the queue from which to remove the item.
 * 
 * Outputs:
 *  Upon success, returns a pointer to the cache item that was removed from the queue.
 *  Otherwise, returns NULL.
 * 
 */
cacheItem *
removeItemFromFrontOfQueue(queue *queue)
{
    cacheItem *firstItem = queue->firstItem;
    // If the queue is empty, return NULL
    if (firstItem == NULL) {
        return NULL;
    }

    // If there is only one item in the queue, update lastItem pointer
    if (queue->firstItem->nextItem == NULL) {
        queue->lastItem = NULL;
    }

    // Update pointers and return removed item
    queue->firstItem->prevItem = NULL;
    queue->firstItem = queue->firstItem->nextItem;
    if (queue->firstItem != NULL) {
        queue->firstItem->prevItem = NULL;
    }
    return firstItem;

}

/**
 * This function prints the contents of the specified queue to the console.
 * 
 * Inputs:
 *  queue: a pointer to the queue to print.
 * 
 * Outputs: None.
 */
void
printQueue(queue *queue)
{
    cacheItem *item = queue->firstItem; // Initialize a pointer to the first item in the queue.
    TracePrintf(1, "-----------------------\n"); // Print a separator to the console.
    while (item != NULL) { // Iterate over each item in the queue.
        TracePrintf(1, "%d\n", item->number); // Print the item's number to the console.
        item = item->nextItem; // Move the pointer to the next item in the queue.
    }
    if (queue->lastItem != NULL) // If the last item in the queue is not NULL:
        TracePrintf(1, "last item = %d\n", queue->lastItem->number); // Print the number of the last item to the console.
    TracePrintf(1, "-----------------------\n"); // Print a separator to the console.
}

/**
 * This function removes the specified item from the specified queue.
 * 
 * Inputs:
 *  queue: a pointer to the queue to remove the item from.
 *  item: a pointer to the item to remove from the queue.
 * 
 * Outputs: None.
 * 
 */
void
removeItemFromQueue(queue *queue, cacheItem *item)
{
    if (item->prevItem == NULL) { // If the item is the first item in the queue:
        removeItemFromFrontOfQueue(queue); // Remove it using the removeItemFromFrontOfQueue function.
    } else {
        if (item->nextItem == NULL) { // If the item is the last item in the queue:
            queue->lastItem = item->prevItem; // Update the last item in the queue to be the item's predecessor.
        }
        item->prevItem->nextItem = item->nextItem; // Update the predecessor's next pointer to skip the item.
        if (item->nextItem != NULL) {
            item->nextItem->prevItem = item->prevItem; // Update the successor's prev pointer to skip the item.
        }
    }
}

/**
 * This function adds the given cache item to the end of the specified queue.
 * 
 * Inputs:
 *  item: a pointer to the cache item to be added to the queue.
 *  queue: a pointer to the queue to which the item is to be added.
 * 
 * Outputs: None.
 * 
 */
void
addItemEndQueue(cacheItem *item, queue *queue)
{
    // if the queue is empty
    if (queue->firstItem == NULL) {
        if (queue == cacheBlockQueue)
        item->nextItem = NULL;
        item->prevItem = NULL;
        queue->lastItem = item;
        queue->firstItem = item;
    } else {    // if the queue is nonempty
        queue->lastItem->nextItem = item;
        item->prevItem = queue->lastItem;
        queue->lastItem = item;
        queue->lastItem->nextItem = NULL;
    }
}

/**
 * This function checks whether the given path and directory entry name are equal.
 * 
 * Inputs:
 *  path: a pointer to a string containing the path of the file or directory.
 *  dirEntryName: a character array representing the directory entry name to be compared.
 * 
 * Outputs: 
 *  Returns true if the given path and directory entry name are equal. 
 *  Otherwise, returns false.
 * 
 */
bool
isEqual(char *path, char dirEntryName[])
{
    int i = 0;
    while (i < DIRNAMELEN) { // Iterate over the maximum length of a directory entry name.
        if ((path[i] == '/' || path[i] == '\0') && dirEntryName[i] == '\0') { // If the end of the path and the end of the directory entry name have been reached:
            return true; // Return true, indicating that the two are equal.
        }
        if (path[i] != dirEntryName[i]) { // If the current characters of the path and directory entry name are not equal:
            return false; // Return false, indicating that the two are not equal.
        }
        i++; // Move to the next character in the path and directory entry name.
    }
    return true; // If the loop completes without returning, then the two are equal. Return true.
}

/**
 * This function marks the cache item associated with the given block number as dirty.
 * 
 * Inputs:
 *  blockNumber: an integer representing the block number to.
 * 
 * Outputs: None.
 *  
 */
void
saveBlock(int blockNumber) 
{
    // Lookup the block item ptr in the hashmap.
    cacheItem *blockItem = (cacheItem *)hash_table_lookup(blockTable, blockNumber);
    // Mark block item as dirty.
    blockItem->dirty = true;
}

/**
 * This function is used to retrieve a block of data either from the cache or from 
 * disk if the block is not already in the cache. 
 * 
 * Inputs:
 *  blockNumber: an integer representing the block number to retrieve from cache or disk.
 * 
 * Outputs: 
 *  A pointer to the requested block of data.
 * 
 */
void *
getBlock(int blockNumber) 
{
    //TracePrintf(1, "GETTING BLOCK #%d\n", blockNumber);
    // First check to see if Block is in the cache using hashmap
    // If it is, remove it from the middle of the block queue add it to the front
    // return the pointer to it
    cacheItem *blockItem = (cacheItem *)hash_table_lookup(blockTable, blockNumber);
    
    if (blockItem != NULL) {
        removeItemFromQueue(cacheBlockQueue, blockItem);
        addItemEndQueue(blockItem, cacheBlockQueue);
        return blockItem->addr;
    }
    
    // If the block is not in the cache
    
    // If the cache is full, remove the LRU block from the end of the queue, 
    // and get the block number
    // Use the block number to remove it from the hashmap
    if (blockCacheSize == BLOCK_CACHESIZE) {
        cacheItem *lruBlockItem = removeItemFromFrontOfQueue(cacheBlockQueue);
        int lruBlockNum = lruBlockItem->number;
        WriteSector(lruBlockNum, lruBlockItem->addr);
        blockCacheSize--;
        hash_table_remove(blockTable, lruBlockNum, NULL, NULL);
        destroyCacheItem(lruBlockItem);
    }
    
    // Allocate space for the new block, read it from disk. 
    // Add the new block to the front of the LRU queue and add it to the 
    // hashmap and then return the pointer to the new block.
    void *block = malloc(BLOCKSIZE);
    ReadSector(blockNumber, block);
    cacheItem *newItem = malloc(sizeof(cacheItem));
    newItem->number = blockNumber;
    newItem->addr = block;
    newItem->dirty = false;
    
    addItemEndQueue(newItem, cacheBlockQueue);
    blockCacheSize++;
    hash_table_insert(blockTable, blockNumber, newItem);
    return block;
}

/**
 * This function checks marks the inode as dirty in the cache.
 * 
 * Inputs:
 *  inodeNum: An integer representing the inode number of the inode that needs 
 *  to be marked as dirty in the cache.
 * 
 * Outputs: None.
 * 
 */
void
saveInode(int inodeNum) 
{

    // Lookup the inode ptr in the hashmap.
    cacheItem *inodeItem = (cacheItem *)hash_table_lookup(inodeTable, inodeNum);
    
    // Mark the inode as dirty.
    inodeItem->dirty = true;
}

/**
 * This function retrieves an inode from the inode cache or disk, depending on 
 * whether or not the inode is currently cached. If the inode is cached, it is 
 * removed from its position in the cache queue and added to the front, and the 
 * pointer to the inode is returned. If the inode is not cached, the least recently 
 * used inode is removed from the cache, its contents are saved to disk, and the new 
 * inode is read from disk and added to the cache. Finally, a pointer to the 
 * newly-cached inode is returned.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number of the inode to be retrieved.
 * 
 * Outputs: 
 *  A pointer to a struct inode, representing the retrieved inode.
 * 
 */
struct inode*
getInode(int inodeNum) 
{
    // First, check to see if inode is in the cache using hashmap
    // If it is, remove it from the middle of the inode queue and add it to the front
    // return the pointer to the inode
    cacheItem *nodeItem = (cacheItem *)hash_table_lookup(inodeTable, inodeNum);
    if (nodeItem != NULL) {
        removeItemFromQueue(cacheInodeQueue, nodeItem);
        addItemEndQueue(nodeItem, cacheInodeQueue);
        return nodeItem->addr;
    }
    
    // If the cache is full:
        // Get the lru inode in the cache, remove it from the hashmap.
        // Get the block number corresponding to lru inode.
        // Get the block corresponding to this block number.
        // Get the correct address corresponding to this inode within that block.
        // Copy the contents of the lru inode into this address.
        // Call save block on that block.
    if (inodeCacheSize == INODE_CACHESIZE) {
        cacheItem *lruInode = removeItemFromFrontOfQueue(cacheInodeQueue);
        int lruInodeNum = lruInode->number;
        inodeCacheSize--;
        hash_table_remove(inodeTable, lruInodeNum, NULL, NULL);
        int lruBlockNum = (lruInodeNum / INODESPERBLOCK) + 1;
        
        void *lruBlock = getBlock(lruBlockNum);
        void *inodeAddrInBlock = (lruBlock + (lruInodeNum - (lruBlockNum - 1) * INODESPERBLOCK) * INODESIZE);
        
        memcpy(inodeAddrInBlock, lruInode->addr, sizeof(struct inode));
        saveBlock(lruBlockNum);
        
        destroyCacheItem(lruInode);
    }
    
    // Get the block number corresponding to this new inode.
    int blockNum = (inodeNum / INODESPERBLOCK) + 1;
    
    // Get the block address for this inode.
    void *blockAddr = getBlock(blockNum);
    
    // Look up the inodes address within the block.
    struct inode *newInodeAddrInBlock = (struct inode *)(blockAddr + (inodeNum - (blockNum - 1) * INODESPERBLOCK) * INODESIZE);
    
    // Copy the contents of the inode into a newly allocated inode.
    struct inode *inodeCpy = malloc(sizeof(struct inode));
    struct cacheItem *inodeItem = malloc(sizeof(struct cacheItem));
    memcpy(inodeCpy, newInodeAddrInBlock, sizeof(struct inode));
    inodeItem->addr = inodeCpy;
    inodeItem->number = inodeNum;
    
    // Add this inode to the front of the LRU queue and add it to the hashmap.
    addItemEndQueue(inodeItem, cacheInodeQueue);
    inodeCacheSize++;
    hash_table_insert(inodeTable, inodeNum, inodeItem);
    
    // Return the address of the new inode.
    return inodeItem->addr;
}

/**
 * This function deallocates the memory allocated for a cache item. 
 * It takes a pointer to a cache item as input and frees the memory allocated for 
 * the address of the inode stored in the cache item and the cache item itself.
 * 
 * Inputs:
 *  item: A pointer to the cache item that needs to be deallocated.
 * 
 * Outputs: None.
 * 
 */
void
destroyCacheItem(cacheItem *item) 
{
    free(item->addr);
    free(item);
}

/**
 * This function calculates the block number that contains the specified inode 
 * by dividing the inode number by the number of inodes per block (INODESPERBLOCK) 
 * and adding 1. It then calls the getBlock() function to retrieve a pointer to 
 * the data block that contains the specified inode. The retrieved pointer is then 
 * returned as the output of the function.
 * 
 * Inputs:
 *  inodeNumber: an integer representing the inode number for which we want 
 *  to retrieve the block.
 * 
 * Outputs: 
 *  A pointer to the block of data that contains the specified inode.
 * 
 */
void *
getInodeBlockNum(int inodeNumber) 
{
    // Calculate and return block number.
    int blockNumber = (inodeNumber / INODESPERBLOCK) + 1;
    return getBlock(blockNumber);
}

/**
 * This function takes an inode and an index n, and returns the block number of the 
 * nth block of the file, allocating the block if necessary and if the 
 * allocateIfNeeded parameter is true.
 * 
 * Inputs:
 *  inode: a pointer to a struct inode representing the file
 *  n: an integer representing the index of the block to retrieve
 *  allocateIfNeeded: a boolean indicating whether to allocate a new block if necessary
 * 
 * Outputs: 
 *  An integer representing the block number of the nth block of the file.
 *  Returns 0 if the given index is invalid or if the block could not be allocated.
 * 
 */
int
getNthBlock(struct inode *inode, int n, bool allocateIfNeeded) 
{
    bool isOver = false;
    if (n >= NUM_DIRECT + BLOCKSIZE / (int)sizeof(int)) {
        return 0;
    }
    if (n*BLOCKSIZE >= inode->size) 
    {
        isOver = true;
    }
    if (isOver && !allocateIfNeeded) {
        return 0;
    }
    if (n < NUM_DIRECT) {
        if (isOver) {
            inode->direct[n] = getNextFreeBlockNum();
        }
        // if getNextFreeBlockNum returned 0, return 0
        return inode->direct[n];
    } 
    //search the direct blocks
    int *indirectBlock = getBlock(inode->indirect);
    if (isOver) {
        // if getNextFreeBlockNum returned 0, return 0
        indirectBlock[n - NUM_DIRECT] = getNextFreeBlockNum();
    }
    int blockNum = indirectBlock[n - NUM_DIRECT];
    return blockNum;
}

/**
 * This function takes a path and the inode number for the starting directory, 
 * and returns the inode number for the file or directory represented by the path.
 * 
 * Inputs:
 *  path: the file path to get the inode number for.
 *  inodeStartNumber: the inode to start looking for the next part of the file path in.
 * 
 * Outputs: 
 *  The inode number of the path, or 0 if it's an invalid path.
 * 
 */
int
getPathInodeNumber(char *path, int inodeStartNumber)
{
    
    // Get the inode number for the first file in path 
    int nextInodeNumber = 0;

    // Get inode corresponding to inodeStartNumber
    void *block = getInodeBlockNum(inodeStartNumber);
    struct inode *inode = getInode(inodeStartNumber);
    if (inode->type == INODE_DIRECTORY) {
        // go get the directory entry in this directory
        // that has that name
        int blockNum;
        int offset = getDirectoryEntry(path, inodeStartNumber, &blockNum, false);
        if (offset != -1) {
            block = getBlock(blockNum);
            struct dir_entry * dir_entry = (struct dir_entry *) ((char *) block + offset);
            nextInodeNumber = dir_entry->inum;
        }
    } else if (inode->type == INODE_REGULAR) {
        return 0;
    } else if (inode->type == INODE_SYMLINK) {
        return 0;
    }
    char *nextPath = path;
    if (nextInodeNumber == 0) {
        // Return error
        return 0;
    }
    while (nextPath[0] != '/') {
        // base case
        if (nextPath[0] == '\0') {
            inode = getInode(nextInodeNumber);
            //TracePrintf(1, "are we a symlink?\n");
            if (inode->type != INODE_SYMLINK) {
                return nextInodeNumber;
            }
            else {
                nextPath = path;
                break;
            }
        }
        nextPath += sizeof (char);
    }
    while (nextPath[0] == '/') {
        nextPath += sizeof (char);
    }
    if (nextPath[0] == '\0') {
        return nextInodeNumber;
    }
    inode = getInode(nextInodeNumber);
    if (inode->type == INODE_SYMLINK) {
        numSymLinks++;
        if (numSymLinks > MAXSYMLINKS) {
            return 0;
        }
        int dataBlockNum = inode->direct[0];
        char *dataBlock = (char *)getBlock(dataBlockNum);
        if (dataBlock[0] == '/') {
            dataBlock += sizeof(char);
            inodeStartNumber = ROOTINODE;
        }
        nextInodeNumber = getPathInodeNumber(dataBlock, inodeStartNumber);
        while (nextPath[0] != '/') {
            if (nextPath[0] == '\0') {
                return nextInodeNumber;
            }
            nextPath += sizeof (char);
        }
        while (nextPath[0] == '/')
            nextPath += sizeof(char);        
    }
    return getPathInodeNumber(nextPath, nextInodeNumber);
}

/**
 * This function marks the given inode number as free by modifying its type to 
 * INODE_FREE, and adds the inode number to the list of free inodes.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number to be marked as free.
 * 
 * Outputs: None.
 * 
 */
void
freeUpInode(int inodeNum) 
{
    // Get address of this inode. 
    struct inode *inode = getInode(inodeNum);
    
    // Modify the type of inode to free.
    inode->type = INODE_FREE;

    addFreeInodeToList(inodeNum);

    saveInode(inodeNum);
}

/**
 * This function gets the number of the next free inode in the file system, and 
 * updates the inode structure for that inode to indicate it has been reused.
 * 
 * Inputs: None.  
 * 
 * Outputs: 
 *  The number of the next free inode in the file system, or 0 if there are 
 *  no free inodes left.
 * 
 */
int 
getNextFreeInodeNum(void) 
{
    // Check if there are free inodes left
    if (firstFreeInode == NULL) {
        return 0;
    }

    // Get the inode number of the first free inode in the linked list
    int inodeNum = firstFreeInode->inodeNumber;
    // Get the inode structure for the inode number
    struct inode *inode = getInode(inodeNum);
    // Increment the reuse counter to indicate that the inode has been reused
    inode->reuse++;
    // Save the inode structure to disk
    saveInode(inodeNum);
    // Remove the first free inode from the linked list
    firstFreeInode = firstFreeInode->next;
    // Return the inode number of the reused inode
    return inodeNum;
}

/**
 * This function adds a new freeInode to the beginning of the linked 
 * list of free inodes.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number of the inode 
 *  that is being added to the list of free inodes.
 * 
 * Outputs: None.  
 */
void 
addFreeInodeToList(int inodeNum) 
{
    // Allocate memory for a new freeInode struct and initialize its values
    freeInode *newHead = malloc(sizeof(freeInode));
    newHead->inodeNumber = inodeNum;
    newHead->next = firstFreeInode;

    // Update the linked list with the new head node
    firstFreeInode = newHead;
    freeInodeCount++;
}

/**
 * This function returns the number of the next free block and updates 
 * the free list accordingly.
 * 
 * Inputs: None.  
 * 
 * Outputs: 
 *  Integer representing the number of the next free block.
 * 
 */
int 
getNextFreeBlockNum(void) 
{
    // If there are no free blocks left:
    if (firstFreeBlock == NULL) { 
        // Return 0 to indicate that there are no free blocks.
        return 0; 
    }
    // Get the block number of the first free block.
    int blockNum = firstFreeBlock->blockNumber; 
    // Update the free list by removing the first free block.
    firstFreeBlock = firstFreeBlock->next;
    // Return the block number of the next free block.
    return blockNum; 
}

/**
 * This function adds a new free block to the head of the free block list.
 * 
 * Inputs:
 *  blockNum: an integer representing the block number to be added to the list.
 * 
 * Outputs: None.  
 * 
 */
void
addFreeBlockToList(int blockNum) 
{
    // Allocate memory for the new free block and initialize its values.
    freeBlock *newHead = malloc(sizeof(freeBlock));
    newHead->blockNumber = blockNum;
    newHead->next = firstFreeBlock;

    // Update the head of the free block list to point to the new block.
    firstFreeBlock = newHead;

    // Increment the count of free blocks in the system.
    freeBlockCount++;
}

/**
 * This function builds the lists of free inodes and free blocks by examining the
 * file system blocks and inodes. 
 * 
 * Inputs: None.  
 * 
 * Outputs: None.  
 * 
 */
void
buildFreeInodeAndBlockLists(void) 
{
    
    int blockNum = 1;
    void *block = getBlock(blockNum);
    
    struct fs_header header = *((struct fs_header*) block);
    
    TracePrintf(1, "num_blocks: %d, num_inodes: %d\n", header.num_blocks,
        header.num_inodes);
    
    // create array indexed by block number
    bool takenBlocks[header.num_blocks];
    // initialize each item to false
    memset(takenBlocks, false, header.num_blocks * sizeof(bool));
    // sector 0 is taken
    takenBlocks[0] = true;
    
    // for each block that contains inodes
    int inodeNum = ROOTINODE;
    while (inodeNum < header.num_inodes) {
        // for each inode, if it's free, add it to the free list
        for (; inodeNum < INODESPERBLOCK * blockNum; inodeNum++) {
            struct inode *inode = getInode(inodeNum);
            if (inode->type == INODE_FREE) {
                addFreeInodeToList(inodeNum);
            } else {
                // keep track of all these blocks as taken
                int i = 0;
                int blockNum;
                while((blockNum = getNthBlock(inode, i++, false)) != 0) {
                    takenBlocks[blockNum] = true;
                }
            }
        }
        blockNum++;
        block = getBlock(blockNum);
    }
    TracePrintf(1, "initialized free inode list with %d free inodes\n", 
        freeInodeCount);
    
    // for each element in the block array
    int i;
    for (i = 0; i < header.num_blocks; i++) {
        if (!takenBlocks[i]) {
            // add block to list
            addFreeBlockToList(i);
        }
    }
    TracePrintf(1, "initialized free block list with %d free blocks\n", 
        freeBlockCount);
    
}

/**
 * This function clears the contents of a file.
 * 
 * Inputs:
 *  inode: a pointer to the inode of the file to be cleared.
 *  inodeNum: an integer representing the inode number of the file to be cleared.
 * 
 * Outputs: None.  
 * 
 */
void
clearFile(struct inode *inode, int inodeNum) 
{
    int i = 0;
    int blockNum;
    // Iterate over each block in the inode, adding it to the free block list.
    while ((blockNum = getNthBlock(inode, i++, false)) != 0) {
        addFreeBlockToList(blockNum);
    }
    // Reset the size of the file to 0 and save the inode.
    inode->size = 0;
    saveInode(inodeNum);
}

/**
 * This function retrieves the directory entry of a file or creates it if it does not exist.
 * 
 * Inputs:
 *  pathname: a string representing the path to the file.
 *  inodeStartNumber: an integer representing the starting inode number to search for the file.
 *  blockNumPtr: a pointer to an integer that will be set to the block number of the directory entry.
 *  createIfNeeded: a boolean indicating whether to create the directory entry if it does not exist.
 * 
 * Outputs: 
 *  An integer representing the offset of the directory entry within its block, or -1 if the 
 *  directory entry does not exist and createIfNeeded is false.
 * 
 */
int
getDirectoryEntry(char *pathname, int inodeStartNumber, int *blockNumPtr, bool createIfNeeded) 
{
    int freeEntryOffset = -1;
    int freeEntryBlockNum = 0;
    void * currentBlock;
    struct dir_entry *currentEntry;
    struct inode *inode = getInode(inodeStartNumber);
    int i = 0;
    int blockNum = getNthBlock(inode, i, false);
    int currBlockNum = 0;
    int totalSize = sizeof (struct dir_entry);
    bool isFound = false;
    while (blockNum != 0 && !isFound) {
        currentBlock = getBlock(blockNum);
        currentEntry = (struct dir_entry *) currentBlock;
        while (totalSize <= inode->size 
                && ((char *) currentEntry < ((char *) currentBlock + BLOCKSIZE))) 
        {
            if (freeEntryOffset == -1 && currentEntry->inum == 0) {
                freeEntryBlockNum = blockNum;
                freeEntryOffset = (int)((char *)currentEntry - (char *)currentBlock);
            }
            
            //check the currentEntry fileName to see if it matches
            TracePrintf(1, "current entry->name - %s\n", currentEntry->name);
            if (isEqual(pathname, currentEntry->name)) {
                isFound = true;
                break;
            }
            
            //increment current entry
            currentEntry = (struct dir_entry *) ((char *) currentEntry + sizeof (struct dir_entry));
            totalSize += sizeof (struct dir_entry);
        }
        if (isFound) {
            break;
        }
        currBlockNum = blockNum;
        blockNum = getNthBlock(inode, ++i, false);
    }
    *blockNumPtr = blockNum;

    if (isFound) {
        int offset = (int)((char *)currentEntry - (char *)currentBlock);
        return offset;
    } 
    if (createIfNeeded) {
        if (freeEntryBlockNum != 0) {
            *blockNumPtr = freeEntryBlockNum;
            return freeEntryOffset;
        }
        if (inode->size % BLOCKSIZE == 0) {
            // we're at the bottom edge of the block, so
            // we need to allocate a new block
            blockNum = getNthBlock(inode, i, true);
            currentBlock = getBlock(blockNum);
            inode->size += sizeof(struct dir_entry);
            struct dir_entry * newEntry = (struct dir_entry *) currentBlock;
            newEntry->inum = 0;
            saveBlock(blockNum);
            saveInode(inodeStartNumber);
            *blockNumPtr = blockNum;
            return 0;
        } 
        inode->size += sizeof(struct dir_entry);
        saveInode(inodeStartNumber);
        currentEntry->inum = 0;
        saveBlock(currBlockNum);
        *blockNumPtr = currBlockNum;
        int offset = (int)((char *)currentEntry - (char *)currentBlock);
        return offset;
    }
    return -1;
}

/**
 * This function gets the inode number of the directory containing a given file, based 
 * on its pathname and the inode number of the current working directory.
 * 
 * Inputs:
 *  pathname: a string representing the absolute or relative pathname of the file whose 
 *  containing directory's inode.
 *  currentInode: an integer representing the inode number of the current working directory.
 *  filenamePtr: a pointer to a character pointer, which will be set to point to the filename 
 *  in the input pathname.
 * 
 * Outputs: 
 *  Upon success, an integer representing the inode number of the directory containing the file. 
 *  Otherwise, returns ERROR.
 * 
 */
int
getContainingDirectory(char *pathname, int currentInode, char **filenamePtr) 
{
    // error checking
    int i;
    for (i = 0; i < MAXPATHNAMELEN; i++) {
        if (pathname[i] == '\0') {
            break;
        }
    }
    if (i == MAXPATHNAMELEN) {
        return ERROR;
    }
    
    // adjust pathname
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
        currentInode = ROOTINODE;
    }
    
    // Truncate the pathname to NOT include the name of the file to create
    int lastSlashIndex = 0;
    i = 0;
    while ((pathname[i] != '\0') && (i < MAXPATHNAMELEN)) {
        if (pathname[i] == '/') {
            lastSlashIndex = i;
        }
        i++;
    }
    
    if (lastSlashIndex != 0) {
        char path[lastSlashIndex + 1];
        for (i = 0; i < lastSlashIndex; i++) {
            path[i] = pathname[i];
        }
        path[i] = '\0';

        char *filename = pathname + (sizeof(char) * (lastSlashIndex + 1));
        *filenamePtr = filename;
        // Get the inode of the directory the file should be created in
        numSymLinks = 0;
        int dirInodeNum = getPathInodeNumber(path, currentInode);
        if (dirInodeNum == 0) {
            return ERROR;
        }
        return dirInodeNum;
    } else {
        
        *filenamePtr = pathname;
        return currentInode;
    }
}


/* Required Procedure Call Requests */

/**
 * This function opens a file with the given pathname.
 * 
 * Inputs:
 *  pathname: a string representing the path of the file to be opened.
 *  currentInode: an integer representing the inode number of the current directory.
 * 
 * Outputs: 
 *  Upon success, returns the inode number of the file if it exists and can be opened.
 *  Otherwise, returns ERROR.
 * 
 */
int 
yfsOpen(char *pathname, int currentInode) 
{
    // Check that inputs are valid
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }
    
    // If the path is absolute, start from the root inode
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
        currentInode = ROOTINODE;
    }
    
    // Reset the count of symbolic links
    numSymLinks = 0;
    // Get the inode number of the file at the given pathname
    int inodenum = getPathInodeNumber(pathname, currentInode);
    // If the file does not exist, return an error
    if (inodenum == 0) {
        return ERROR;
    }
    // Return the inode number of the file
    return inodenum;
}

/**
 * This function creates a new file or opens an existing file for writing.
 * 
 * Inputs:
 *  pathname: a string representing the path of the file to be created or opened.
 *  currentInode: an integer representing the inode number of the current directory.
 *  inodeNumToSet: an integer representing the inode number to set for the newly created file, or -1 for create new.
 * 
 * Outputs: 
 *  Upon success, returns the inode number of the newly created or opened file.
 *  Otherwise, returns ERROR.
 * 
 */
int
yfsCreate(char *pathname, int currentInode, int inodeNumToSet) 
{

    // Check for invalid inputs.
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }
    // Check for invalid pathname length.
    int i;
    for (i = 0; i < MAXPATHNAMELEN; i++) {
        if (pathname[i] == '\0') {
            break;
        }
    }
    if (i == MAXPATHNAMELEN) {
        return ERROR;
    }
    
    // Check for invalid pathname format.
    i = 0;
    while ((pathname[i] != '\0') && (i < MAXPATHNAMELEN)) {
        if (pathname[i] == '/') {
            if (i+1 > MAXPATHNAMELEN || pathname[i+1] == '\0') {
                return ERROR;
            }
        }
        i++;
    }
    TracePrintf(1, "Creating %s in %d\n", pathname, currentInode);

    // Get the inode number of the containing directory of the file to be created.
    char *filename;
    int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);
    TracePrintf(1, "containind dirInodenum = %d\n", dirInodeNum);
    if (dirInodeNum == ERROR) {
        return ERROR;
    }

    // Check that the containing directory inode is actually a directory.
    struct inode *dirInode = getInode(dirInodeNum);
    if (dirInode->type != INODE_DIRECTORY) {
        return ERROR;
    }

    // Search all directory entries of that inode for the file name to create.
    int blockNum;
    TracePrintf(1, "getting directory entry: %s in inode %d\n", filename, dirInodeNum);
    int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
    TracePrintf(1, "offset = %d, blockNum = %d\n", offset, blockNum);
    void *block = getBlock(blockNum);
    struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);

    // If the file exists, get the inode, set its size to zero, and return
    // that inode number to user
    int inodeNum = dir_entry->inum;
    if (inodeNum != 0) {
        if (inodeNumToSet != -1) {
            return ERROR;
        }
        int inodeNum = dir_entry->inum;
        struct inode *inode = getInode(inodeNum);
        clearFile(inode, inodeNum);
        
        saveInode(inodeNum);
        return inodeNum;
    }
    
    // If the file does not exist, find the first free directory entry, get
    // a new inode number from free list, get that inode, change the info on 
    // that inode and directory entry (name, type), then return the inode number.
    for (i = 0; i<DIRNAMELEN; i++) {
        dir_entry->name[i] = '\0';
    }
    for (i = 0; filename[i] != '\0'; i++) {
        dir_entry->name[i] = filename[i];
    }
    TracePrintf(1, "new directory entry name: %s\n", dir_entry->name);
    if (inodeNumToSet == CREATE_NEW) {
        // Create a new inode for the file and set the directory entry to point to it
        TracePrintf(1, "Creating new!\n");
        // Get the next available inode number and set it as the inode number for the file
        inodeNum = getNextFreeInodeNum();
        TracePrintf(1, "new inodeNum = %d\n", inodeNum);
        dir_entry->inum = inodeNum;
        // Save the changes to the disk
        saveBlock(blockNum);
        // Get the inode struct for the new file and set its properties
        struct inode *inode = getInode(inodeNum);
        inode->type = INODE_REGULAR;
        inode->size = 0;
        inode->nlink = 1;
        // Save the changes to the disk
        saveInode(inodeNum);
        // Return the new inode number
        return inodeNum;
    } else {
        // Set the directory entry to point to the specified inode number
        dir_entry->inum = inodeNumToSet;
        // Save the changes to the disk
        saveBlock(blockNum);
        // Return the specified inode number
        return inodeNumToSet;
    }
}

/**
 * This function reads data from a file starting at the given byte offset and 
 * copies it into a buffer.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number of the file to be read.
 *  buf: a pointer to a buffer where the read data will be copied.
 *  size: an integer representing the number of bytes to be read.
 *  byteOffset: an integer representing the byte offset from which to start reading.
 *  pid: an integer representing the process ID of the calling process.
 * 
 * Outputs: 
 *  Upon success, returns the number of bytes read. Otherwise, returns ERROR.
 * 
 */
int
yfsRead(int inodeNum, void *buf, int size, int byteOffset, int pid) 
{
    // Check for invalid input parameters.
    if (buf == NULL || size < 0 || byteOffset < 0 || inodeNum <= 0) {
        return ERROR;
    }
    
    // Get the inode for the specified file.
    struct inode *inode = getInode(inodeNum);
    
    // Check if byteOffset is greater than the size of the file.
    if (byteOffset > inode->size) {
        return ERROR;
    }
    
    // Determine the number of bytes to read.
    int bytesLeft = size;
    if (inode->size - byteOffset < size) {
        bytesLeft = inode->size - byteOffset;
    }
    
    // Initialize the return value.
    int returnVal = bytesLeft;
    
    // Calculate the block and byte offsets.
    int blockOffset = byteOffset % BLOCKSIZE;

    int bytesToCopy = BLOCKSIZE - blockOffset;
    
    // Iterate over each block to read the data.
    int i;
    for (i = byteOffset / BLOCKSIZE; bytesLeft > 0; i++) {
        // Get the block number for the current block.
        int blockNum = getNthBlock(inode, i, false);
        if (blockNum == 0) {
            return ERROR;
        }
        
        // Get the data for the current block.
        void *currentBlock = getBlock(blockNum);
        
        // Determine the number of bytes to copy for the current block.
        if (bytesLeft < bytesToCopy) {
            bytesToCopy = bytesLeft;
        }
        
        // Copy the data from the current block to the buffer.
        if (CopyTo(pid, buf, (char *)currentBlock + blockOffset, bytesToCopy) == ERROR)
        {
            TracePrintf(1, "error copying %d bytes to pid %d\n", bytesToCopy, pid);
            return ERROR;
        }
        
        // Update the offsets and remaining bytes left to read.
        buf += bytesToCopy;
        blockOffset = 0;
        bytesLeft -= bytesToCopy;
        bytesToCopy = BLOCKSIZE;
    }
    
    // Return the number of bytes read.
    return returnVal;
}

/**
 * This function writes to a file.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number of the file to write to.
 *  buf: a pointer to a buffer containing the data to be written.
 *  size: an integer representing the number of bytes to write.
 *  byteOffset: an integer representing the starting offset of the write in bytes.
 *  pid: an integer representing the process ID of the process invoking the write.
 * 
 * Outputs: 
 *  Upon success, returns the number of bytes written to the file.
 *  Otherwise, returns ERROR.
 * 
 * Notes:
 *  - The function will fail if the inode is not of type INODE_REGULAR.
 *  - If the write extends beyond the current file size, the file size is updated accordingly.
 */
int 
yfsWrite(int inodeNum, void *buf, int size, int byteOffset, int pid) 
{
    // Get the inode of the file to write to.
    struct inode *inode = getInode(inodeNum);
    
    // Check if the inode is of type INODE_REGULAR.
    if (inode->type != INODE_REGULAR) {
        return ERROR;
    }
    
    int bytesLeft = size;
    
    int returnVal = bytesLeft;
    
    int blockOffset = byteOffset % BLOCKSIZE;

    int bytesToCopy = BLOCKSIZE - blockOffset;
    
    int i;
    // Loop through the blocks to be written to.
    for (i = byteOffset / BLOCKSIZE; bytesLeft > 0; i++) {
        // Get the block number to write to, creating it if necessary.
        int blockNum = getNthBlock(inode, i, true);
        if (blockNum == 0) {
            return ERROR;
        }
        // Get a pointer to the current block
        void *currentBlock = getBlock(blockNum);
        
        // Determine how many bytes to write to the block.
        if (bytesLeft < bytesToCopy) {
            bytesToCopy = bytesLeft;
        }
        
        // Copy data from the buffer to the block.
        if (CopyFrom(pid, (char *)currentBlock + blockOffset, buf, bytesToCopy) == ERROR)
        {
            TracePrintf(1, "error copying %d bytes from pid %d\n", bytesToCopy, pid);
            return ERROR;
        }

        buf += bytesToCopy;
        // Save the block.
        saveBlock(blockNum);
        
        blockOffset = 0;
        bytesLeft -= bytesToCopy;
        bytesToCopy = BLOCKSIZE;
        // Update the file size if necessary.
        int bytesWrittenSoFar = size - bytesLeft;
        if (bytesWrittenSoFar + byteOffset > inode->size) {
            inode->size = bytesWrittenSoFar + byteOffset;
        }
    }
    // Save the inode.
    saveInode(inodeNum);
    return returnVal;
}


/**
 * This function seeks to a specified position in a file based on the specified offset and whence values.
 * 
 * Inputs:
 *  inodeNum: an integer representing the inode number of the file to be seeked.
 *  offset: an integer representing the offset from the current position to be used in the seek operation.
 *  whence: an integer representing the starting position for the seek operation (SEEK_SET, SEEK_CUR, or SEEK_END).
 *  currentPosition: an integer representing the current position in the file.
 * 
 * Outputs: 
 *  Upon success, returns the new file position after the seek operation. Otherwise, returns ERROR.
 * 
 */
int 
yfsSeek(int inodeNum, int offset, int whence, int currentPosition) 
{
    // Initialize numSymLinks to 0
    numSymLinks = 0;

    // Get the inode for the specified inode number
    struct inode *inode = getInode(inodeNum);

    // Get the size of the file from the inode
    int size = inode->size;

    // Check if the current position is within the bounds of the file
    if (currentPosition > size || currentPosition < 0) {
        return ERROR;
    }

    // Handle the SEEK_SET case
    if (whence == SEEK_SET) {
        // Check if the new offset is within the bounds of the file
        if (offset < 0 || offset > size) {
            return ERROR;
        }
        // Return the new position after the seek operation
        return offset;
    }

    // Handle the SEEK_CUR case
    if (whence == SEEK_CUR) {
        // Check if the new position is within the bounds of the file
        if (currentPosition + offset > size || currentPosition + offset < 0) {
            return ERROR;
        }
        // Return the new position after the seek operation
        return currentPosition + offset;
    }

    // Handle the SEEK_END case
    if (whence == SEEK_END) {
        // Check if the new offset is within the bounds of the file
        if (offset > 0 || size + offset < 0) {
            return ERROR;
        }
        // Return the new position after the seek operation
        return size + offset;
    }

    // If the whence value is not valid, return an error
    return ERROR;
}

/**
 * This function creates a hard link between two existing files.
 * 
 * Inputs:
 *  oldName: a string representing the path of the existing file to be linked.
 *  newName: a string representing the path of the new file to be created as a hard link.
 *  currentInode: an integer representing the inode number of the current directory.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 * Notes:
 *  - Creates a new file with name 'newName' in the current directory that is a hard link 
 *    to the existing file 'oldName'.
 *  - Increments the hard link count of the inode for 'oldName'.
 * 
 */
int 
yfsLink(char *oldName, char *newName, int currentInode) 
{
    // Check for invalid inputs
    if (oldName == NULL || newName == NULL || currentInode <= 0) {
        return ERROR;
    }

    // If the old file name begins with a '/', update the current inode number to ROOTINODE
    if (oldName[0] == '/') {
        oldName += sizeof(char);
        currentInode = ROOTINODE;
    }

    // Retrieve the inode number of the old file
    numSymLinks = 0;
    int oldNameNodeNum = getPathInodeNumber(oldName, currentInode);
    struct inode *inode = getInode(oldNameNodeNum);

    // If the old file is a directory or the inode number is invalid, return an error
    if (inode->type == INODE_DIRECTORY || oldNameNodeNum == 0) {
        return ERROR;
    }

    // If the new file name begins with a '/', update the current inode number to ROOTINODE
    if (newName[0] == '/') {
        newName += sizeof(char);
        currentInode = ROOTINODE;
    }

    // Create the new file as a hard link to the old file
    if (yfsCreate(newName, currentInode, oldNameNodeNum) == ERROR) {
        return ERROR;
    }

    // Increment the hard link count of the old file inode and save it
    inode->nlink++;
    saveInode(oldNameNodeNum);
    return 0;
}

/**
 * This function unlinks a file at the specified path, removing its directory entry
 * from its containing directory and, if necessary, clearing its inode.
 * 
 * Inputs:
 *  pathname: a string representing the path of the file to be unlinked.
 *  currentInode: an integer representing the inode number of the current directory.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
yfsUnlink(char *pathname, int currentInode) 
{
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }
    
    // Get the containind directory 
    char *filename;
    int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);
    
    struct inode *dirInode = getInode(dirInodeNum);
    if (dirInode->type != INODE_DIRECTORY) {
        return ERROR;
    }

    int blockNum;
    int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, false);
    if (offset == -1) {
        return ERROR;
    }
    void *block = getBlock(blockNum);

    // Get the directory entry associated with the path
    struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);
    
    // Get the inode associated with the directory entry
    int inodeNum = dir_entry->inum;
    struct inode *inode = getInode(inodeNum);
    
    // Decrease nlinks by 1
    inode->nlink--;
    
    // If nlinks == 0, clear the file
    if (inode->nlink == 0) {
        clearFile(inode, inodeNum);
    } 
    
    saveInode(inodeNum);
    
    // Set the inum to zero
    dir_entry->inum = 0;
    saveBlock(blockNum);
    
    return 0;
}

/**
 * This function creates a symbolic link from one file to another.
 * 
 * Inputs:
 *  oldname: a pointer to a string representing the path of the existing file to be linked.
 *  newname: a pointer to a string representing the path of the new file to be created as a symbolic link.
 *  currentInode: an integer representing the inode number of the current directory.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 * Notes:
 *  - Creates a new file as a symbolic link to the existing file specified by oldname.
 *  - The new file is created at the path specified by newname.
 *  - If the path specified by newname does not exist, it will be created.
 *  - If the path specified by newname already exists, it will be overwritten.
 *  - The symbolic link contains a string that represents the path of the existing 
 *    file specified by oldname.
 *  - The existing file specified by oldname is not modified or moved.
 */
int
yfsSymLink(char *oldname, char *newname, int currentInode) 
{
    
    if (newname[0] == '/') {
        newname += sizeof(char);
        currentInode = ROOTINODE;
    }
    
    if (oldname == NULL || newname == NULL || currentInode <= 0) {
        return ERROR;
    }
    int i;
    for (i = 0; i < MAXPATHNAMELEN; i++) {
        if (oldname[i] == '\0') {
            break;
        }
    }
    if (i == MAXPATHNAMELEN) {
        return ERROR;
    }
    
    for (i = 0; i < MAXPATHNAMELEN; i++) {
        if (newname[i] == '\0') {
            break;
        }
    }
    if (i == MAXPATHNAMELEN) {
        return ERROR;
    }
    
    // create a directory for newname
    char *filename;
    int dirInodeNum = getContainingDirectory(newname, currentInode, &filename);
    // Search all directory entries of that inode for the file name to create
    int blockNum;
    int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
    void *block = getBlock(blockNum);
    
    struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);
    
    // link that inode to newname
    int inodeNum = getNextFreeInodeNum();
    dir_entry->inum = inodeNum;
    memset(dir_entry->name, '\0', DIRNAMELEN);

    for (i = 0; filename[i] != '\0'; i++) {
        dir_entry->name[i] = filename[i];
    };
    saveBlock(blockNum);
    struct inode *inode = getInode(inodeNum);
    inode->type = INODE_SYMLINK;
    inode->size = sizeof(char) * strlen(oldname);
    inode->nlink = 1;
    inode->direct[0] = getNextFreeBlockNum();
    
    void *dataBlock = getBlock(inode->direct[0]);
    memcpy(dataBlock, oldname, strlen(oldname));
    
    saveBlock(inode->direct[0]);
    saveInode(inodeNum);
    return 0;
}

/**
 * This function reads the content of a symbolic link file and copies it into a buffer.
 * 
 * Inputs:
 *  pathname: a pointer to a string representing the path of the symbolic link file to be read.
 *  buf: a pointer to a buffer where the read data will be copied.
 *  len: an integer representing the maximum number of bytes to be read.
 *  currentInode: an integer representing the inode number of the current working directory.
 *  pid: an integer representing the process ID of the calling process.
 * 
 * Outputs: 
 *  Upon success, returns the number of bytes read. Otherwise, returns ERROR.
 * 
 * Notes:
 *  - The currentInode parameter may be updated if the pathname is an absolute path.
 *  - The numSymLinks variable may be updated to keep track of the number of symbolic links traversed.
 *  - The dataBlock parameter may be updated to point to the block of data containing the symbolic link content.
 *  - The buffer pointed to by buf may be modified to contain the symbolic link content.
 */
int 
yfsReadLink(char *pathname, char *buf, int len, int currentInode, int pid) 
{
    // Check for invalid inputs
    if (pathname == NULL || buf == NULL || len < 0 || currentInode <= 0) {
        return ERROR;
    }

    // Print debugging information
    TracePrintf(1, "read link for %s, len %d, at inode %d, from pid %d\n", pathname, len, currentInode, pid);
    
    // Update currentInode if the pathname is an absolute path
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
         currentInode = ROOTINODE;
    }
    
    // Initialize variables
    numSymLinks = 0;
    int symInodeNum = getPathInodeNumber(pathname, currentInode);
    if (symInodeNum == ERROR) {
        return ERROR;
    }
    struct inode *symInode = getInode(symInodeNum);
    
    int dataBlockNum = symInode->direct[0];
    char *dataBlock = (char *)getBlock(dataBlockNum);
    TracePrintf(1, "data block has string -> %s\n", dataBlock);
    
    // Calculate the number of characters to read
    int charsToRead = 0;
    while (charsToRead < len && dataBlock[charsToRead] != '\0') {
        charsToRead++;
    }
    TracePrintf(1, "copying %d bytes from pid %d\n", charsToRead, pid);
    
    // Copy the data to the buffer
    if (CopyTo(pid, buf, (char *)dataBlock, charsToRead) == ERROR) {
        // Error copying data
        TracePrintf(1, "error copying %d bytes from pid %d\n", charsToRead, pid);
        return ERROR;
    }

    return charsToRead;
}


/**
 * This function creates a new directory in the file system with the given pathname and
 * adds an entry for it in its parent directory.
 * 
 * Inputs:
 *   pathname: a pointer to a string representing the pathname of the directory to be created.
 *   currentInode: an integer representing the inode number of the current working directory.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
yfsMkDir(char *pathname, int currentInode) 
{
    
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
         currentInode = ROOTINODE;
    }
    char *filename;
    int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);
    // Search all directory entries of that inode for the file name to create
    int blockNum;
    int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
    void *block = getBlock(blockNum);
    
    struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);
    
    // return error if this directory already exists
    if (dir_entry->inum != 0) {
        return ERROR;
    }

    memset(&dir_entry->name, '\0', DIRNAMELEN);
    int i;
    for (i = 0; filename[i] != '\0'; i++) {
        dir_entry->name[i] = filename[i];
    }
    
    int inodeNum = getNextFreeInodeNum();
    dir_entry->inum = inodeNum;
    saveBlock(blockNum);
    block = getInodeBlockNum(inodeNum);
    
    struct inode *inode = getInode(inodeNum);
    inode->type = INODE_DIRECTORY;
    inode->size = 2 * sizeof (struct dir_entry);
    inode->nlink = 1;
    
    int firstDirectBlockNum = getNextFreeBlockNum();
    void *firstDirectBlock = getBlock(firstDirectBlockNum);
    inode->direct[0] = firstDirectBlockNum;
    
    struct dir_entry *dir1 = (struct dir_entry *)firstDirectBlock;
    dir1->inum = inodeNum;
    dir1->name[0] = '.';
    
    struct dir_entry *dir2 = (struct dir_entry *)((char *)dir1 + sizeof(struct dir_entry));
    dir2->inum = dirInodeNum;
    dir2->name[0] = '.';
    dir2->name[1] = '.';
    
    saveBlock(firstDirectBlockNum);
    
    saveInode(inodeNum);
    return 0;
}

/**
 * This function removes a directory specified by the given pathname.
 * 
 * Inputs:
 *  pathname: a null-terminated string representing the path of the directory to be removed.
 *  currentInode: an integer representing the inode number of the current working directory.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int
yfsRmDir(char *pathname, int currentInode) 
{
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
         currentInode = ROOTINODE;
    }
    numSymLinks = 0;
    int inodeNum = getPathInodeNumber(pathname, currentInode);
    if (inodeNum == ERROR) {
        return ERROR;
    }
    struct inode *inode = getInode(inodeNum);
    
    if (inode->size > (int)(2*sizeof(struct dir_entry))) {
        return ERROR;
    }
    
    clearFile(inode, inodeNum);
    addFreeInodeToList(inodeNum);
    
    
    char *filename;
    int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);

    int blockNum;
    int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
    void *block = getBlock(blockNum);

    // Get the directory entry associated with the path
    struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);
    
    // Set the inum to zero
    dir_entry->inum = 0;
    saveBlock(blockNum);
    return 0;
}

/**
 * This function changes the current working directory to the specified directory.
 * 
 * Inputs:
 *  pathname: a pointer to a string containing the path of the directory to change to.
 *  currentInode: an integer representing the inode number of the current working directory.
 * 
 * Outputs: 
 *  Upon success, returns the inode number of the specified directory. Otherwise, returns ERROR.
 * 
 */
int 
yfsChDir(char *pathname, int currentInode) 
{
    // check for valid inputs
    if (pathname == NULL || currentInode <= 0) {
        return ERROR;
    }

    // if the path is absolute, reset the currentInode to the root inode
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
        currentInode = ROOTINODE;
    }

    // get the inode number for the specified path
    numSymLinks = 0;
    int inode = getPathInodeNumber(pathname, currentInode);
    if (inode == 0) {
        return ERROR;
    }

    // return the inode number of the specified directory
    return inode;
}

/**
 * This function retrieves file status information and stores it in the given statbuf.
 * 
 * Inputs:
 *  pathname: a string representing the path of the file to retrieve information for.
 *  currentInode: an integer representing the inode number of the current directory.
 *  statbuf: a pointer to a struct Stat where the retrieved information will be stored.
 *  pid: an integer representing the process ID.
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 */
int 
yfsStat(char *pathname, int currentInode, struct Stat *statbuf, int pid) 
{
    // check for valid inputs
    if (pathname == NULL || currentInode <= 0 || statbuf == NULL) {
        return ERROR;
    }

    // if the path is absolute, reset the currentInode to the root inode
    if (pathname[0] == '/') {
        while (pathname[0] == '/')
            pathname += sizeof(char);
        currentInode = ROOTINODE;
    }

    // get the inode number for the specified path
    numSymLinks = 0;
    int inodeNum = getPathInodeNumber(pathname, currentInode);
    if (inodeNum == 0) {
        return ERROR;
    }

    // get the inode and populate the statbuf
    struct inode *inode = getInode(inodeNum);
    struct Stat stat;
    stat.inum = inodeNum;
    stat.nlink = inode->nlink;
    stat.size = inode->size;
    stat.type = inode->type;

    // copy the statbuf to the process's memory
    if (CopyTo(pid, statbuf, &stat, sizeof(struct Stat)) == ERROR) {
        TracePrintf(1, "error copying %d bytes to pid %d\n", sizeof(struct Stat), pid);
        return ERROR;
    }

    // return success
    return 0;
}

/**
 * This function synchronizes all dirty blocks and inodes with the disk.
 * 
 * Inputs: None.  
 * 
 * Outputs: 
 *  Upon success, returns 0. Otherwise, returns ERROR.
 * 
 * Notes:
 *  - Iterates over all the items in the cacheBlockQueue and cacheInodeQueue and
      writes back the dirty blocks and inodes respectively to the disk.
    - Uses WriteSector function to write back dirty blocks to disk.
    - Uses memcpy function to copy dirty inodes into their corresponding block.
 * 
 */
int
yfsSync(void) 
{
    TracePrintf(1, "About to sync all dirty blocks and inodes\n");
    // First sync all dirty blocks
    cacheItem *currBlockItem = cacheBlockQueue->firstItem;
    while (currBlockItem != NULL) {
        if (currBlockItem->dirty) {
            //write this block back to disk
            WriteSector(currBlockItem->number, currBlockItem->addr);
        }
        currBlockItem = currBlockItem->nextItem;
    }
    
    // Now sync all dirty inodes
    cacheItem *currInodeItem = cacheInodeQueue->firstItem;
    while (currInodeItem != NULL) {
        if (currInodeItem->dirty) {
            int inodeNum = currInodeItem->number;
            int blockNum = (inodeNum / INODESPERBLOCK) + 1;

            void *block = getBlock(blockNum);
            void *inodeAddrInBlock = (block + (inodeNum - (blockNum - 1) * INODESPERBLOCK) * INODESIZE);

            memcpy(inodeAddrInBlock, currInodeItem->addr, sizeof(struct inode));
            WriteSector(blockNum, block);
        }
        currInodeItem = currInodeItem->nextItem;
    }
    TracePrintf(1, "Done syncing\n");
    return 0;
 }

/**
 * This function syncs all dirty blocks and inodes and then shuts down the YFS file system server.
 * 
 * Inputs: None.  
 * 
 * Outputs: 
 *  Upon success, returns 0.
 * 
 * Notes:
 *  - Calls yfsSync() to sync all dirty blocks and inodes before shutting down.
 *  - Exits with status 0 to shutdown the server.
 * 
 */
int
yfsShutdown(void) 
{
    yfsSync();
    TracePrintf(1, "About to shutdown the YFS file system server...\n");
    Exit(0);
}

/**
 * This function checks whether the given path and directory entry name are equal.
 * 
 * Inputs:
 *  argc: an integer representing the number of arguments passed in.
 *  argv: an array of strings representing the arguments passed in
 * 
 * Outputs: 
 *  Upon success, returns 0.
 * 
 */
int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    init();

    if (argc > 1) {
        if (Fork() == 0) {
            Exec(argv[1], argv + 1);
        } else {
            for (;;) {
                processRequest(); 
            }
        }
    }
    
    return (0);
}