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
			break;
		}else{
			printf("this was set to 1: %d\n", i);
			
		}
	}
	if(pos == -1){
		//no free blocks
		return -1;
	}
	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(inode_bitmap, pos);
	memcpy(buffer, inode_bitmap, sizeof(char)*MAX_INUM/8);
	bio_write(s_block->i_bitmap_blk, buffer);
	free(buffer);
	free(inode_bitmap);
	return pos;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	//almost exact same as previous function. refer to its comments to understand

	// Step 1: Read inode bitmap from disk
	bitmap_t data_bitmap = malloc(sizeof(char)*MAX_DNUM/8);
	void* buffer = malloc(BLOCK_SIZE);
	if(s_block != NULL){
		bio_read(s_block->d_bitmap_blk, buffer);
		memcpy(data_bitmap, buffer, s_block->max_dnum/8);
	}else{
		//something wrong with read
		free(buffer);
		free(data_bitmap);
		return -1;
	}	
	// Step 2: Traverse inode bitmap to find an available slot
	int i, pos = -1;
	for(i = 0; i < MAX_DNUM; i++){
		if(get_bitmap(data_bitmap, i)==0){
			pos = i;
			break;
		}
	}
	if(pos == -1){
		//no free blocks
		free(buffer);
		free(data_bitmap);
		return -1;
	}
	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(data_bitmap, pos);
	memcpy(buffer, data_bitmap, sizeof(char)*MAX_DNUM/8);
	bio_write(s_block->d_bitmap_blk,buffer);
	free(buffer);
	free(data_bitmap);
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

  printf("block_no: %d  offset: %d  s_block->i_start_blk: %d\n", block_no, offset, s_block->i_start_blk);


  //copy inode from within the block
  memcpy((void*)inode, (void*) &buffer[sizeof(struct inode)*offset], sizeof(struct inode));
  printf("SIZE for inode %d: %d\n", inode->ino,inode->size);
  free(buffer);
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
  free(buffer);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
  struct dirent * temp_dirent = malloc(sizeof(struct dirent));
  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  struct inode *ino_temp = malloc(sizeof(struct inode));
  readi(ino, ino_temp);
  int num_entries = ino_temp->size/sizeof(struct dirent);
  printf("num entries in %d :%d\n", ino, num_entries);
  // Step 2: Get data block of current directory from inode
  // may have mutiple data blocks but will deal with that later
  int block_no =  ino_temp->direct_ptr[0];
  printf("block_no: %d\n", block_no);
  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure
  char * buffer = malloc(BLOCK_SIZE);
  bio_read(s_block->d_start_blk+block_no, (void*)buffer);
  int i;
  for(i = 0; i < num_entries; i++){
	printf("DIR FIND ITERATION: %d\n", i);	
  	memcpy(temp_dirent, &buffer[i*sizeof(struct dirent)], sizeof(struct dirent));
	if(temp_dirent->valid == 1){
		printf("ENTRY WAS VALID\n");
		if(strcmp(fname, temp_dirent->name)==0){
			printf("FOUND IT\n");
			memcpy(dirent, temp_dirent, sizeof(struct dirent));
			free(buffer);
			free(temp_dirent);
			return 1;
		}else{
			printf("DIDN'T FIND IT: temp_dirent->name: %s which is not the same as: %s\n", temp_dirent->name, fname);
		}
	}else{
		printf("ENTRY WAS INVALID\n");
	}
  }
  
	//failure
	free(buffer);
	free(temp_dirent);
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode

	printf("-------WE ARE IN DIR ADD-------\n");

	// Step 2: Check if fname (directory name) is already used in other entries
	int num_blocks = dir_inode.size/BLOCK_SIZE;
	//printf("yo!\n");
	int entries_per_block = BLOCK_SIZE/sizeof(struct dirent);
	if(dir_inode.size%BLOCK_SIZE != 0){
		num_blocks++;
	}
	//printf("num_blocks: %d\n", num_blocks);
	int num_entries = dir_inode.size/sizeof(struct dirent);
	int i, j;
	for(i = 0; i < num_blocks;i++){
		printf("OUTER ITERATION OF SEARCH %d\n", i);
		char * block = malloc(BLOCK_SIZE);
		bio_read(dir_inode.direct_ptr[i]+s_block->d_start_blk, block);
		for(j=0; j < entries_per_block;j++){	
			printf("INNER ITERATION OF SEARCH %d\n", j);
			struct dirent* temp = malloc(sizeof(struct dirent));
			memcpy(temp, &block[j*sizeof(struct dirent)], sizeof(struct dirent) );
			if(temp->valid == 1){
				printf("ENTRY WAS VALID\n");
				if(temp->len == name_len && strcmp(fname, temp->name)==0){
					printf("DUPLICATE FILE\n");
					return -1;
				}else{
					printf("NOT A DUPLICATE\n");
				}
			}else{
				printf("ENTRY WAS INVALID\n");
			}
			free(temp);
				
		}
		free(block);
	}
	printf("**SEARCH DONE, WAS NOT A DUPLICATE!**\n");
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	int j_pos = -1, i_pos=-1;
	for(i = 0; i < num_blocks;i++){
		char * block = malloc(BLOCK_SIZE);
		bio_read(dir_inode.direct_ptr[i]+s_block->d_start_blk, block);
		for(j=0; j < entries_per_block;j++){
			struct dirent* temp = malloc(sizeof(struct dirent));
			memcpy(temp, &block[j*sizeof(struct dirent)], sizeof(struct dirent) );
			if(temp->valid == 0){
				j_pos = j;
				i_pos = i;
				free(temp);
				break;
			}
			free(temp);
				
		}
		if(j_pos > 0){
			break;
		}
		free(block);
	}
	//printf("\n");
	// Allocate a new data block for this directory if it does not exist
	if(j_pos < 0){
		//find empty data block and allocate it 
		char * block = malloc(BLOCK_SIZE);
		bio_read(s_block->d_bitmap_blk, block);
		printf("NO BLOCKS AVAILABLE??? NEED TO ADD A NEW ONE??\n");
		int i, j;
		for(i =0; i < MAX_DNUM; i++){
			if(get_bitmap((bitmap_t) block, i)==0){
				//block is invalid, aka free to be given to inode
				printf("found an empty block\n");
				set_bitmap((bitmap_t) block, i);
				bio_write(s_block->d_bitmap_blk, block);
				dir_inode.size+=BLOCK_SIZE;
				dir_inode.direct_ptr[dir_inode.size/BLOCK_SIZE] = i;
				free(block);
				char * new_data_block = malloc(BLOCK_SIZE);
				struct dirent temp;
				temp.valid = 0;
				//fill block with dirents
				for(j =0; j < BLOCK_SIZE/sizeof(struct dirent);j++){
					memcpy(&new_data_block[j*sizeof(struct dirent)], &temp, sizeof(struct dirent)); 
				}
				bio_write(i+s_block->d_start_blk, new_data_block);
				i_pos = i;
				j_pos = 0;
				break;	

			}else{
				printf("%d \n", i);
			}
		}
		free(block);
	}

	printf("ENTERED IN DIRENT BLOCK: %d POSITION %dyo\n", i_pos, j_pos);
	// Update directory inode
	char* block = malloc(BLOCK_SIZE);
	bio_read(dir_inode.direct_ptr[i_pos]+s_block->d_start_blk, block);
	struct dirent *temp = malloc(sizeof(struct dirent));
	memcpy(temp, &block[j_pos*sizeof(struct dirent)], sizeof(struct dirent));
	//char temp_name[208];
	memcpy(temp->name, fname, name_len);
	temp->name[name_len] = '\0';
	printf("NEW NAME: %s+ length: %ld\n", temp->name, name_len);
	temp->valid = 1;
	temp->ino = f_ino;
	temp->len = name_len;


	memcpy(&block[j_pos*sizeof(struct dirent)], temp, sizeof(struct dirent));
	
	free(temp);
	temp = malloc(sizeof(struct dirent));
	// Write directory entry
	bio_write(dir_inode.direct_ptr[i_pos]+s_block->d_start_blk, block);
	bio_read(dir_inode.direct_ptr[i_pos]+s_block->d_start_blk, block);
	memcpy(temp, &block[j_pos*sizeof(struct dirent)], sizeof(struct dirent));
	free(temp);
	free(block);
	printf("************temp-> valid: %d   temp->ino: %d temp->name %s********\n", temp->valid, temp->ino, temp->name);
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	int num_blocks = dir_inode.size/BLOCK_SIZE;
	printf("dir_inode's size is %d\n", dir_inode.size);
	int i, j;
	for(i=0; i < num_blocks; i++){
		char * buffer = malloc(BLOCK_SIZE);
		bio_read(dir_inode.direct_ptr[i]+s_block->d_start_blk, buffer );
		for(j=0; j < BLOCK_SIZE/sizeof(struct dirent);j++){
			struct dirent* cur_dirent= malloc(sizeof(struct dirent));
			memcpy(cur_dirent, &buffer[j*sizeof(struct dirent)], sizeof(struct dirent));
			if(strcmp(fname, cur_dirent->name) == 0 && name_len == cur_dirent->len){
				cur_dirent->valid = 0;
				memcpy(&buffer[j*sizeof(struct dirent)], cur_dirent, sizeof(struct dirent));
				bio_write(dir_inode.direct_ptr[i]+s_block->d_start_blk, buffer);
				return 0; 	
			}else{
				printf("searching for: %s found: %s search length:%ld found length:%d\n", fname, cur_dirent->name, name_len, cur_dirent->len);

			}
			free(cur_dirent);
			
		}
		free(buffer);
	}
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return -1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way


	char* token = strtok((char*)path, "/");
	int dir_inode = ino; //root directory should always be passed (0)
	struct dirent * dirent = malloc(sizeof(struct dirent));
	while(token != NULL){
	//	printf("token: --%s--: directory: %d\n", token, dir_inode);
		readi(dir_inode, inode);
		if(inode->type == _FILE_){
			token=strtok(NULL, "/");
			if(token ==NULL){
				printf("returned the inode\n");
				return inode->ino;
			}else{
				printf("token not null: %s\n", token);
				return -1;
			}
		}


		
		if(inode->valid == 1){
	//		printf("SUPER BLOCK TEST: %d\n",s_block->i_start_blk);	
			int ret = dir_find(dir_inode, token, strlen(token), dirent);
			if(ret == -1){
	//			printf("SUPER BLOCK TEST: %d\n",s_block->i_start_blk);
				printf("dir_find failed\n");
				return -1;
			}else{
				dir_inode = dirent->ino;
				//token=strtok(NULL, "/");
			}
		}else{
			printf("inode with number %d wasnt valid\n", inode->ino);
			return -1;
		}

		
	}
	

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

	int z;
	for(z = 0; z < MAX_INUM; z++){
		if(get_bitmap(inode_bitmap, z)==1){
			printf("%d something went wrong\n", z );
		}

	}
	// initialize data block bitmap
	bitmap_t data_bitmap = malloc(MAX_DNUM/8);
	memset(data_bitmap, 0, MAX_DNUM/8);
	// update bitmap information for root directory	
	for(z = 0; z < MAX_DNUM; z++){
		if(get_bitmap((bitmap_t)data_bitmap, z)==1){
			printf("%d ---something went wrong\n", z );
		}

	}

	printf("anything wrong?\n");


	set_bitmap(inode_bitmap, 0);
	set_bitmap(data_bitmap, 0);
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
	temp_inode->size = BLOCK_SIZE;
	temp_inode->type = _DIRECTORY_;
	temp_inode->link = 1;
	//need to set stat still, also set up data block
	temp_inode->direct_ptr[0] = 0;
	int l;
	for(l=1;l<16;l++){
		temp_inode->direct_ptr[l] = -1;
	}
	char *buffer2 = malloc(BLOCK_SIZE);
	bio_read(s_block->d_start_blk, buffer2);
	int i;
	struct dirent* temp_dirent = malloc(sizeof(struct dirent));
	temp_dirent->valid = 0;
	for(i = 0; i < BLOCK_SIZE/sizeof(struct dirent); i++){
		memcpy(&buffer[i*sizeof(struct dirent)], temp_dirent, sizeof(struct dirent));
	}
	free(temp_dirent);
	bio_write(s_block->d_start_blk, buffer2);
	free(buffer2);

	memcpy(buffer, temp_inode, sizeof(struct inode));
	bio_write(3, buffer);
	free(buffer);


	buffer = malloc(BLOCK_SIZE);
	//update inode bitmap block
	bio_read(s_block->i_bitmap_blk, buffer);
	set_bitmap((bitmap_t)buffer, 0);
	bio_write(s_block->i_bitmap_blk, buffer);


	bio_read(s_block->d_bitmap_blk, buffer);
	set_bitmap((bitmap_t)buffer, 0);
	bio_write(s_block->d_bitmap_blk, buffer);
	free(buffer);


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
		printf("SUPERBLOCK i start blk: %d\n", s_block->i_start_blk);
	    	
	}else{
		s_block = malloc(sizeof(struct superblock));
		char* buffer = malloc(BLOCK_SIZE);
		bio_read(0, buffer);
		memcpy(s_block, buffer, sizeof(struct superblock));
		free(buffer);
	}
  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	if(s_block != NULL){free(s_block);}
	// Step 2: Close diskfile
	printf("closing?");
	dev_close(diskfile_path);

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	printf("testing get node by path at least: %s\n", path);

	// Step 1: call get_node_by_path() to get inode from path
	struct inode* temp_inode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, temp_inode);
	if(ret ==-1){
		printf("failure: super block test: %d\n", s_block->i_start_blk);
		return -ENOENT;
	}
	readi(ret, temp_inode);
	// Step 2: fill attribute of file into stbuf from inode
	if(temp_inode->type == _FILE_){
		stbuf->st_mode   = S_IFREG | 0777;
	}else if(temp_inode->type == _DIRECTORY_){
		//stbuf->st_nlink  = 2;
		stbuf->st_mode = S_IFDIR | 0755;
	}
		time(&stbuf->st_mtime);
		stbuf->st_nlink  = 2;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();
		stbuf->st_ino = temp_inode->ino;
		stbuf->st_size = temp_inode->size;
		//stbuf->st_blocks = temp_inode->size/BLOCK_SIZE;
	char * temp__ = malloc(sizeof(char));
	free(temp__);
	free(temp_inode);
	printf("still ok after getattr\n");	

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

    struct inode inode;
    return get_node_by_path(path, 0, &inode);

}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode *inode = malloc(sizeof (struct inode));

    int exists = get_node_by_path(path, 0, inode);
    if (exists == -1) return -1;
    printf("does exist\n");
    readi(exists, inode);
    // Step 2: Read directory entries from its data blocks, and copy them to filler
	for (int i = 0; i < 16; i++) {
		if (inode->direct_ptr[i] > -1) {
			printf("block to read from on iteration %d: %d\n", i, inode->direct_ptr[i]);
			struct dirent* dirent = (struct dirent*) malloc(sizeof(struct dirent));
			char* buffer = malloc(BLOCK_SIZE);
			bio_read(s_block->d_start_blk + inode->direct_ptr[i], buffer);
			memcpy(dirent, buffer, sizeof(struct dirent));
			filler(buffer, dirent->name, NULL, 0);
		}
	}

    return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {
    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char* parent_path;
    char* target_path;
    parent_path = strdup(path);
    target_path = strdup(path);
    char* parent_name = dirname(parent_path);
    char* target_name = basename(target_path);
    printf("parent name: %s\n", parent_path);
    printf("target name: %s\n", target_name);
    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    int exists = get_node_by_path(parent_name, 0, &parent_inode);
	printf("EXISTS??: %d\n", exists);
    if (exists == -1) {
		printf("ERROR: parent directory does not exist\n");
		return -1;
	}
    // Step 3: Call get_avail_ino() to get an available inode number
    int target_ino = get_avail_ino();

    // Step 4: Call dir_add() to add directory entry of target directory to parent directory
    struct inode target_inode;
	dir_add(parent_inode, target_ino, target_name, strlen(target_name));

    // Step 5: Update inode for target directory
    // struct dirent target_dirent;
    // target_dirent.ino = target_ino;
    // target_dirent.valid = 0;
    // memcpy(target_dirent.name, target_name, strlen(target_name));
    // target_dirent.len = strlen(target_name);

    target_inode.ino = target_ino;
    target_inode.valid = 1;
    target_inode.size = 0;
    target_inode.type = _DIRECTORY_;
    target_inode.link = 2;
    int i;
    for (i = 0; i < 16; i++) {
        target_inode.direct_ptr[i] = -1;
    }

    // Step 6: Call writei() to write inode to disk
    writei(target_ino, &target_inode);
	printf("FINISHED MKDIR\n");

    return 0;
}

