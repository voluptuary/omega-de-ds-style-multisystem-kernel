#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "launcher_path_policy.h"
#include "launcher_artwork_path.h"
#include "launcher_indexed_name.h"

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

static int resolve_artwork_directory_component(void *context,
	const char *parent_path, const char *component,
	char *resolved_name, size_t resolved_name_capacity)
{
	char fatfs_parent[288];
	DIR dir;
	FILINFO info;
	int found = 0;
	size_t length;

	(void)context;
	if(!resolved_name || !resolved_name_capacity ||
		snprintf(fatfs_parent, sizeof(fatfs_parent), "0:%s", parent_path) >= (int)sizeof(fatfs_parent) ||
		f_opendir(&dir, fatfs_parent) != FR_OK)
		return 0;
	while(f_readdir(&dir, &info) == FR_OK && info.fname[0])
	{
		if(strcmp(component, info.fname) != 0 &&
			(!info.altname[0] || strcmp(component, info.altname) != 0))
			continue;
		if(found || !(info.fattrib & AM_DIR))
		{
			found = 0;
			break;
		}
		length = strlen(info.fname);
		if(length >= resolved_name_capacity)
			break;
		memcpy(resolved_name, info.fname, length + 1);
		found = 1;
	}
	check(f_closedir(&dir) == FR_OK, "close directory after artwork path resolution");
	return found;
}

