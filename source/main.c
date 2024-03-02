#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include <disa.h>

static const int bufsize = 0x10000;

static Result mount_media(FS_Archive *archive, u32 archive_id) {
	return FSUSER_OpenArchive(archive, archive_id, fsMakePath(PATH_EMPTY, ""));
}

static Result file_open(FS_Archive *archive, Handle *file, const char *path) {
	return FSUSER_OpenFile(file, *archive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
}

static Result file_open_write(FS_Archive *archive, Handle *file, const char *path) {
	return FSUSER_OpenFile(file, *archive, fsMakePath(PATH_ASCII, path), FS_OPEN_CREATE | FS_OPEN_WRITE, 0);
}

static Result file_copy(Handle input, Handle output) {
	u64 filesize = 0;
	Result res = -1;
	u8 *buf = (u8 *)malloc(bufsize);

	if (!buf) return MAKERESULT(RL_FATAL, RS_INVALIDSTATE, RM_OS, RD_OUT_OF_MEMORY);

	if (R_FAILED(res = FSFILE_GetSize(input, &filesize))) goto bad_exit;
	if (R_FAILED(res = FSFILE_SetSize(output, filesize))) goto bad_exit;

#define MIN(x,y) ((x) < (y) ? (x) : (y))

	u64 remain = filesize;
	u64 to_read = MIN(bufsize, remain);
	u64 offset = 0;
	u32 read = 0, written = 0;

	printf("Copying file... %lld/%lld bytes", offset, filesize);

	while (remain) {
		Result res = FSFILE_Read(input, &read, offset, buf, to_read);
		if (R_FAILED(res)) goto bad_exit;
		res = FSFILE_Write(output, &written, offset, buf, read, FS_WRITE_FLUSH);
		if (R_FAILED(res)) goto bad_exit;

		remain -= read;
		offset += read;
		to_read = MIN(bufsize, remain);
		printf("\rCopying file... %lld/%lld bytes", offset, filesize);
	}

	printf("\n");

	res = 0;

bad_exit:
	if (buf) free(buf);
	return res;
#undef MIN
}

static Result open_dir(FS_Archive *archive, Handle *out, const u16 *path) {
	return FSUSER_OpenDirectory(out, *archive, fsMakePath(PATH_UTF16, path));
}

static Result make_dir(FS_Archive *archive, const char *path, bool ignore_exists) {
	Result ret = FSUSER_CreateDirectory(*archive, fsMakePath(PATH_ASCII, path), 0);
	if (R_FAILED(ret) && ret == 0xC82044BE && ignore_exists) return 0;
	return ret;
}

static Result read_dir(Handle dir, FS_DirectoryEntry **ents, int max_count, u32 *read) {
	*ents = (FS_DirectoryEntry *)malloc(max_count * sizeof(FS_DirectoryEntry));
	return FSDIR_Read(dir, read, max_count, *ents);
}

#define ERR_EXIT(msg) { printf("\n\x1b[31m%s: %08lX\x1b[0m\n", msg, res); goto exit; }

int main(int argc, char* argv[])
{
	fsInit();
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	bool found_id0 = false;
	char buf[65];

	FS_DirectoryEntry *ents = NULL;
	FS_Archive nand = 0LLU;
	FS_Archive sdmc = 0LLU;

	Handle data = 0;
	Handle disa_file = 0;
	Handle out_disa = 0;
	Handle out_pa = 0;

	u32 read = 0;
	Result res = -1;


	if (R_FAILED(res = mount_media(&nand, ARCHIVE_NAND_CTR_FS)))
		ERR_EXIT("Failed mounting nand.");

	if (R_FAILED(res = mount_media(&sdmc, ARCHIVE_SDMC)))
		ERR_EXIT("Failed mounting SD.");

	if (R_FAILED(res = open_dir(&nand, &data, u"/data")))
		ERR_EXIT("Failed opening nand:/data");

	if (R_FAILED(res = read_dir(data, &ents, 4, &read)))
		ERR_EXIT("Failed listing contents of nand:/data");

	if (R_FAILED(res = FSDIR_Close(data)))
		ERR_EXIT("Failed closing directory.");
	data = 0;

	if (read) {
		char utf8[33];
		utf8[32] = '\0';
		for (int i = 0; i < read; i++) {
			int r = utf16_to_utf8((uint8_t *)&utf8, ents[i].name, 33);
			if (r == 32) {
				printf("your ID0: %s\n", utf8);
				snprintf(buf, sizeof(buf), "/data/%s/sysdata/00010034/00000000", utf8);
				found_id0 = true;
				break;
			}
		}
	}

	if (ents) { free(ents); ents = NULL; }

	if (!found_id0) {
		printf("Could not find id0 on nand.\n"); // this should never happen
		goto exit;
	}

	if (R_FAILED(res = file_open(&nand, &disa_file, buf)))
		ERR_EXIT("Failed opening BOSS DISA file for reading.");

	if (R_FAILED(res = make_dir(&sdmc, "/spotpass_cache", true)))
		ERR_EXIT("Failed creating output directory on SD card. Make sure SD card is not set to read-only.");

	if (R_FAILED(res = file_open_write(&sdmc, &out_pa, "/spotpass_cache/partitionA.bin")))
		ERR_EXIT("Failed opening sdmc:/spotpass_cache/partitionA.bin");

	u32 part_count = 0;

	if (R_FAILED(res = disa_extract_partition_a(disa_file, out_pa, &part_count)))
		ERR_EXIT("Failed copying file to SD card. Make sure SD card is not set to read-only.");

	printf("File dumped to: sd:/spotpass_cache/partitionA.bin\n\n");

	printf("Partition count: %ld\n", part_count);

	if (part_count > 1) {
		printf("\x1b[33m\nYou have partitionB!\x1b[0m\n\n");
		if (R_FAILED(res = file_open_write(&sdmc, &out_disa, "/spotpass_cache/00000000")))
			ERR_EXIT("Failed opening sd:/spotpass_cache/00000000");

		if (R_FAILED(res = file_copy(disa_file, out_disa)))
			ERR_EXIT("Failed copying file to SD card. Make sure SD card is not set to read-only.")

		printf("File dumped to: sd:/spotpass_cache/00000000\n");

		printf(
			"\x1b[32m\n\nHaving partitionB is rare. Please get\n"
			"in touch with us on Discord so that we\n"
			"can analyze your data further:\n\n"
			"\x1b[36mhttps://discord.gg/wxCEY8MHvh\x1b[0m\n\n");
	}

#undef ERR_EXIT

	FSFILE_Close(disa_file);
	FSFILE_Close(out_pa);
	FSFILE_Close(out_disa);
	disa_file = out_pa = out_disa = 0;

	FSUSER_CloseArchive(nand);
	FSUSER_CloseArchive(sdmc);
	nand = sdmc = 0;

	printf("\x1b[32m\nCompleted successfully.\x1b[0m\n\n");

exit:
	if (ents) free(ents);
	if (data) { FSDIR_Close(data); svcCloseHandle(data); }
	if (disa_file) { FSDIR_Close(disa_file); svcCloseHandle(disa_file); }
	if (out_pa) { FSDIR_Close(out_pa); svcCloseHandle(out_pa); }
	if (out_disa) { FSDIR_Close(out_disa); svcCloseHandle(out_disa); }
	if (nand) FSUSER_CloseArchive(nand);
	if (sdmc) FSUSER_CloseArchive(sdmc);

	printf("Press START to exit\n");

	while (aptMainLoop())
	{
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break;
	}

	gfxExit();
	fsExit();
	return 0;
}
