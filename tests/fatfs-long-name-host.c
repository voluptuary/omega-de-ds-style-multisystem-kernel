#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "launcher_path_policy.h"

#define TEST_SECTORS 4096U
#define SECTOR_SIZE 512U
static unsigned char disk[TEST_SECTORS * SECTOR_SIZE];
static FATFS fs;

static void check(int condition, const char *message)
{
	if(!condition)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

DSTATUS disk_initialize(BYTE pdrv) { return (pdrv == 0) ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv) { return (pdrv == 0) ? 0 : STA_NOINIT; }

DRESULT disk_read(BYTE pdrv, BYTE *buffer, LBA_t sector, UINT count)
{
	if((pdrv != 0) || (sector > TEST_SECTORS) || (count > TEST_SECTORS - sector))
		return RES_PARERR;
	memcpy(buffer, disk + (size_t)sector * SECTOR_SIZE, (size_t)count * SECTOR_SIZE);
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buffer, LBA_t sector, UINT count)
{
	if((pdrv != 0) || (sector > TEST_SECTORS) || (count > TEST_SECTORS - sector))
		return RES_PARERR;
	memcpy(disk + (size_t)sector * SECTOR_SIZE, buffer, (size_t)count * SECTOR_SIZE);
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE command, void *buffer)
{
	if(pdrv != 0)
		return RES_PARERR;
	switch(command)
	{
		case CTRL_SYNC: return RES_OK;
		case GET_SECTOR_COUNT: *(LBA_t*)buffer = TEST_SECTORS; return RES_OK;
		case GET_SECTOR_SIZE: *(WORD*)buffer = SECTOR_SIZE; return RES_OK;
		case GET_BLOCK_SIZE: *(DWORD*)buffer = 1; return RES_OK;
		default: return RES_PARERR;
	}
}

DWORD get_fattime(void)
{
	return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

static void mount_disk(void)
{
	check(f_mount(&fs, "0:", 1) == FR_OK, "mount the in-memory FAT volume");
}

static void format_disk(void)
{
	MKFS_PARM options;
	unsigned char work[4096];
	f_mount(NULL, "0:", 0);
	memset(&options, 0, sizeof(options));
	options.fmt = FM_FAT | FM_SFD;
	check(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK, "format the in-memory FAT volume");
	mount_disk();
}

static void write_file(const char *path, const char *contents)
{
	FIL file;
	UINT written = 0;
	UINT length = (UINT)strlen(contents);
	check(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "create a test file by LFN");
	check(f_write(&file, contents, length, &written) == FR_OK && written == length, "write test file contents");
	check(f_close(&file) == FR_OK, "close a test file");
}

static void find_alias_in_directory(const char *path, const char *long_name,
	char *alias, size_t alias_size)
{
	DIR dir;
	FILINFO info;
	int found = 0;
	check(f_opendir(&dir, path) == FR_OK, "open directory for alias lookup");
	while(f_readdir(&dir, &info) == FR_OK && info.fname[0])
	{
		if(strcmp(info.fname, long_name) == 0)
		{
			size_t length = strlen(info.altname);
			check(length > 0 && length < alias_size, "FatFs supplies a bounded SFN alias for the long name");
			memcpy(alias, info.altname, length + 1);
			found = 1;
			break;
		}
	}
	check(f_closedir(&dir) == FR_OK, "close root directory after alias lookup");
	check(found, "find the long-name entry");
}

static void find_alias(const char *long_name, char *alias, size_t alias_size)
{
	find_alias_in_directory("0:/", long_name, alias, alias_size);
}

static void find_name_by_alias(const char *alias, char *long_name, size_t name_size)
{
	DIR dir;
	FILINFO info;
	int found = 0;
	check(f_opendir(&dir, "0:/") == FR_OK, "open root directory for LFN resolution");
	while(f_readdir(&dir, &info) == FR_OK && info.fname[0])
	{
		if(strcmp(info.altname, alias) == 0)
		{
			size_t length = strlen(info.fname);
			check(length < name_size, "resolved LFN fits the caller's output buffer");
			memcpy(long_name, info.fname, length + 1);
			found = 1;
			break;
		}
	}
	check(f_closedir(&dir) == FR_OK, "close root directory after LFN resolution");
	check(found, "resolve an SFN identity to its full LFN");
}

static void test_legacy_path_policy(void)
{
	char path[600];
	char component_a[99];
	char component_b[99];
	size_t used;

	memset(component_a, 'a', 98);
	component_a[98] = '\0';
	memset(component_b, 'b', 98);
	component_b[98] = '\0';
	snprintf(path, sizeof(path), "/%s/%s/", component_a, component_b);
	check(LauncherPathMayBeLegacyTruncated(path, 512), "reject malformed trailing slash in stored path");

	used = (size_t)snprintf(path, sizeof(path), "/%s/%s/", component_a, component_b);
	path[used - 1] = '\0';
	check(!LauncherPathMayBeLegacyTruncated(path, 256), "allow a short path whose components fit the old name field");

	memset(path, 0, sizeof(path));
	path[0] = '/';
	memset(path + 1, 'a', 99);
	path[100] = '\0';
	check(LauncherPathMayBeLegacyTruncated(path, 512), "reject a component at the old 99-byte truncation boundary");

	memset(path, 0, sizeof(path));
	path[0] = '/';
	memset(path + 1, 'a', 98);
	path[99] = '/';
	memset(path + 100, 'b', 98);
	path[198] = '/';
	memset(path + 199, 'c', 55);
	path[254] = '\0';
	check(!LauncherPathMayBeLegacyTruncated(path, 256), "allow a 254-byte favorite path with short components");
	path[254] = 'c';
	path[255] = '\0';
	check(LauncherPathMayBeLegacyTruncated(path, 256), "reject a favorite path at its old 255-byte capacity");
}

static void test_maximum_save_backup_path(void)
{
	static const char backup_dir[] = "/SYSTEM/BACKUP/SAVER";
	char save_name[256];
	char backup_path[256 + LAUNCHER_SAVE_BACKUP_PATH_EXTRA];
	char generation_zero[sizeof(backup_path)];
	char generation_one[sizeof(backup_path)];
	char ordinary_generation[sizeof(backup_path)];
	char emulator_generation[sizeof(backup_path)];
	char small_buffer[16];
	FIL file;
	FILINFO info;
	UINT written = 0;
	const char contents[] = "save";
	size_t dir_length = strlen(backup_dir);
	size_t path_length;

	check(f_mkdir("/SYSTEM") == FR_OK, "create the system directory");
	check(f_mkdir("/SYSTEM/BACKUP") == FR_OK, "create the backup directory");
	check(f_mkdir(backup_dir) == FR_OK, "create the save-backup directory");

	memset(save_name, 'a', 251);
	memcpy(save_name + 251, ".sav", 5);
	check(strlen(save_name) == 255, "construct a maximum-length save basename");
	check(LauncherBuildSaveBackupPath(backup_dir, save_name,
		backup_path, sizeof(backup_path)), "build backup path for a 255-byte save basename");
	path_length = strlen(backup_path);
	check(path_length == dir_length + 1 + 255, "backup path contains the full directory and basename");
	check(strcmp(backup_path + dir_length + 1, save_name) == 0,
		"backup path preserves the complete save basename");
	check(LauncherBuildSaveBackupGenerationPath(backup_dir, save_name, '0',
		generation_zero, sizeof(generation_zero)), "build generation zero for a maximum-length save name");
	check(LauncherBuildSaveBackupGenerationPath(backup_dir, save_name, '1',
		generation_one, sizeof(generation_one)), "build generation one for a maximum-length save name");
	check(strlen(generation_zero) == path_length && strlen(generation_one) == path_length,
		"maximum-length generation names remain at the FatFs component limit");
	check(strcmp(generation_zero + path_length - 4, ".sa0") == 0 &&
		strcmp(generation_one + path_length - 4, ".sa1") == 0,
		"maximum-length .sav backups encode rotation without extending the component");
	check(LauncherBuildSaveBackupGenerationPath(backup_dir, "ordinary.sav", '0',
		ordinary_generation, sizeof(ordinary_generation)), "build the ordinary save backup path");
	check(strcmp(ordinary_generation, "/SYSTEM/BACKUP/SAVER/ordinary.sav0") == 0,
		"ordinary backup names keep their existing appended-digit convention");
	check(LauncherBuildSaveBackupGenerationPath(backup_dir, "ordinary.esv", '4',
		emulator_generation, sizeof(emulator_generation)), "build the ordinary emulator-save backup path");
	check(strcmp(emulator_generation, "/SYSTEM/BACKUP/SAVER/ordinary.esv4") == 0,
		"ordinary emulator-save backup names keep their existing rotation convention");
	memset(small_buffer, 'x', sizeof(small_buffer));
	check(!LauncherBuildSaveBackupGenerationPath(backup_dir, save_name, '0',
		small_buffer, sizeof(small_buffer)) && small_buffer[0] == '\0',
		"reject an undersized generation-path buffer without leaving partial output");

	check(f_open(&file, generation_zero, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK,
		"FatFs opens the maximum-length numbered backup under the nested path");
	check(f_write(&file, contents, sizeof(contents) - 1, &written) == FR_OK &&
		written == sizeof(contents) - 1, "write the maximum-path backup fixture");
	check(f_close(&file) == FR_OK, "close the maximum-path backup fixture");
	check(f_rename(generation_zero, generation_one) == FR_OK,
		"rotate the maximum-length backup to the next generation");
	check(f_stat(generation_zero, &info) == FR_NO_FILE && f_stat(generation_one, &info) == FR_OK,
		"FatFs finds the rotated maximum-length generation and no stale source");

	memset(save_name, 'a', 251);
	memcpy(save_name + 251, ".esv", 5);
	check(LauncherBuildSaveBackupGenerationPath(backup_dir, save_name, '0',
		generation_zero, sizeof(generation_zero)), "build generation zero for a maximum-length emulator save");
	check(strcmp(generation_zero + strlen(generation_zero) - 4, ".es0") == 0,
		"maximum-length .esv backups keep the component bounded");
}

static void test_long_name_alias_operations(void)
{
	char exact_name[100];
	char long_name[128];
	char long_path[160];
	char resolved_name[128];
	char alias[13];
	char exact_path[128];
	char alias_path[32];
	FIL file;
	FILINFO info;

	memset(exact_name, 'a', 95);
	memcpy(exact_name + 95, ".TXT", 5);
	snprintf(long_name, sizeof(long_name), "%s_extra.BIN", exact_name);
	snprintf(exact_path, sizeof(exact_path), "0:/%s", exact_name);
	snprintf(long_path, sizeof(long_path), "0:/%s", long_name);

	write_file(exact_path, "short");
	write_file(long_path, "longer");
	find_alias(long_name, alias, sizeof(alias));
	snprintf(alias_path, sizeof(alias_path), "0:/%s", alias);

	check(f_open(&file, exact_path, FA_READ) == FR_OK, "the old 99-byte prefix opens the exact sibling");
	check(f_size(&file) == 5, "the prefix resolved to the shorter sibling's contents");
	check(f_close(&file) == FR_OK, "close the prefix lookup");
	check(f_unlink(exact_path) == FR_OK, "reproduce deletion by the old truncated prefix");
	check(f_stat(exact_path, &info) == FR_NO_FILE, "the old prefix deleted the exact shorter sibling");
	check(f_stat(alias_path, &info) == FR_OK, "the longer file remains after wrong-target deletion");

	write_file(exact_path, "short");
	find_alias(long_name, alias, sizeof(alias));
	snprintf(alias_path, sizeof(alias_path), "0:/%s", alias);
	check(f_open(&file, alias_path, FA_READ) == FR_OK, "open the long file by its enumerated SFN alias");
	check(f_size(&file) == 6, "the alias resolved to the long file's contents");
	check(f_close(&file) == FR_OK, "close the alias lookup");
	find_name_by_alias(alias, resolved_name, sizeof(resolved_name));
	check(strcmp(resolved_name, long_name) == 0, "directory resolution returns the full LFN for recent-title display");
	check(f_unlink(alias_path) == FR_OK, "delete the long file by its enumerated SFN alias");
	check(f_stat(alias_path, &info) == FR_NO_FILE, "the alias target was removed");
	check(f_stat(exact_path, &info) == FR_OK, "the exact 99-byte sibling remains after alias deletion");
}

static void test_long_directory_title_resolution(void)
{
	static const char parent_name[] = "Outer Directory Long Name";
	static const char child_name[] = "Inner Folder Long Name";
	char parent_alias[13];
	char parent_alias_path[32];
	char child_path[96];
	DIR dir;
	FILINFO info;
	int found = 0;

	check(f_mkdir("0:/Outer Directory Long Name") == FR_OK,
		"create a parent directory with an LFN");
	find_alias(parent_name, parent_alias, sizeof(parent_alias));
	snprintf(parent_alias_path, sizeof(parent_alias_path), "0:/%s", parent_alias);
	snprintf(child_path, sizeof(child_path), "0:/%s/%s", parent_name, child_name);
	check(f_mkdir(child_path) == FR_OK, "create a nested directory with an LFN");
	check(f_opendir(&dir, parent_alias_path) == FR_OK,
		"open an LFN parent directory through the launcher's SFN-preserving path");
	while(f_readdir(&dir, &info) == FR_OK && info.fname[0])
	{
		if(strcmp(info.fname, child_name) == 0)
		{
			found = 1;
			break;
		}
	}
	check(f_closedir(&dir) == FR_OK, "close the nested LFN directory");
	check(found, "directory enumeration returns the full title-bar LFN");
}

int main(void)
{
	memset(disk, 0, sizeof(disk));
	format_disk();
	test_legacy_path_policy();
	test_long_name_alias_operations();
	test_long_directory_title_resolution();
	test_maximum_save_backup_path();
	puts("FatFs long-name identity scenarios passed.");
	return 0;
}
