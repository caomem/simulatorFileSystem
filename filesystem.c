#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "libdisksimul.h"
#include "filesystem.h"
#define DATA_LENGTH 508
#define DIR_LENGTH 16

/**
 * @brief Format disk.
 * 
 */
int fs_format(){
	int ret, i;
	struct table_directory root_dir;
	struct sector_0 sector0;
	struct sector_data sector;
	
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 1)) != 0 ){
		return ret;
	}
	
	memset(&sector0, 0, sizeof(struct sector_0));
	
	/* first free sector. */
	sector0.free_sectors_list = 2;
	
	ds_write_sector(0, (void*)&sector0, SECTOR_SIZE);
	
	memset(&root_dir, 0, sizeof(root_dir));
	
	ds_write_sector(1, (void*)&root_dir, SECTOR_SIZE);
	
	/* Create a list of free sectors. */
	memset(&sector, 0, sizeof(sector));
	
	for(i=2;i<NUMBER_OF_SECTORS;i++){
		if(i<NUMBER_OF_SECTORS-1){
			sector.next_sector = i+1;
		}else{
			sector.next_sector = 0;
		}
		ds_write_sector(i, (void*)&sector, SECTOR_SIZE);
	}
	
	ds_stop();
	
	printf("Disk size %d kbytes, %d sectors.\n", (SECTOR_SIZE*NUMBER_OF_SECTORS)/1024, NUMBER_OF_SECTORS);
	
	return 0;
}

locateDir(&dir, simul_file, filename, &sec)
int locateDir(struct table_directory* dir, char *absoluteDir, char *file, int *section){
	int i, flag = 0;
	char dirBuff[20];
	memset(dirBuff, 0, sizeof(dirBuff));
	for (i = 0; i < strlen(simul_file); i++)
	{
		if (simul_file[i] == '/'){
			flag = 1;
			break;
		}else{
			if(i>20) return 1;
			dirBuff[i] = simul_file[i];
		}
	}
	if (strlen(dirBuff) > 0)
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i] == NULL || dir.entries[i].sector_start == 0){
			continue;
		}
		if (!strcmp(dirBuff, dir.entries[i].name)){
			if (dir.entries[i].dir == 1){
				return locateDir();
			} else return 1;
		}
	}
	return 1;
		
}

/**
 * @brief Create a new file on the simulated filesystem.
 * @param input_file Source file path.
 * @param simul_file Destination file path on the simulated file system.
 * @return 0 on success.
 */