static int tfs_rmdir(const char *path) {

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char* parent_path;
    char* target_path;
    parent_path = strdup(path);
    target_path = strdup(path);
    char* parent_name = dirname(parent_path);
    char* target_name = basename(target_path);

    // Step 2: Call get_node_by_path() to get inode of target directory
    struct inode target_inode;
    get_node_by_path(target_name, 0, &target_inode);

    // Step 3: Clear data block bitmap of target directory
    bitmap_t data_bitmap = malloc(MAX_DNUM/8);
    void* data_buffer = malloc(BLOCK_SIZE);
    bio_read(s_block->d_bitmap_blk, data_buffer);
    memcpy(data_bitmap, data_buffer, s_block->max_dnum/8);
    unset_bitmap(data_bitmap, target_inode.direct_ptr[0]);
    

    // Step 4: Clear inode bitmap and its data block
    bitmap_t inode_bitmap = malloc(sizeof(char)*MAX_INUM/8);
    void* inode_buffer = malloc(BLOCK_SIZE);
    bio_read(s_block->i_bitmap_blk, inode_buffer);
    memcpy(inode_bitmap, inode_buffer, s_block->max_inum/8);
    unset_bitmap(inode_bitmap, target_inode.ino);
    bio_write(s_block->i_bitmap_blk, (const void *)inode_bitmap);


    // Step 5: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    int exists = get_node_by_path(parent_name, 0, &parent_inode);
    if (exists == -1) return -1;

    // Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
    dir_remove(parent_inode, target_name, strlen(target_name));
    free(inode_buffer);
    return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	//char* dir = malloc(sizeof(char)*10);
	char *dir, *base, *dir_name, *base_name;
	//dirname and basename modify their arguments so need to duplicate string
	printf("-------duplicating path %s------------\n", path);
	dir = strdup(path);
	//base = strdup(path);
	//dir = malloc(sizeof(char)*10);
	printf("the first duplicate worked\n");
	memcpy(dir, path, strlen(path));
	printf("the first duplicate worked\n");
	dir[strlen(path)] = '\0';
	printf("the first duplicate worked\n");
	base = strdup(path);
	dir_name = dirname(dir);
	printf("dir_name: %s\n", dir_name);
	base_name = basename(base);
	printf("base_name: %s\n", base_name);
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode* inode = malloc(sizeof(struct inode)); 
	printf("-------------getting inode by path--------\n");
	int dir_ino = get_node_by_path(dir_name, 0, inode);
	printf("inode is %d\n", dir_ino);
	if(dir_ino <0){
		printf("didn't find it\n");
		return -1;
	}else{
		readi(0, inode);
	}
	// Step 3: Call get_avail_ino() to get an available inode number
	printf("------------getting available ino:------------\n");
	int avail_ino = get_avail_ino();
	printf("first available inode was %d\n", avail_ino);
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	struct inode *new_node = malloc(sizeof(inode));
	printf("-----------calling dir_add---------\n");
	int found = dir_add(*inode, avail_ino, base_name, strlen(base_name));
	if(found <0){
		printf("can't add a duplicate file!!!\n");
		return -ENOENT;
	}
	// Step 5: Update inode for target file
	new_node->ino = avail_ino;
	new_node->valid = 1;
	new_node->size = 4096;
	new_node->type = _FILE_;
	new_node->link = 2;
	int i;
	for(i = 0; i < 16;i++){
		new_node->direct_ptr[i] = -1;
	}
	int avail_block = get_avail_blkno();
	char* buffer = malloc(BLOCK_SIZE);
	bio_read(s_block->i_bitmap_blk, buffer);
	set_bitmap((bitmap_t)buffer, avail_ino);
	bio_read(s_block->d_bitmap_blk, buffer);
	set_bitmap((bitmap_t)buffer, avail_block);
	new_node->direct_ptr[0] = get_avail_blkno();
	// need to update vstat too
	// Step 6: Call writei() to write inode to disk
	printf("------------INODE CHECKS----------\n");
	writei(avail_ino, new_node);
	free(new_node);
	new_node = malloc(sizeof(struct inode));
	readi(avail_ino, new_node);
	printf("Inode should be %d. it is %d\n", avail_ino, new_node->ino);
	printf("This inode should be valid. its validity is: %d\n", new_node->valid);
	printf("This inode should be have a size of 4096. it is %d\n", new_node->size);
	printf("This inode should have a link count of 2. it is %d\n", new_node->link);
	printf("This inode should only have one valid direct ptr. they are: ");
	int z;
	for(z = 0; z < 16; z++){
		printf("ptr %d: %d\n", z, new_node->direct_ptr[z]);
	}
	free(buffer);
	free(new_node);
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* temp_inode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, temp_inode);
	free(temp_inode);
	if(ret ==-1){
		printf("failure: couldn't find inode\n");
		return ret;
	}
	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	struct inode* temp_inode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, temp_inode);
	if(ret ==-1){
		free(temp_inode);
		printf("failure: couldn't find inode\n");
		return ret;
	}
	free(temp_inode);
	// Step 2: Based on size and offset, read its data blocks from disk
	int start_block = offset/BLOCK_SIZE;
	int num_blocks = size/BLOCK_SIZE;
	if((size)%BLOCK_SIZE!=0){
		num_blocks++;
	}
	printf("size: %ld, offset: %ld \n NUMBER OF BLOCKS BEING READ: %d STARTING AT: %d\n", size, offset, num_blocks, start_block);
	int end_block = num_blocks+start_block;
	int i, bytes_read = 0;
	// Step 3: copy the correct amount of data from offset to buffer
	for(i = start_block; i < end_block;i++){
		char* temp_buffer = malloc(BLOCK_SIZE);
		bio_read(s_block->d_start_blk+temp_inode->direct_ptr[i], temp_buffer);
		if(i == start_block){
			memcpy(buffer,temp_buffer+(offset%BLOCK_SIZE), BLOCK_SIZE-(offset%BLOCK_SIZE));
		       	bytes_read += BLOCK_SIZE-(offset%BLOCK_SIZE);	
		}else if(size-bytes_read>=BLOCK_SIZE){
			memcpy(buffer+bytes_read, temp_buffer, BLOCK_SIZE);
			bytes_read+=BLOCK_SIZE;
		}else{
			memcpy(buffer+bytes_read, temp_buffer, size-bytes_read);
			bytes_read+=(size-bytes_read);
		}
		free(temp_buffer);
	}

	// Note: this function should return the amount of bytes you copied to buffer
	return bytes_read;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* temp_inode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, temp_inode);
	if(ret ==-1){
		free(temp_inode);
		printf("failure: couldn't find inode\n");
		return ret;
	}
	// Step 2: Based on size and offset, read its data blocks from disk
	//char * temp_read_buffer = malloc(size);
	int start_block = offset/BLOCK_SIZE;
	int num_blocks = temp_inode->size/BLOCK_SIZE;
	if((temp_inode->size)%BLOCK_SIZE!=0){
		num_blocks++;
	}
	int i, bytes_written = 0;
	for(i = start_block; i < num_blocks;i++){
		char* temp_buffer = malloc(BLOCK_SIZE);
		bio_read(s_block->d_start_blk+temp_inode->direct_ptr[i], temp_buffer);
		if(i == start_block){
			memcpy(temp_buffer+(offset%BLOCK_SIZE), buffer,BLOCK_SIZE-(offset%BLOCK_SIZE));
			bio_write(s_block->d_start_blk+temp_inode->direct_ptr[i], temp_buffer);
		       	bytes_written += BLOCK_SIZE-(offset%BLOCK_SIZE);	
		}else if(size-bytes_written>=BLOCK_SIZE){
			memcpy(temp_buffer+bytes_written, buffer, BLOCK_SIZE);
			bytes_written+=BLOCK_SIZE;
			bio_write(s_block->d_start_blk+temp_inode->direct_ptr[i], temp_buffer);
		}else{
			memcpy(temp_buffer+bytes_written, buffer, size-bytes_written);
			bytes_written+=(size-bytes_written);
			bio_write(s_block->d_start_blk+temp_inode->direct_ptr[i], temp_buffer);
		}
		free(temp_buffer);
	}

	if(bytes_written < size){ // need to give more blocks
		int num_new_blocks = (size-bytes_written)/BLOCK_SIZE;
		if((size-bytes_written)%BLOCK_SIZE!=0){
			num_new_blocks++;
		}
		for(i=0; i < num_new_blocks;i++){
			if(i+num_blocks > 16){
				return bytes_written;
			}else{
				int new_block = get_avail_blkno();
				temp_inode->direct_ptr[i+num_blocks] = new_block;
				if(size-bytes_written >= BLOCK_SIZE ){
					bio_write(s_block->d_start_blk+temp_inode->direct_ptr[i+num_blocks], buffer+bytes_written);
					bytes_written+=BLOCK_SIZE;
				}else{
					char* temp_buffer = malloc(BLOCK_SIZE);
					memcpy(temp_buffer, buffer+bytes_written, size-bytes_written);
					bio_write(s_block->d_start_blk+temp_inode->direct_ptr[i+num_blocks], temp_buffer);
					bytes_written+=(size-bytes_written);	
					free(temp_buffer);
				}

			}

		}


	}
	
	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk
	temp_inode->size = offset+bytes_written;
	writei(temp_inode->ino, temp_inode);

	// Note: this function should return the amount of bytes you write to disk
	return bytes_written;
}