static void test_mirrored_artwork_directory_resolution(void)
{
	static const char long_directory[] = "Game Boy Color";
	static const char nested_directory[] = "Commercial Releases";
	char directory_alias[13];
	char nested_alias[13];
	char artwork_alias[13];
	char artwork_nested_alias[13];
	char alias_rom_dir[64];
	char resolved_rom_dir[256];
	char resolved_name[256];
	char mirror_path[384];
	char alias_mirror_path[384];
	char legacy_art_parent[256];
	char legacy_art_dir[256];
	char legacy_art_path[384];
	char short_resolved_path[256];
	FILINFO info;
	int written;

	check(f_mkdir("0:/ROMS") == FR_OK, "create the ROM root directory");
	check(f_mkdir("0:/ROMS/Game Boy Color Alt") == FR_OK,
		"create a colliding GBC-like directory before the target");
	check(f_mkdir("0:/ROMS/Game Boy Color") == FR_OK,
		"create a Game Boy Color directory with an LFN");
	find_alias_in_directory("0:/ROMS", long_directory, directory_alias,
		sizeof(directory_alias));
	check(f_mkdir("0:/ROMS/Game Boy Color/Commercial Releases Alt") == FR_OK,
		"create a colliding nested directory before the target");
	check(f_mkdir("0:/ROMS/Game Boy Color/Commercial Releases") == FR_OK,
		"create a nested long-name ROM directory");
	find_alias_in_directory("0:/ROMS/Game Boy Color", nested_directory,
		nested_alias, sizeof(nested_alias));
	check(snprintf(alias_rom_dir, sizeof(alias_rom_dir), "/ROMS/%s/%s",
		directory_alias, nested_alias) <
		(int)sizeof(alias_rom_dir), "build the launcher's SFN-based ROM directory path");
	check(LauncherArtworkResolveDirectoryPath(alias_rom_dir,
		resolved_rom_dir, sizeof(resolved_rom_dir), resolved_name, sizeof(resolved_name),
		resolve_artwork_directory_component, 0),
		"resolve every SFN directory component for mirrored artwork");
	check(strcmp(resolved_rom_dir, "/ROMS/Game Boy Color/Commercial Releases") == 0,
		"all artwork path components use their visible LFNs");

	check(f_mkdir("0:/ARTWORK") == FR_OK, "create the isolated artwork fixture directory");
	check(f_mkdir("0:/ARTWORK/IMGS") == FR_OK, "create the wide artwork root");
	check(f_mkdir("0:/ARTWORK/IMGS/ROMS") == FR_OK,
		"create the mirrored ROM root directory");
	check(f_mkdir("0:/ARTWORK/IMGS/ROMS/Game Boy Color") == FR_OK,
		"create the mirrored LFN artwork directory");
	check(f_mkdir("0:/ARTWORK/IMGS/ROMS/Game Boy Color/Commercial Releases") == FR_OK,
		"create the nested mirrored LFN artwork directory");
	find_alias_in_directory("0:/ARTWORK/IMGS/ROMS", long_directory,
		artwork_alias, sizeof(artwork_alias));
	find_alias_in_directory("0:/ARTWORK/IMGS/ROMS/Game Boy Color", nested_directory,
		artwork_nested_alias, sizeof(artwork_nested_alias));
	check(strcmp(directory_alias, artwork_alias) != 0 &&
		strcmp(nested_alias, artwork_nested_alias) != 0,
		"colliding siblings make the ROM and artwork trees generate different SFN aliases");
	written = snprintf(mirror_path, sizeof(mirror_path),
		"0:/ARTWORK/IMGS%s/Oracle of Ages.bmp", resolved_rom_dir);
	check(written > 0 && (size_t)written < sizeof(mirror_path),
		"build the LFN mirrored-artwork file path");
	write_file(mirror_path, "art");
	written = snprintf(alias_mirror_path, sizeof(alias_mirror_path),
		"0:/ARTWORK/IMGS/ROMS/%s/%s/Oracle of Ages.bmp",
		directory_alias, nested_alias);
	check(written > 0 && (size_t)written < sizeof(alias_mirror_path),
		"build the pre-fix SFN mirrored-artwork file path");
	check(f_stat(alias_mirror_path, &info) != FR_OK,
		"the original SFN path misses artwork stored under the LFN mirror");
	check(f_stat(mirror_path, &info) == FR_OK,
		"the canonical LFN path finds the mirrored Game Boy Color artwork");

	check(f_mkdir("0:/ROMS/GB") == FR_OK, "create a short-name GB directory");
	check(LauncherArtworkResolveDirectoryPath("/ROMS/GB",
		short_resolved_path, sizeof(short_resolved_path), resolved_name, sizeof(resolved_name),
		resolve_artwork_directory_component, 0),
		"resolve an existing short-name ROM directory");
	check(strcmp(short_resolved_path, "/ROMS/GB") == 0,
		"short-name artwork paths remain unchanged by canonicalization");

	check(snprintf(legacy_art_parent, sizeof(legacy_art_parent),
		"0:/ARTWORK/IMGS/ROMS/%s", directory_alias) <
		(int)sizeof(legacy_art_parent),
		"build the first-level legacy SFN artwork directory path");
	check(f_mkdir(legacy_art_parent) == FR_OK,
		"create a legacy SFN-style first-level artwork directory");
	check(snprintf(legacy_art_dir, sizeof(legacy_art_dir),
		"%s/%s", legacy_art_parent, nested_alias) <
		(int)sizeof(legacy_art_dir),
		"build a legacy SFN-style artwork directory path");
	check(f_mkdir(legacy_art_dir) == FR_OK,
		"create an SFN-style legacy artwork directory");
	written = snprintf(legacy_art_path, sizeof(legacy_art_path),
		"%s/Legacy Artwork.bmp", legacy_art_dir);
	check(written > 0 && (size_t)written < sizeof(legacy_art_path),
		"build a legacy SFN artwork path");
	write_file(legacy_art_path, "legacy");
	check(f_stat(legacy_art_path, &info) == FR_OK,
		"the legacy SFN-style mirror candidate remains accessible");
}

typedef struct
{
	const char *path;
	unsigned int resolver_calls;
	unsigned int entries_examined;
} IndexedNameMetrics;

static int resolve_indexed_name(void *opaque, const char *identity,
	char *out_name, size_t out_name_capacity)
{
	IndexedNameMetrics *metrics = (IndexedNameMetrics*)opaque;
	DIR dir;
	FILINFO info;
	FRESULT result;
	size_t length;
	int found = 0;
	int ambiguous = 0;

	metrics->resolver_calls++;
	if(!metrics->path || !identity || !out_name || !out_name_capacity ||
		f_opendir(&dir, metrics->path) != FR_OK)
		return 0;
	while((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0])
	{
		metrics->entries_examined++;
		if(strcmp(identity, info.fname) != 0 &&
			(!info.altname[0] || strcmp(identity, info.altname) != 0))
			continue;
		if(found && strcmp(out_name, info.fname) != 0)
		{
			ambiguous = 1;
			break;
		}
		if(!found)
		{
			length = strlen(info.fname);
			if(length >= out_name_capacity)
				break;
			memcpy(out_name, info.fname, length + 1);
			found = 1;
		}
	}
	if(f_closedir(&dir) != FR_OK)
		return 0;
	return found && !ambiguous && result == FR_OK;
}