int fs_create(char* input_file, char* simul_file){
	int ret;
	struct stat b;

	if (strlen(simul_file)<2 || simul_file[0] != '/'){
		/* Param error */
		perror("simul_file is not valid: ");
		return 1;
	}

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	FILE* file =  NULL;
	/* Check if the input_file exists and open it */
	if( stat(input_file, &b) == 0){
		if( (file = fopen(input_file, "rb")) == NULL){
			/* error openning the file */
			perror("fopen: ");
			ds_stop();
			return 1;
		}
	}else{
		/* error file not exist */
		perror("fileNotExist: ");
		ds_stop();
		return 1;
	}

	struct sector_0 sector0;
    memset(sector0, 0, sizeof(sector0));
	if ( (ret = ds_read_sector(0, (void*)&sector0, SECTOR_SIZE)) != 0){
		ds_stop();
		return ret;
	}
	if (sector0.free_sectors_list == 0){
		/* error end of memory */
		perror("EndOfMemory: ");
		ds_stop();
		return 1;
	}
	struct sector_data freeSector;
    memset(freeSector, 0, sizeof(freeSector));
	if ( (ret = ds_read_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE)) != 0){
		ds_stop();
		return ret;
	}

	struct table_directory dir;
    memset(dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE)) != 0){
		ds_stop();
		return ret;
	}
	char filename[20];
	unsigned int sec;
	if ( (ret = locateDir(&dir, simul_file, filename, &sec)) != 0){
		ds_stop();
		return ret;
	}
	int i;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i] == NULL){
			struct file_dir_entry entry;
			memset(&entry, 0, sizeof(entry));
			dir.entries[i] = entry;
		}	
		if (dir.entries[i].sector_start == 0){
			dir.entries[i].dir = 0;
			dir.entries[i].name = filename;
			dir.entries[i].size_bytes = b.st_size;	
			dir.entries[i].sector_start = sector0.free_sectors_list;
			break;
		}
		
	}	

	int n;
	unsigned int last = 0;
    while((n = fread(freeSector.data, sizeof(char), DATA_LENGTH, file)) == DATA_LENGTH){
        if ( (ret = ds_write_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
		last = sector0.free_sectors_list;
		sector0.free_sectors_list = freeSector.next_sector;
		if (sector0.free_sectors_list == 0){
			/* error end of memory */
			perror("EndOfMemory: ");
			ds_stop();
			return 1;
		}
        memset(freeSector, 0, sizeof(freeSector));
		if ( (ret = ds_read_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE)) != 0){
			ds_stop();
			return ret;
		}
    }
	if(n != 0){
		unsigned int aux = freeSector.next_sector;
		freeSector.next_sector = 0;
		if ( (ret = ds_write_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
		sector0.free_sectors_list = aux;
		if ( (ret = ds_write_sector(0, (void*)&sector0, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
		if ( (ret = ds_write_sector(sec, (void*)&dir, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
	}else if(last != 0){
		memset(freeSector, 0, sizeof(freeSector));
		if ( (ret = ds_read_sector(last, (void*)&freeSector, SECTOR_SIZE)) != 0){
			ds_stop();
			return ret;
		}
		freeSector.next_sector = 0;
		if ( (ret = ds_write_sector(last, (void*)&freeSector, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
		sector0.free_sectors_list = aux;
		if ( (ret = ds_write_sector(0, (void*)&sector0, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
		if ( (ret = ds_write_sector(sec, (void*)&dir, SECTOR_SIZE) != 0){
			ds_stop();
			return ret;
		}
	}else {
		/* error input_file void */
		perror("Input_file is Void: ");
		ds_stop();
		return 1;
	}	
    fclose(file);	

	ds_stop();
	
	return 0;
}

/**
 * @brief Read file from the simulated filesystem.
 * @param output_file Output file path.
 * @param simul_file Source file path from the simulated file system.
 * @return 0 on success.
 */
int fs_read(char* output_file, char* simul_file){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
		
	/* Write the code to read a file from the simulated filesystem. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Delete file from file system.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_del(char* simul_file){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code delete a file from the simulated filesystem. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief List files from a directory.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_ls(char *dir_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to show files or directories. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Create a new directory on the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_mkdir(char* directory_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to create a new directory. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Remove directory from the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_rmdir(char *directory_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to delete a directory. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Generate a map of used/available sectors. 
 * @param log_f Log file with the sector map.
 * @return 0 on success.
 */
int fs_free_map(char *log_f){
	int ret, i, next;
	//struct root_table_directory root_dir;
	struct sector_0 sector0;
	struct sector_data sector;
	char *sector_array;
	FILE* log;
	int pid, status;
	int free_space = 0;
	char* exec_params[] = {"gnuplot", "sector_map.gnuplot" , NULL};

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* each byte represents a sector. */
	sector_array = (char*)malloc(NUMBER_OF_SECTORS);

	/* set 0 to all sectors. Zero means that the sector is used. */
	memset(sector_array, 0, NUMBER_OF_SECTORS);
	
	/* Read the sector 0 to get the free blocks list. */
	ds_read_sector(0, (void*)&sector0, SECTOR_SIZE);
	
	next = sector0.free_sectors_list;

	while(next){
		/* The sector is in the free list, mark with 1. */
		sector_array[next] = 1;
		
		/* move to the next free sector. */
		ds_read_sector(next, (void*)&sector, SECTOR_SIZE);
		
		next = sector.next_sector;
		
		free_space += SECTOR_SIZE;
	}

	/* Create a log file. */
	if( (log = fopen(log_f, "w")) == NULL){
		perror("fopen()");
		free(sector_array);
		ds_stop();
		return 1;
	}
	
	/* Write the the sector map to the log file. */
	for(i=0;i<NUMBER_OF_SECTORS;i++){
		if(i%32==0) fprintf(log, "%s", "\n");
		fprintf(log, " %d", sector_array[i]);
	}
	
	fclose(log);
	
	/* Execute gnuplot to generate the sector's free map. */
	pid = fork();
	if(pid==0){
		execvp("gnuplot", exec_params);
	}
	/* Wait gnuplot to finish */
	wait(&status);
	
	free(sector_array);
	
	ds_stop();
	
	printf("Free space %d kbytes.\n", free_space/1024);
	
	return 0;
}

