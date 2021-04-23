/*
 *  Copyright (C) 2021 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	struct superblock* s_block= malloc(sizeof(struct superblock));
	//might not work since it might try and read a full block into a superblock, might need to read full block then memcpy part instead 
	bio_read(0, (void *) s_block);
	bitmap_t inode_bitmap = malloc(sizeof(bitmap_t));
	if(s_block != NULL){
		//same thing with this. might need to make void buffer the size of a block and then just cast it to be a bitmap
		bio_read(s_block->i_bitmap_blk, (void*)inode_bitmap);
	}else{
		//something wrong with read
		return -1;
	}	
	// Step 2: Traverse inode bitmap to find an available slot
	int i, pos = -1;
	for(i = 0; i < MAX_INUM; i++){
		if(get_bitmap(inode_bitmap, i)==0){
			pos = i;
		}
	}
	if(pos == -1){
		//no free blocks
		return -1;
	}
	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(inode_bitmap, pos);
	bio_write(s_block->i_bitmap_blk, (const void *)inode_bitmap);
	return pos;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	//almost exact same as previous function. refer to its comments to understand

	// Step 1: Read inode bitmap from disk
	struct superblock* s_block= malloc(sizeof(struct superblock));
	bio_read(0, (void *) s_block);
	bitmap_t data_bitmap = malloc(sizeof(bitmap_t));
	if(s_block != NULL){
		bio_read(s_block->d_bitmap_blk, (void*)data_bitmap);
	}else{
		//something wrong with read
		return -1;
	}	
	// Step 2: Traverse inode bitmap to find an available slot
	int i, pos = -1;
	for(i = 0; i < MAX_DNUM; i++){
		if(get_bitmap(data_bitmap, i)==0){
			pos = i;
		}
	}
	if(pos == -1){
		//no free blocks
		return -1;
	}
	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(data_bitmap, pos);
	bio_write(s_block->d_bitmap_blk, (const void *)data_bitmap);
	return pos;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number
  int inodes_per_block = BLOCK_SIZE/sizeof(struct inode);
  int block_no = ino/inodes_per_block; //int division :)

  // Step 2: Get offset of the inode in the inode on-disk block
  int offset = ino%inodes_per_block;
  // Step 3: Read the block from disk and then copy into inode structure

  // might have to modify this line as noted in get_avail_ino()
  struct superblock* s_block= malloc(sizeof(struct superblock));
  bio_read(0, (void *) s_block);
  char* buffer = malloc(BLOCK_SIZE);
  //since inode blocks dont start at 0, need to get i_start_blk from the superblock and add that to the calculated block_no
  bio_read(block_no+s_block->i_start_blk, (void*)buffer);
  //copy inode from within the block
  memcpy((void*)inode, (void*) &buffer[sizeof(struct inode)*offset], sizeof(struct inode));
  return 0;
}

int writei(uint16_t ino, struct inode *inode) {

//similar to readi, but need to read block i_start_blk + blockno into mem, copy inode into correct offset, and then write the block back into memory.


  // Step 1: Get the inode's on-disk block number
  int inodes_per_block = BLOCK_SIZE/sizeof(struct inode);
  int block_no = ino/inodes_per_block;
  // Step 2: Get offset of the inode in the inode on-disk block
  int offset = ino%inodes_per_block;
  // Step 3: Write inode to disk
  struct superblock* s_block= malloc(sizeof(struct superblock));
  bio_read(0, (void *) s_block);
  char* buffer = malloc(BLOCK_SIZE);
  bio_read(block_no+s_block->i_start_blk, (void*)buffer);
  memcpy((void*) &buffer[sizeof(struct inode)*offset], (void*) inode, sizeof(struct inode));
  bio_write(block_no+s_block->i_start_blk, (const void*) buffer);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  struct inode *ino_temp = malloc(sizeof(struct inode));
  readi(ino, ino_temp);
  int num_entries = ino_temp->size/sizeof(dirent);
  // Step 2: Get data block of current directory from inode
  // may have mutiple data blocks but will deal with that later
  int block_no =  ino_temp->direct_ptr[0];
  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure
  char * buffer = malloc(BLOCK_SIZE);
  bio_read(0, (void*) buffer);
  struct superblock* super_block = malloc(sizeof(struct superblock));
  memcpy(super_block, buffer, sizeof(struct superblock));
  bio_read(super_block->i_start_blk+block_no, (void*)buffer);
  int i;
  for(i = 0; i < num_entries; i++){
	memcpy(dirent, &buffer[i*sizeof(struct dirent)], sizeof(struct dirent));
	if(dirent->valid == 1){
		if(strcmp(fname, dirent->name)==0){
			//success
			return 1;
		}
	}
  }
  
	//failure
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile

	// write superblock information

	// initialize inode bitmap

	// initialize data block bitmap

	// update bitmap information for root directory

	// update inode for root directory

	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}

