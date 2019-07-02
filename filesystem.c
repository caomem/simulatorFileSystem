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
	
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 1))){
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

/**
 * @brief Localize a directory.
 * @param dir Directory struct reference.
 * @param absoluteDir Destination file path on the simulated file system.
 * @param file File name residue in absoluteDir
 * @param section Reference to section locate of directory localized 
 * @param showLabel Set 1 to print directory name or 0 to not print  
 * @return 0 on success.
 */
int locateDir(struct table_directory* dir, char *absoluteDir, char **file, unsigned int *section, char showLabel){
	int i, newDir = 0, j;
	char *dirBuff = (char*)calloc(20, 1);
	for (i = 0; i < strlen(absoluteDir); i++)
	{
		if (absoluteDir[i] == '/'){
			if (i+1 >= strlen(absoluteDir)) return 1;
			newDir = 1;
			break;
		}else{
			if(i>20) return 1;
			dirBuff[i] = absoluteDir[i];
		}
	}
	if (strlen(dirBuff) < 1) return 1;
	if (newDir){
		for (j = 0; j < DIR_LENGTH; j++){
			if (!dir->entries[j].sector_start){
				continue;
			}			
			if (!strcmp(dirBuff, dir->entries[j].name)){
				if (dir->entries[j].dir){
					if (section) *section = dir->entries[j].sector_start;
					int ret;
					if ((ret = ds_read_sector(dir->entries[j].sector_start, (void*)dir, SECTOR_SIZE))){
						ds_stop();
						return ret;
					}
					free(dirBuff);
					return locateDir(dir, absoluteDir+i+1, file, section, showLabel);
				} else return 1;
			}		
		}
	} else {
		if (file) *file = dirBuff;
		return 0;
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

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
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

	// check the existence of space for new data
	struct sector_0 sector0;
    memset(&sector0, 0, sizeof(sector0));
	if ( (ret = ds_read_sector(0, (void*)&sector0, SECTOR_SIZE))){
		fclose(file);
		ds_stop();
		return ret;
	}
	if (sector0.free_sectors_list == 0){
		/* error end of memory */
		perror("EndOfMemory: ");
		fclose(file);
		ds_stop();
		return 1;
	}
	struct sector_data freeSector;
    memset(&freeSector, 0, sizeof(freeSector));
	if ( (ret = ds_read_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE))){
		fclose(file);
		ds_stop();
		return ret;
	}

	// check the directory existence and create the structure for save
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		fclose(file);
		ds_stop();
		return ret;
	}
	char *filename;
	unsigned int sec = 1;
	if ( (ret = locateDir(&dir, simul_file+1, &filename, &sec, 0))){
		perror("Not is possible localize directory: ");
		fclose(file);
		ds_stop();
		return ret;
	}
	if (strlen(filename) < 1){
		perror("Invalid file name: ");
		fclose(file);
		ds_stop();
		return 1;
	}
	int i;
	char hasSpace = 0;	
	for (i = 0; i < DIR_LENGTH; i++){
		if (!dir.entries[i].sector_start){
			dir.entries[i].dir = 0;
			strcpy(dir.entries[i].name, filename);
			dir.entries[i].size_bytes = b.st_size;	
			dir.entries[i].sector_start = sector0.free_sectors_list;
			hasSpace = 1;
			break;
		}
	}
	if (!hasSpace){
		perror("Directory is loted: ");
		fclose(file);
		ds_stop();
		return 1;
	}
	
	// save the new file
	int n;
	unsigned int last = 0;
    while((n = fread(freeSector.data, sizeof(char), DATA_LENGTH, file)) == DATA_LENGTH){
        if ( (ret = ds_write_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE))){
			fclose(file);
			ds_stop();
			return ret;
		}
		last = sector0.free_sectors_list;
		sector0.free_sectors_list = freeSector.next_sector;
		if (sector0.free_sectors_list == 0){
			/* error end of memory */
			perror("EndOfMemory: ");
			fclose(file);
			ds_stop();
			return 1;
		}
        memset(&freeSector, 0, sizeof(freeSector));
		if ( (ret = ds_read_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE))){
			fclose(file);
			ds_stop();
			return ret;
		}
    }
	unsigned int aux;
	if(n != 0){ // until exists data for saving
		aux = freeSector.next_sector;
		freeSector.next_sector = 0;
		if ( (ret = ds_write_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE))){
			fclose(file);
			ds_stop();
			return ret;
		}
	}else if(last != 0){ // set the final of file in last section saved
		memset(&freeSector, 0, sizeof(freeSector));
		if ( (ret = ds_read_sector(last, (void*)&freeSector, SECTOR_SIZE))){
			fclose(file);
			ds_stop();
			return ret;
		}
		aux = freeSector.next_sector;
		freeSector.next_sector = 0;
		if ( (ret = ds_write_sector(last, (void*)&freeSector, SECTOR_SIZE))){
			fclose(file);
			ds_stop();
			return ret;
		}
	}else {
		/* error input_file void */
		perror("Input_file is Void: ");
		fclose(file);
		ds_stop();
		return 1;
	}	
	sector0.free_sectors_list = aux;
	if ( (ret = ds_write_sector(0, (void*)&sector0, SECTOR_SIZE))){ // write the free sector list
		fclose(file);
		ds_stop();
		return ret;
	}
	if ( (ret = ds_write_sector(sec, (void*)&dir, SECTOR_SIZE))){ // write the directory sector
		fclose(file);
		ds_stop();
		return ret;
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
	
	if (strlen(simul_file)<2 || simul_file[0] != '/'){
		/* Param error */
		perror("simul_file is not valid: ");
		return 1;
	}

	if (strlen(output_file)<1 || output_file[strlen(output_file)-1] == '/'){
		/* Param error */
		perror("output_file is not valid: ");
		return 1;
	}
	
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
		return ret;
	}

	FILE* file =  NULL;
	/* Create the file for output */
	if( (file = fopen(output_file, "wb")) == NULL){
		/* error creating the file */
		perror("fopen: ");
		ds_stop();
		return 1;
	}

	// check the file existence in simul
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		fclose(file);
		ds_stop();
		return ret;
	}
	char *filename;
	unsigned int sec = 1;
	if ( (ret = locateDir(&dir, simul_file+1, &filename, &sec, 0))){
		perror("Not is possible localize directory: ");
		fclose(file);
		ds_stop();
		return ret;
	}
	if (strlen(filename) < 1){
		perror("Invalid input_file name: ");
		fclose(file);
		ds_stop();
		return 1;
	}

	int i;	
	char flag = 0;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start && !dir.entries[i].dir && !strcmp(dir.entries[i].name, filename)){
			flag = 1;
			break;
		}
	}	
	if (!flag) {
		perror("Not is possible find the input_file: ");
		fclose(file);
		ds_stop();
		return 1;
	}
	int lastPiece = dir.entries[i].size_bytes%DATA_LENGTH;

	// read input file and write in output file
	struct sector_data data;
	memset(&data, 0, sizeof(data));
	if ( (ret = ds_read_sector(dir.entries[i].sector_start, (void*)&data, SECTOR_SIZE))){
		perror("error when read the file: ");
		fclose(file);
		ds_stop();
		return ret;
	}

	while (data.next_sector){
		if ( !(ret = fwrite(data.data, sizeof(char), DATA_LENGTH, file))){
			perror("error when write in the outpu_file: ");
			fclose(file);
			ds_stop();
			return ret;
		}
		if ( (ret = ds_read_sector(data.next_sector, (void*)&data, SECTOR_SIZE))){
			perror("error when read the file: ");
			fclose(file);
			ds_stop();
			return ret;
		}
	}
	if (lastPiece){
		if ( !(ret = fwrite(data.data, sizeof(char), lastPiece, file))){
			perror("error when write last piece in the outpu_file: ");
			fclose(file);
			ds_stop();
			return ret;
		}
	}	

	fclose(file);
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

	if (strlen(simul_file)<2 || simul_file[0] != '/'){
		/* Param error */
		perror("simul_file is not valid: ");
		return 1;
	}

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
		return ret;
	}
	
	// check the file existence in simul
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}
	char *filename;
	unsigned int sec = 1;
	if ( (ret = locateDir(&dir, simul_file+1, &filename, &sec, 0))){
		perror("Not is possible localize directory: ");
		ds_stop();
		return ret;
	}
	if (strlen(filename) < 1){
		perror("Invalid input_file name: ");
		ds_stop();
		return 1;
	}

	int i;	
	char flag = 0;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start && !dir.entries[i].dir && !strcmp(dir.entries[i].name, filename)){
			flag = 1;
			break;
		}
	}	
	if (!flag) {
		perror("Not is possible find the input_file: ");
		ds_stop();
		return 1;
	}

	// Delete input file
	struct sector_data data;
	memset(&data, 0, sizeof(data));
	if ( (ret = ds_read_sector(dir.entries[i].sector_start, (void*)&data, SECTOR_SIZE))){
		perror("error when read the file: ");
		ds_stop();
		return ret;
	}
	
	struct sector_0 sector0;
    memset(&sector0, 0, sizeof(sector0));
	if ( (ret = ds_read_sector(0, (void*)&sector0, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}
	unsigned int last;
	do{
		last = data.next_sector;
		if ( (ret = ds_read_sector(data.next_sector, (void*)&data, SECTOR_SIZE))){
			perror("error when read the file: ");
			ds_stop();
			return ret;
		}
	} while (data.next_sector);
	data.next_sector = sector0.free_sectors_list;
	if ((ret = ds_write_sector(last, &data, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

	sector0.free_sectors_list = dir.entries[i].sector_start;
	if ((ret = ds_write_sector(0, &sector0, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

	dir.entries[i].sector_start = 0;
	if ((ret = ds_write_sector(sec, &dir, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

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

	if (strlen(dir_path)<1 || dir_path[0] != '/'){
		/* Param error */
		perror("dir_path is not valid: ");
		return 1;
	}

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
		return ret;
	}
	
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}
	char *filename;
	if (strlen(dir_path) > 1)	
	if ( (ret = locateDir(&dir, dir_path+1, &filename, NULL, 0))){
		perror("Not is possible localize directory: ");
		ds_stop();
		return ret;
	}
	
	int i;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start && !strcmp(dir.entries[i].name, filename))
		{
			if (!dir.entries[i].dir){
				perror("Not is possible localize directory: ");
				ds_stop();
				return ret;
			}
			if ( (ret = ds_read_sector(dir.entries[i].sector_start, (void*)&dir, SECTOR_SIZE))){
				ds_stop();
				return ret;
			}
			break;
		}
	}
	
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start)
		{
			printf(" => %c %s\t%d bytes\n", (dir.entries[i].dir)?'d':'f', dir.entries[i].name, dir.entries[i].size_bytes);
		}
	}

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
	if (strlen(directory_path)<2 || directory_path[0] != '/'){
		/* Param error */
		perror("directory_path is not valid: ");
		return 1;
	}

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
		return ret;
	}
	
	// check the directory
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}
	unsigned int sec = 1;
	char *filename;
	if ( (ret = locateDir(&dir, directory_path+1, &filename, &sec, 0))){
		perror("Not is possible localize directory: ");
		ds_stop();
		return ret;
	}
	int i;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start && !strcmp(dir.entries[i].name, filename))
		{
			perror("Not is possible create this directory, alredy exist a file with it name: ");
			ds_stop();
			return ret;
		}
	}
	char flag = 0;
	for (i = 0; i < DIR_LENGTH; i++){
		if (!dir.entries[i].sector_start)
		{
			flag = 1;
			break;
		}
	}
	if (!flag){
		perror("Directory is loted: ");
		ds_stop();
		return 1;
	}

	// check the existence of space for new data
	struct sector_0 sector0;
    memset(&sector0, 0, sizeof(sector0));
	if ( (ret = ds_read_sector(0, (void*)&sector0, SECTOR_SIZE))){
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
    memset(&freeSector, 0, sizeof(freeSector));
	if ( (ret = ds_read_sector(sector0.free_sectors_list, (void*)&freeSector, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}

	struct table_directory newDir;
	memset(&newDir, 0, sizeof(newDir));
	if ((ret = ds_write_sector(sector0.free_sectors_list, &newDir, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}
	dir.entries[i].sector_start = sector0.free_sectors_list;
	dir.entries[i].dir = 1;
	strcpy(dir.entries[i].name, filename);
	dir.entries[i].size_bytes = 0;
	if ((ret = ds_write_sector(sec, &dir, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

	sector0.free_sectors_list = freeSector.next_sector;
	if ((ret = ds_write_sector(0, &sector0, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

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
	if (strlen(directory_path)<3 || directory_path[0] != '/' || directory_path[strlen(directory_path)-1] != '/'){
		/* Param error */
		perror("directory_path is not valid: ");
		return 1;
	}

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
		return ret;
	}
	
	// check the file existence in simul
	struct table_directory dir;
    memset(&dir, 0, sizeof(dir));
	if ( (ret = ds_read_sector(1, (void*)&dir, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}
	char *filename;
	unsigned int sec = 1;
	char *labelAux = (char*)calloc(strlen(directory_path), 1);
	strcpy(labelAux, directory_path+1);

	labelAux[strlen(labelAux)-1] = 0;
	if ( (ret = locateDir(&dir, labelAux, &filename, &sec, 0))){
		perror("Not is possible localize directory: ");
		ds_stop();
		return ret;
	}
	
	if (strlen(filename) < 1){
		perror("Invalid directory_path name: ");
		ds_stop();
		return 1;
	}
	int i;	
	char flag = 0;
	for (i = 0; i < DIR_LENGTH; i++){
		if (dir.entries[i].sector_start && dir.entries[i].dir && !strcmp(dir.entries[i].name, filename)){
			flag = 1;
			break;
		}
	}	
	if (!flag) {
		perror("Not is possible find the directory_path: ");
		ds_stop();
		return 1;
	}

	// Delete input directory
	struct table_directory dirDel;
	memset(&dirDel, 0, sizeof(dir));
	if ( (ret = ds_read_sector(dir.entries[i].sector_start, (void*)&dirDel, SECTOR_SIZE))){
		perror("error when read the file: ");
		ds_stop();
		return ret;
	}

	int j;
	for (j = 0; j < DIR_LENGTH; j++)
	{
		if(dirDel.entries[j].sector_start)
		{
			perror("Directory not void: ");
			ds_stop();
			return ret;
		}
	}
	
	struct sector_0 sector0;
    memset(&sector0, 0, sizeof(sector0));
	if ( (ret = ds_read_sector(0, (void*)&sector0, SECTOR_SIZE))){
		ds_stop();
		return ret;
	}

	struct sector_data aux;
	aux.next_sector = sector0.free_sectors_list;
	if ((ret = ds_write_sector(dir.entries[i].sector_start, &aux, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}
	sector0.free_sectors_list = dir.entries[i].sector_start;
	if ((ret = ds_write_sector(0, &sector0, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

	dir.entries[i].sector_start = 0;
	if ((ret = ds_write_sector(sec, &dir, SECTOR_SIZE))){
		perror("error when write: ");
		ds_stop();
		return ret;
	}

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

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0))){
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

