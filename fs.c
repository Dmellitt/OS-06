
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

void inode_load( int inumber, struct fs_inode *inode ) 
{ 
	union fs_block block;

    // get block of inodes, add 1 to skip superblock
	disk_read(inumber/INODES_PER_BLOCK+1, block.data); 

    
    // set pointer to requested inode
    *inode = block.inodes[inumber%INODES_PER_BLOCK];
}

void inode_save( int inumber, struct fs_inode *inode )
{
	union fs_block block;
    
    // get block of inodes, add 1 to skip superblock
	disk_read(inumber/INODES_PER_BLOCK+1, block.data); 

    // set requested inode to given inode
    block.inodes[inumber%INODES_PER_BLOCK] = *inode;

    // update block
	disk_write(inumber/INODES_PER_BLOCK+1, block.data); 
}

int valid_inumber( int inumber ) 
{
	union fs_block block;

	disk_read(0,block.data); 

    if( inumber >= block.super.ninodes )
        return 0;

    return 1;
}

int free_block(){
	union fs_block block;

	disk_read(0,block.data); 

    int i;
    for(i=0;i<block.super.nblocks;i++){
        if(!map[i])
            return i;
    }

    return -1;
}

int fs_format()
{
    // check for block bitmap
    if(map){
        printf("Error: disk is mounted.\n");
        return 0;
    }
    
	union fs_block block;

    // write super block
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
    block.super.ninodeblocks = (disk_size()+9)/10;
    block.super.ninodes = block.super.ninodeblocks*INODES_PER_BLOCK;
    disk_write(0,block.data);

    int ninodeblocks = block.super.ninodeblocks;
        
    // create empty block
    int i;
    for(i=0;i<DISK_BLOCK_SIZE;i++)
        block.data[i] = 0;
    
    // clear inodes
    for(i=1;i<=ninodeblocks;i++)
        disk_write(i,block.data);

    return 1;
}

void fs_debug()
{
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
        free(map);
        map = 0;
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
    if(!map){
        printf("Error: no mounted disk\n");
        return 0;
    }

    union fs_block block;

    disk_read(0, block.data);
    int ninodeblocks = block.super.ninodeblocks;

    int i;

    for (i = 1; i <= ninodeblocks; i++) {
      disk_read(i, block.data);

      // skip inode 0
      int j=0;
      if (i ==1) {
        j = 1;
      }
      for(; j < INODES_PER_BLOCK; j++) {
        //find invalid inode to replace
        if (!block.inodes[j].isvalid) {
          struct fs_inode inode;
          inode.size = 0;
          inode.isvalid = 1; 
          inode.indirect = 0;
          int k;
          for(k = 0; k < POINTERS_PER_INODE; k++) {
            inode.direct[k] = 0;
          }

          // write to disk and return
          inode_save((j+((i-1)*INODES_PER_BLOCK)), &inode);
          return (j+((i-1)*INODES_PER_BLOCK)); 
        }
      }
    } 

    printf("Error: no free inode space\n");  
    return 0;
}

int fs_delete( int inumber )
{
    if(!map){
        printf("Error: no mounted disk\n");
        return 0;
    }

  if(!valid_inumber(inumber) || inumber ==0 ){
    printf("Error: invalid inumber\n");
    return 0;
  }

  // access the inode
  struct fs_inode inode;
  inode_load(inumber, &inode);

  if(!inode.isvalid){
    printf("Error: invalid inode\n");
    return 0;
  }

  // release the data
  inode.size = 0;
  inode.isvalid = 0;
  int k;
  for(k = 0; k < POINTERS_PER_INODE; k++) {
    if(inode.direct[k]) {
      // free direct blocks on bitmap
      map[inode.direct[k]] = 0;
      inode.direct[k] = 0;
    }
  }

  if(inode.indirect) {
    union fs_block block;
    disk_read(inode.indirect, block.data);
    int i;

    // free indirect data blocks
    for(i=0;i<POINTERS_PER_BLOCK;i++)
      if(block.pointers[i]) 
        map[block.pointers[i]] = 0;

    inode.indirect = 0;
  }

  inode_save(inumber, &inode);
  return 1; 

}

int fs_getsize( int inumber )
{
    if(!map){
        printf("Error: no mounted disk\n");
        return -1;
    }

    if(!valid_inumber(inumber)){
      printf("Error: invalid inumber\n");
      return -1;
    }

    struct fs_inode inode;

    inode_load( inumber, &inode );

    if( !inode.isvalid ){
        printf("Error: invalid inode\n");
        return -1;
    }

    return inode.size;
}