static int tfs_unlink(const char *path) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* dir, *base, *dir_name, *base_name;
	//dirname and basename modify their arguments so need to duplicate string
	printf("-------UNLINK START------------\n");
	dir = strdup(path);
	base = strdup(path);
	dir_name = dirname(dir);
	printf("dir_name: %s\n", dir_name);
	base_name = basename(base);
	printf("base_name: %s\n", base_name);
	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode *inode = malloc(sizeof(struct inode));
	int node_num = get_node_by_path(path, 0, inode);
	readi(node_num , inode);
	printf("get node by path is fine.\n");
	if(node_num < 0){
		return -1;
	}
	// Step 3: Clear data block bitmap of target file
	char*buffer = malloc(BLOCK_SIZE);
	bio_read(s_block->d_bitmap_blk, buffer);
	int i;
	for(i=0;i < 16; i++){
		printf("iteration: %d --- %d\n", i, inode->direct_ptr[i]);
		if(inode->direct_ptr[i] != -1){
			unset_bitmap((bitmap_t)buffer, inode->direct_ptr[i]);
		}
	}
	printf("unsetting data bitmap worked\n");
	bio_write(s_block->d_bitmap_blk, buffer);
	free(buffer);
	// Step 4: Clear inode bitmap and its data block
	buffer = malloc(BLOCK_SIZE);
	bio_read(s_block->i_bitmap_blk, buffer);
	unset_bitmap((bitmap_t)buffer, node_num);
	bio_write(s_block->i_bitmap_blk, buffer);
	printf("unsetting inode bitmap worked\n");

	inode->valid = 0;
	for(i = 0; i < 16; i++){
		inode->direct_ptr[i] = -1;
	}


	writei(node_num, inode);
	printf("freed inode successfully\n");
	free(inode);
	inode = malloc(sizeof(struct inode));
	// Step 5: Call get_node_by_path() to get inode of parent directory
	int parent_node_num = get_node_by_path(dir_name, 0, inode);
	readi(parent_node_num, inode);
	printf("PARENT NODE NUM:%d, %d, size: %d\n", parent_node_num, inode->ino, inode->size);
	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	printf("found parent\n");
	if(dir_remove(*inode, base_name, strlen(base_name))==-1){
		printf("DID NOT ACTUALLY WORK!!!!!");	
	}	
	free(buffer);
	printf("-------UNLINK END------------\n");
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

