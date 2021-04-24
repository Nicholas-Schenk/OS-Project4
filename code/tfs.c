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

#define _DIRECTORY_ 0
#define _FILE_ 1

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
int disk_file = -1;
struct superblock* s_block;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bitmap_t inode_bitmap = malloc(sizeof(char)*MAX_INUM/8);
	void* buffer = malloc(BLOCK_SIZE);
	if(s_block != NULL){
		bio_read(s_block->i_bitmap_blk, buffer);
		memcpy(inode_bitmap, buffer, s_block->max_inum/8);
	}else{
		//superblock not allocated somehow
		return -1;
	}	
	// Step 2: Traverse inode bitmap to find an available slot
	int i, pos = -1;
	for(i = 0; i < s_block->max_inum; i++){
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
	bitmap_t data_bitmap = malloc(MAX_DNUM/8);
	void* buffer = malloc(BLOCK_SIZE);
	if(s_block != NULL){
		bio_read(s_block->d_bitmap_blk, buffer);
		memcpy(data_bitmap, buffer, s_block->max_dnum/8);
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
  bio_read(s_block->i_start_blk+block_no, (void*)buffer);
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
	int num_blocks = dir_inode.size/BLOCK_SIZE;
	int entries_per_block = BLOCK_SIZE/sizeof(struct dirent);
	if(dir_inode.size%BLOCK_SIZE != 0){
		num_blocks++;
	}
	int num_entries = dir_inode.size/sizeof(struct dirent);
	int i, j, count=0;
	for(i = 0; i < num_blocks;i++){
		char * block = malloc(BLOCK_SIZE);
		//bio_read();
		for(j=0; j < entries_per_block;j++){
			count++;
			if(count > num_entries){
				break;
			}
			struct dirent* temp = malloc(sizeof(struct dirent));
			/*memcpy(temp, );
			if(){
			
			}*/
				
		}
	}
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
	dev_init(diskfile_path);
	// write superblock information
	char * buffer = malloc(BLOCK_SIZE);
	s_block = malloc(sizeof(s_block));
	s_block->magic_num = MAGIC_NUM;
	s_block->max_inum = MAX_INUM;
	s_block->max_dnum = MAX_DNUM;
	s_block->i_bitmap_blk = 1;
	s_block->d_bitmap_blk = 2;
	s_block->i_start_blk = 3;
	int inode_blocks = BLOCK_SIZE/sizeof(struct inode);
	if(BLOCK_SIZE%(sizeof(struct inode))!= 0){
		inode_blocks++;
	}
	s_block->d_start_blk =inode_blocks+3;
	memcpy(buffer, s_block, sizeof(struct superblock));
	bio_write(0, (const void*)buffer);
	free(buffer);
	char* inode_bitmap_block = malloc(BLOCK_SIZE);
	char* data_bitmap_block = malloc(BLOCK_SIZE);
	// initialize inode bitmap
	bitmap_t inode_bitmap = malloc(MAX_INUM/8);
	memset(inode_bitmap, 0, MAX_INUM/8);
	// initialize data block bitmap
	bitmap_t data_bitmap = malloc(MAX_DNUM/8);
	memset(data_bitmap, 0, MAX_DNUM/8);
	// update bitmap information for root directory
	set_bitmap(inode_bitmap, 0);
	memcpy(inode_bitmap_block, inode_bitmap, MAX_INUM/8);
	memcpy(data_bitmap_block, data_bitmap, MAX_INUM/8);

	bio_write(1, inode_bitmap_block);
	bio_write(2, data_bitmap_block);

	free(inode_bitmap_block);
	free(data_bitmap_block);

	// update inode for root directory
	buffer = malloc(BLOCK_SIZE);
	bio_read(3, buffer);
	struct inode * temp_inode = malloc(sizeof(struct inode));
	memcpy(temp_inode, buffer, sizeof(struct inode));

	temp_inode->ino = 0;
	temp_inode->valid = 1;
	temp_inode->size = 0;
	temp_inode->type = _DIRECTORY_;
	temp_inode->link = 1;
	//need to set stat still

	memcpy(buffer, temp_inode, sizeof(struct inode));
	bio_write(3, buffer);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	printf("WE'RE INITIALIZING????\n");
	disk_file = dev_open(diskfile_path);
	if(disk_file < 0){
		tfs_mkfs();
		disk_file = dev_open(diskfile_path);
	}
  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(s_block);
	// Step 2: Close diskfile
	printf("closing?");
	dev_close(diskfile_path);

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

