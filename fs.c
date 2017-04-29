
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

bool *map = 0;

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inodes[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int fs_format()
{
    // check for block bitmap
    if(map){
        printf("Error: disk is mounted.\n");
        return 0;
    }
    
	union fs_block block;

    // create empty block
    int i;
    for(i=0;i<DISK_BLOCK_SIZE;i++)
        block.data[i] = 0;
    
    // destroy data
    for(i=1;i<disk_size();i++)
	    disk_write(i,block.data);

    // write super block
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
    block.super.ninodeblocks = (disk_size()+9)/10;
    block.super.ninodes = block.super.ninodeblocks*INODES_PER_BLOCK;

    disk_write(0,block.data);
        
	return 1;
}

void fs_debug()
{
    if(!map){
        printf("Error: no mounted disk.\n");
        return;
    }

	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
    if( block.super.magic != FS_MAGIC ){
        printf("    magic number is invalid\n");
        return;
    }
    else
        printf("    magic number is valid\n");
	printf("    %d blocks on disk\n",block.super.nblocks);
	printf("    %d blocks for inodes\n",block.super.ninodeblocks);
	printf("    %d inodes total\n",block.super.ninodes);

    // inodes
    int i;
    int ninodeblocks = block.super.ninodeblocks;
    for(i=1; i <= ninodeblocks; i++){
    	disk_read(i,block.data);    
        int j;
        for(j=0; j < INODES_PER_BLOCK; j++)
            if(block.inodes[j].isvalid){
                printf("inode %d:\n", j);
                printf("    size %d bytes\n", block.inodes[j].size);

                // direct blocks
                printf("    direct blocks:");
                int k;
                for(k=0;k<POINTERS_PER_INODE;k++)
                    if(block.inodes[j].direct[k])
                        printf(" %d", block.inodes[j].direct[k]);
                printf("\n");

                // indirect block
                if(block.inodes[j].indirect){
	                union fs_block indirect;
	                disk_read(block.inodes[j].indirect,indirect.data);
                    printf("    indirect block: %d\n", block.inodes[j].indirect);
                    printf("    indirect data blocks:");

                    for(k=0;k<POINTERS_PER_BLOCK;k++)
                        if(indirect.pointers[k])
                            printf(" %d", indirect.pointers[k]);

                    printf("\n");
                }

            }
    }
}

int fs_mount()
{
	union fs_block block;
    if(map){
        printf("Error: disk already mounted\n");
        return 0;
    }

	disk_read(0,block.data);
    if(block.super.magic != FS_MAGIC){
        printf("Error: magic number is invalid\n");
    }
    else {
        // create zeroed bitmap
        map = malloc(sizeof(bool)*block.super.nblocks);
        int i;
        for(i=0;i<block.super.nblocks;i++)
            map[i] = 0;

        map[0] = 1;
        int ninodeblocks = block.super.ninodeblocks;
        // look through inodes
        for(i=1; i <= ninodeblocks; i++){
            map[i]=1;

        	disk_read(i,block.data);    
            int j;
            for(j=0; j < INODES_PER_BLOCK; j++)
                if(block.inodes[j].isvalid){
                    int k;
                    // mark pointed-to blocks as occupied
                    for(k=0;k<POINTERS_PER_INODE;k++)
                        if(block.inodes[j].direct[k])
                            map[block.inodes[j].direct[k]]=1;

                    // indirect block
                    if(block.inodes[j].indirect){
                        map[block.inodes[j].indirect]=1;

	                    union fs_block indirect;
	                    disk_read(block.inodes[j].indirect,indirect.data);
    
                        // mark occupied data blocks
                        for(k=0;k<POINTERS_PER_BLOCK;k++)
                            if(indirect.pointers[k])
                                map[indirect.pointers[k]]=1;
                    }
                }
       }    
    }

	return 1;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
