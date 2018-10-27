#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

// diskStatus = 1 if disk mounted / 0 if no disk mounted
int diskStatus = 0;

struct superblock
{
	// Offset: 0x00	Length: 8	Signature
	// Offset: 0x08	Length: 2	Total amount of blocks of virtual disk
	// Offset: 0x0A	Length: 2	Root directory block index
	// Offset: 0x0C	Length: 2	Data block start index
	// Offset: 0x0E	Length: 2	Amount of data blocks
	// Offset: 0x10	Length: 1	Number of blocks for FAT
	// Offset: 0x11	Length: 4079	Unused/Padding

	char signature[8];
	uint16_t numberOfBlocks;
	uint16_t rootBlockIndex;
	uint16_t dataBlockStartIndex;
	uint16_t numberOfDataBlocks;
	uint8_t numberOfFATBlocks;
	char unused[4079];
} __attribute__((packed));

struct superblock Superblock;

uint16_t* FAT;

struct rootEntry
{
	// Offset: 0x00	Length: 16	Filename
	// Offset: 0x10	Length: 4	File Size
	// Offset: 0x14	Length: 2	Index of 1st data block
	// Offset: 0x16	Length: 10	Unused/Padding

	char filename[FS_FILENAME_LEN];
	uint32_t fileSize;
	uint16_t firstDataBlockIndex;
	char unused[10];
} __attribute__((packed));

struct rootEntry Root[FS_FILE_MAX_COUNT];

struct fileDescriptor
{
	int fileIndex;
	int fileOffset;
};

struct fileDescriptor fdTable[FS_OPEN_MAX_COUNT];

int findEmptyFD(void)
{
	for (int fdTableIndex = 0; fdTableIndex < FS_OPEN_MAX_COUNT; fdTableIndex++){
		if (fdTable[fdTableIndex].fileIndex == -1){
			return fdTableIndex;
		}
	}
	return -1;
}


int findEmptyFAT(void)
{
	for (int FATIndex = 1; FATIndex < Superblock.numberOfDataBlocks; FATIndex++){
		if (FAT[FATIndex]==0){
			return FATIndex;
		}
	}
	return 0;
}

int fs_mount(const char *diskname)
{
	// Error Check: Disk currently mounted
	if (diskStatus == 1){
		return -1;
	}

	diskStatus = block_disk_open(diskname);

	// Error Check: Cannot open disk
	if (diskStatus == -1){
		diskStatus = 0;
		return -1;
	}
	diskStatus = 1;

	// Load Superblock
	block_read(0,(void*)&Superblock);

	// Error Check: Signature
	char expectedSignature[8] = "ECS150FS";
	if(memcmp(Superblock.signature,expectedSignature,8) != 0){
		return -1;
	}

	// Error Check: Disk Count
	if (Superblock.numberOfBlocks != block_disk_count()){
		return -1;
	}

	// Load FAT
	FAT = (uint16_t*)malloc(Superblock.numberOfFATBlocks * BLOCK_SIZE);
	for (int FATIndex = 0; FATIndex < Superblock.numberOfFATBlocks; FATIndex++){
		block_read((FATIndex+1),(void*)FAT+(BLOCK_SIZE * FATIndex));
	}
	
	// Error Check: First FAT is EOC
	if (FAT[0]!=FAT_EOC){
		return -1;
	}

	// Load Root Directory
	block_read((Superblock.rootBlockIndex),(void*)&Root);

	// Initialize FDTable
	for (int tableIndex = 0; tableIndex < FS_OPEN_MAX_COUNT; tableIndex++){
		fdTable[tableIndex].fileIndex = -1;
		fdTable[tableIndex].fileOffset = 0;
	}
	return 0;
}