int fs_read( int inumber, char *data, int length, int offset )
{
    if(!map){
        printf("Error: no mounted disk\n");
        return 0;
    }

    if(!valid_inumber(inumber)){
        printf("Error: invalid inumber\n");
        return 0;
    }

    int read = 0;
    union fs_block block;

    // access the inode
    struct fs_inode inode;
    inode_load(inumber, &inode);
    
    if(!inode.isvalid){
        printf("Error: invalid inode\n");
        return 0;
    }

    // limit length to inode size
    if( offset > inode.size )
        return 0;
    if( length > inode.size-offset )
        length = inode.size-offset;
    
    union fs_block indirect;
    int i;
    for(i=0;i<POINTERS_PER_INODE+POINTERS_PER_BLOCK && read < length;i++) {
        if(i==POINTERS_PER_INODE){
            if(inode.indirect)
                disk_read(inode.indirect, indirect.data);
            else
                return read;
        }

        int pointer;
        if(i<POINTERS_PER_INODE)
            pointer = inode.direct[i];
        else
            pointer = indirect.pointers[i-POINTERS_PER_INODE];

        if(pointer) {
            // skip block if offset is big enough
            if( offset > DISK_BLOCK_SIZE ){
                offset -= DISK_BLOCK_SIZE;
                continue;
            }

            // bytes to read in this iteration
            int to_read = length-read;
            // don't read outside of block
            if( to_read > DISK_BLOCK_SIZE-offset )
                to_read = DISK_BLOCK_SIZE-offset;
     
            // copy memory
            disk_read(pointer, block.data);
            memcpy(data+read, block.data+offset, to_read);

            // offset is 0 after first copy
            offset = 0;
            read += to_read;
        }
    }

    return read;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    if(!map){
        printf("Error: no mounted disk\n");
        return 0;
    }

    if(!valid_inumber(inumber)){
        printf("Error: invalid inumber\n");
        return 0;
    }

    int written = 0;
    union fs_block block;

    // access the inode
    struct fs_inode inode;
    inode_load(inumber, &inode);
    
    if(!inode.isvalid){
        printf("Error: invalid inode\n");
        return 0;
    }

    int max = (POINTERS_PER_INODE+POINTERS_PER_BLOCK)*DISK_BLOCK_SIZE;
    // limit length to inode size
    if( offset > max )
        return 0;
    if( length > max-offset )
        length = max-offset;
    
    union fs_block indirect;
    int i;
    for(i=0;i<POINTERS_PER_INODE+POINTERS_PER_BLOCK && written < length;i++) {
        if(i==POINTERS_PER_INODE){
            if(inode.indirect)
                // existing pointer block
                disk_read(inode.indirect, indirect.data);
            else{
                int free_index = free_block();
                
                // no free blocks
                if(free_index==-1)
                    return written;

                // allocate pointer block                
                inode.indirect=free_index;
                map[free_index]=1;
                int j;
                for(j=0;j<DISK_BLOCK_SIZE;j++)
                    block.data[j]=0;

                disk_write(inode.indirect, block.data);
                disk_read(inode.indirect, indirect.data);
            }
        }

        int pointer;
        if(i<POINTERS_PER_INODE)
            pointer = inode.direct[i];
        else
            pointer = indirect.pointers[i-POINTERS_PER_INODE];

        // skip block if offset is big enough
        if( offset > DISK_BLOCK_SIZE ){
            offset -= DISK_BLOCK_SIZE;
            continue;
        }

        // bytes to read in this iteration
        int to_write = length-written;

        // don't read outside of block
        if( to_write > DISK_BLOCK_SIZE-offset )
            to_write = DISK_BLOCK_SIZE-offset;
     
        // allocate if necessary
        if( !pointer ){
            int free_index = free_block();

            if(free_index==-1)
                return written;
        
            pointer = free_index;
            map[free_index]=1;

            // update inode pointers
            if(i<POINTERS_PER_INODE)
                inode.direct[i] = pointer;
            else{
                indirect.pointers[i-POINTERS_PER_INODE] = pointer;
                disk_write(inode.indirect, indirect.data); 
            }

            int j;
            for(j=0;j<DISK_BLOCK_SIZE;j++)
                block.data[j]=0;    
        }
        else {
            // copy memory
            disk_read(pointer, block.data);
        }

        // save inode to disk
        inode.size+=to_write;
        inode_save(inumber, &inode);

        // write data
        memcpy(block.data+offset, data+written, to_write);
        disk_write(pointer, block.data);
        

        // offset is 0 after first copy
        offset = 0;
        written += to_write;
    }

    return written;
}