static unsigned int run_indexed_name_performance_case(unsigned int file_count,
	const char *directory_path)
{
	static const unsigned int clipped_name_flag = 1U; /* FM_ENTRY_ALIAS_DISPLAY */
	char path[128];
	char filename[128];
	char long_name[128];
	char truncated_name[100];
	char alias[13];
	char resolved_name[256];
	DIR dir;
	FILINFO info;
	FRESULT result;
	IndexedNameMetrics metrics;
	unsigned int created;
	unsigned int indexed = 0;
	int written;

	check(f_mkdir(directory_path) == FR_OK, "create an indexed-name performance folder");
	for(created = 0; created < file_count; created++)
	{
		written = snprintf(filename, sizeof(filename), "Game Collection Item %03u.gba", created);
		check(written > 0 && (size_t)written < sizeof(filename), "format a performance-fixture game name");
		written = snprintf(path, sizeof(path), "%s/%s", directory_path, filename);
		check(written > 0 && (size_t)written < sizeof(path), "format a performance-fixture game path");
		write_file(path, "rom");
	}

	strcpy(long_name, "Long Performance Entry ");
	memset(long_name + strlen(long_name), 'x', 88);
	strcpy(long_name + strlen("Long Performance Entry ") + 88, ".gba");
	written = snprintf(path, sizeof(path), "%s/%s", directory_path, long_name);
	check(written > 0 && (size_t)written < sizeof(path), "format the clipped performance-fixture path");
	write_file(path, "long rom");
	find_alias_in_directory(directory_path, long_name, alias, sizeof(alias));

	memset(&metrics, 0, sizeof(metrics));
	metrics.path = directory_path;
	check(f_opendir(&dir, directory_path) == FR_OK, "open performance fixture for index enumeration");
	while((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0])
	{
		if(strcmp(info.fname, long_name) == 0)
			continue;
		check(LauncherCopyIndexedNameOrResolve(info.fname, 0, clipped_name_flag,
			info.altname[0] ? info.altname : info.fname, resolved_name,
			sizeof(resolved_name), resolve_indexed_name, &metrics),
			"copy a complete indexed LFN without rescanning its directory");
		check(strcmp(resolved_name, info.fname) == 0,
			"the thumbnail fast path preserves the indexed LFN");
		indexed++;
	}
	check(f_closedir(&dir) == FR_OK, "close performance fixture after index enumeration");
	check(result == FR_OK && indexed == file_count,
		"every ordinary performance-fixture filename was indexed");
	check(metrics.resolver_calls == 0 && metrics.entries_examined == 0,
		"complete indexed LFNs perform no per-thumbnail directory resolver scan");

	strncpy(truncated_name, long_name, sizeof(truncated_name) - 1);
	truncated_name[sizeof(truncated_name) - 1] = '\0';
	check(strlen(long_name) >= sizeof(truncated_name) - 1,
		"performance fixture includes an LFN clipped by the 99-byte index");
	check(LauncherCopyIndexedNameOrResolve(truncated_name, clipped_name_flag,
		clipped_name_flag, alias, resolved_name, sizeof(resolved_name),
		resolve_indexed_name, &metrics),
		"clipped index names retain the alias-to-LFN resolver");
	check(strcmp(resolved_name, long_name) == 0,
		"the resolver restores the complete LFN for clipped entries");
	check(metrics.resolver_calls == 1 && metrics.entries_examined >= file_count,
		"only the clipped entry performs one ambiguity-checked directory scan");
	return metrics.entries_examined;
}

static void test_indexed_name_thumbnail_performance(void)
{
	unsigned int small_scan_entries = run_indexed_name_performance_case(16, "0:/PERF16");
	unsigned int large_scan_entries = run_indexed_name_performance_case(96, "0:/PERF96");
	check(large_scan_entries > small_scan_entries,
		"the retained long-name fallback scan reflects folder size");
	printf("indexed-name resolver scans: 16 files -> %u entries; 96 files -> %u entries; complete indexed LFNs -> 0 scans\n",
		small_scan_entries, large_scan_entries);
}

int main(void)
{
	memset(disk, 0, sizeof(disk));
	format_disk();
	test_legacy_path_policy();
	test_long_name_alias_operations();
	test_long_directory_title_resolution();
	test_mirrored_artwork_directory_resolution();
	test_maximum_save_backup_path();
	test_indexed_name_thumbnail_performance();
	puts("FatFs long-name identity scenarios passed.");
	return 0;
}