int fs_umount(void)
{
	// Error Check: Ensure disk is mounted
	if (diskStatus != 1){
		return -1;
	}

	// Error Check: Open file descriptor
	for (int tableIndex = 0; tableIndex < FS_OPEN_MAX_COUNT; tableIndex++){
		if(fdTable[tableIndex].fileIndex != -1){
			return -1;
		}
	}

	// Write Superblock to disk
	block_write(0,(void*)&Superblock);

	// Write FAT to disk
	for (int FATIndex = 0; FATIndex < Superblock.numberOfFATBlocks; FATIndex++){
		block_write((FATIndex+1),(void*)FAT+(BLOCK_SIZE * FATIndex));
	}

	// Write Root to disk
	block_write((Superblock.rootBlockIndex),(void*)&Root);

	diskStatus = block_disk_close();
	if (diskStatus == -1){
		return -1;
	}
	free(FAT);
	return 0;
}

int fs_info(void)
{
	printf("FS Info:\n");
	printf("total_blk_count=%d\n",Superblock.numberOfBlocks);
	printf("fat_blk_count=%d\n",Superblock.numberOfFATBlocks);
	printf("rdir_blk=%d\n",Superblock.rootBlockIndex);
	printf("data_blk=%d\n",Superblock.dataBlockStartIndex);
	printf("data_blk_count=%d\n",Superblock.numberOfDataBlocks);
	int freeBlocks = 0;
	for(int FATIndex = 0; FATIndex < Superblock.numberOfDataBlocks; FATIndex++){
		if (FAT[FATIndex] == 0){
			freeBlocks++;
		}
	}
	printf("fat_free_ratio=%d/%d\n",freeBlocks,Superblock.numberOfDataBlocks);
	int freeRoot = 0;
	for(int rootDirectoryIndex = 0; rootDirectoryIndex < FS_FILE_MAX_COUNT; rootDirectoryIndex++){
		if ((char)Root[rootDirectoryIndex].filename[0] == '\0'){
			freeRoot++;
		}
	}
	printf("rdir_free_ratio=%d/%d\n",freeRoot,FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	// Error Check
	if ((strlen(filename) > FS_FILENAME_LEN) || (diskStatus != 1)){
		return -1;
	}

	// Find empty Root
	int freeRootIndex = -1;
	for (int rootIndex = 0; rootIndex < FS_FILE_MAX_COUNT; rootIndex++){
		// Error Check: Filename already exists in root
		if (strcmp(Root[rootIndex].filename, filename) == 0){
			return -1;
		}

		if (((char)Root[rootIndex].filename[0]=='\0')&&(freeRootIndex == -1)){
			freeRootIndex = rootIndex;
		}
	}

	// Error Check: No empty room in root directory
	if (freeRootIndex == -1){
		return -1;
	}

	// Set fields for new Root
	strcpy(Root[freeRootIndex].filename,filename);
	Root[freeRootIndex].fileSize = 0;	
	Root[freeRootIndex].firstDataBlockIndex = FAT_EOC;
	return 0;
}

int fs_delete(const char *filename)
{
	// Error Check
	if ((strlen(filename) > FS_FILENAME_LEN) || (diskStatus != 1)){
		return -1;
	}

	// Find file in Root
	int fileIndex = -1;
	for (int rootIndex = 0; rootIndex < FS_FILE_MAX_COUNT; rootIndex++){
		// Error Check: Filename already exists in root
		if (strcmp(Root[rootIndex].filename, filename) == 0){
			fileIndex = rootIndex;
			break;
		}
	}

	// Error Check: File not found
	if (fileIndex == -1){
		return -1;
	}
	
	// Clear the Root
	memset(Root[fileIndex].filename,'\0',sizeof(Root[fileIndex].filename));
	Root[fileIndex].fileSize = 0;
	uint16_t nextFATBlock = Root[fileIndex].firstDataBlockIndex;
	Root[fileIndex].firstDataBlockIndex = 0;

	// Clear the FAT & free Data Blocks
	if (nextFATBlock == FAT_EOC){
		return 0;
	}
	char NULLBlock[BLOCK_SIZE] = {};
	while(FAT[nextFATBlock] != FAT_EOC){
		uint16_t clearedBlock = nextFATBlock;
		nextFATBlock = FAT[nextFATBlock];
		FAT[clearedBlock] = 0;
		block_write(FAT[clearedBlock]+Superblock.dataBlockStartIndex, NULLBlock);
	}
	FAT[nextFATBlock] = 0;
	block_write(FAT[nextFATBlock]+Superblock.dataBlockStartIndex, NULLBlock);
	return 0;
}

int fs_ls(void)
{
	if(diskStatus != 1){
		return -1;
	}
	printf("FS Ls:\n");
	for (int rootIndex = 0; rootIndex < FS_FILE_MAX_COUNT; rootIndex++){
		if ((Root[rootIndex].filename[0]) != '\0'){
			printf("file: %s, size: %d, data_blk: %d\n",Root[rootIndex].filename,Root[rootIndex].fileSize,Root[rootIndex].firstDataBlockIndex);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	// Error Check: Filename too large
	if ((strlen(filename) > FS_FILENAME_LEN) || (diskStatus != 1)){
		return -1;
	}

	int openFD = findEmptyFD();

	// Error Check: FD Table full
	if (openFD == -1){
		return -1;
	}

	// Find File in Root
	int fileIndex = -1;
	for (int rootIndex = 0; rootIndex < FS_FILE_MAX_COUNT; rootIndex++){
		if (strcmp(filename,Root[rootIndex].filename)== 0){
			fileIndex = rootIndex;
			break;
		}
	}

	// Error Check: File not found in Root
	if (fileIndex == -1){
		return -1;
	}

	fdTable[openFD].fileIndex = fileIndex;
	return openFD;
}

int fs_close(int fd)
{
	// Error Check
	if (diskStatus != 1 || fd >= FS_OPEN_MAX_COUNT || fd < 0 || fdTable[fd].fileIndex == -1){
		return -1;
	}

	fdTable[fd].fileIndex = -1;
	fdTable[fd].fileOffset = 0;
	return 0;
}

int fs_stat(int fd)
{
	// Error Check
	if (diskStatus != 1 || fd >= FS_OPEN_MAX_COUNT || fd < 0 || fdTable[fd].fileIndex == -1){
		return -1;
	}

	int fileSize = Root[fdTable[fd].fileIndex].fileSize;
	return fileSize;
}

int fs_lseek(int fd, size_t offset)
{
	// Error Check
	if (diskStatus != 1 || fd >= FS_OPEN_MAX_COUNT || fd < 0 || fdTable[fd].fileIndex == -1 || offset > (Root[fdTable[fd].fileIndex].fileSize)){
		return -1;
	}
	fdTable[fd].fileOffset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	// Error Check
	if (diskStatus != 1 || fd >= FS_OPEN_MAX_COUNT || fd < 0 || fdTable[fd].fileIndex == -1 || fdTable[fd].fileOffset > Root[fdTable[fd].fileIndex].fileSize){
		return -1;
	}

	if (count == 0){
		return 0;
	}

	int fileIndex = fdTable[fd].fileIndex;

	// Writing past current file datablock (allocate new blocks)
	int numberOfFileDataBlocks = ((Root[fileIndex].fileSize +(BLOCK_SIZE-1))/BLOCK_SIZE);
	if ((((fdTable[fd].fileOffset + count)+(BLOCK_SIZE-1))/BLOCK_SIZE) > numberOfFileDataBlocks){
		int numberOfNewBlocks = ((((fdTable[fd].fileOffset + count)+(BLOCK_SIZE-1))/BLOCK_SIZE) - numberOfFileDataBlocks);
		int totalNumberOfNewBlocks = numberOfNewBlocks;
		int currentBlock = 0;
		int nextBlock = Root[fileIndex].firstDataBlockIndex;
		while (nextBlock != FAT_EOC){
			currentBlock = nextBlock;
			nextBlock = FAT[currentBlock];
		}
		if (currentBlock == 0){	
			Root[fileIndex].firstDataBlockIndex = findEmptyFAT();
			
			// Error Check: No room left on disk
			if(Root[fileIndex].firstDataBlockIndex == 0){
				FAT[currentBlock] = FAT_EOC;
				Root[fileIndex].firstDataBlockIndex = FAT_EOC;
				return 0;
			}

			currentBlock = Root[fileIndex].firstDataBlockIndex;
			FAT[currentBlock] = FAT_EOC;
			numberOfNewBlocks--;
		}
		while(numberOfNewBlocks > 0){
			FAT[currentBlock] = findEmptyFAT();

			// Error Check: No room left on disk
			if(FAT[currentBlock] == 0){
				FAT[currentBlock] = FAT_EOC;
				count = ((numberOfFileDataBlocks*BLOCK_SIZE)-Root[fileIndex].fileSize)+((totalNumberOfNewBlocks-numberOfNewBlocks)*BLOCK_SIZE);
				break;
			}

			currentBlock = FAT[currentBlock];
			FAT[currentBlock] = FAT_EOC;
			numberOfNewBlocks--;
		}
		Root[fileIndex].fileSize = (fdTable[fd].fileOffset + count);
	}

	// Load full file into bounce buffer
	void* fullFile = (void*)malloc((((Root[fileIndex].fileSize)+(BLOCK_SIZE - 1))/BLOCK_SIZE)*BLOCK_SIZE);

	int dataIndex = 0;
	int currentDataBlock = 0;
	int nextDataBlock = Root[fileIndex].firstDataBlockIndex; // Should correspond to FAT index here

	// Load full file into fullFile
	while (nextDataBlock != FAT_EOC){
		currentDataBlock = nextDataBlock;
		nextDataBlock = FAT[currentDataBlock];
		block_read(currentDataBlock+Superblock.dataBlockStartIndex, fullFile + dataIndex);
		dataIndex = dataIndex + BLOCK_SIZE;
	}

	// Write to full file
	memcpy(fullFile+fdTable[fd].fileOffset,buf,count);
	fdTable[fd].fileOffset = fdTable[fd].fileOffset + count;

	// Write full file back to disk
	dataIndex = 0;
	currentDataBlock = 0;
	nextDataBlock = Root[fileIndex].firstDataBlockIndex;
	while (nextDataBlock != FAT_EOC){
		currentDataBlock = nextDataBlock;
		nextDataBlock = FAT[currentDataBlock];
		block_write(currentDataBlock+Superblock.dataBlockStartIndex,(void*)fullFile+dataIndex);
		dataIndex = dataIndex + BLOCK_SIZE;
	}
	memset(fullFile,0,sizeof(fullFile));
	free(fullFile);
	return count;
}

int fs_read(int fd, void *buf, size_t count)
{
	// Error Check
	if (diskStatus != 1 || fd >= FS_OPEN_MAX_COUNT || fd < 0 || fdTable[fd].fileIndex == -1 || fdTable[fd].fileOffset > (Root[fdTable[fd].fileIndex].fileSize)){
		return -1;
	}

	// Generate the full file to read from
	int fileIndex = fdTable[fd].fileIndex;
	void* fullFile = (void*)malloc((((Root[fileIndex].fileSize)+(BLOCK_SIZE - 1))/BLOCK_SIZE)*BLOCK_SIZE);
	int dataIndex = 0;
	int currentBlock = Root[fileIndex].firstDataBlockIndex;
	
	// Case: Empty File
	if(currentBlock == FAT_EOC){
		return 0;
	}

	while (FAT[currentBlock] != FAT_EOC){
		block_read(currentBlock+Superblock.dataBlockStartIndex,(void*)fullFile + dataIndex);
		currentBlock = FAT[currentBlock];
		dataIndex = dataIndex + BLOCK_SIZE;
	}
	block_read(currentBlock+Superblock.dataBlockStartIndex, (void*)fullFile + dataIndex);
	dataIndex = dataIndex + BLOCK_SIZE; // Should be number of data blocks * BLOCK_SIZE

	if((fdTable[fd].fileOffset + count) > Root[fileIndex].fileSize){
		count = (Root[fileIndex].fileSize - fdTable[fd].fileOffset);
		if(count > Root[fileIndex].fileSize){
			count = Root[fileIndex].fileSize;
		}
	}

	memcpy(buf,fullFile+fdTable[fd].fileOffset,count);
	fdTable[fd].fileOffset = fdTable[fd].fileOffset + count;
	memset(fullFile,0,sizeof(fullFile));
	free(fullFile);
	return count;
}

