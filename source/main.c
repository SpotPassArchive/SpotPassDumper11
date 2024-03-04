#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include <disa.h>
#include <upload.h>
#include <util.h>

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

#define T(x,msg) TRE((x), msg, exit);

static Result upload_dump(Handle file) {
	Result res = 0xE7E3FFFF;

	T(upload_init(), "Failed initializing upload");
	printf("Connecting to the internet...\n");
	T(upload_connect(), "Failed connecting to wifi");
	T(upload_conn_test(), "Internet connectivity is not present");
	T(upload_partition_a(file), "Failed uploading dump");

	printf("\x1b[32m\nFile uploaded successfully. Thank you!\x1b[0m\n\n");

exit:
	upload_exit();
	return res;
}

static Result dump_boss_data() {
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
	Result res = 0xE7E3FFFF;

	T(mount_media(&nand, ARCHIVE_NAND_CTR_FS), "Failed mounting nand");
	T(mount_media(&sdmc, ARCHIVE_SDMC), "Failed mounting SD");
	T(open_dir(&nand, &data, u"/data"), "Failed opening nand:/data");
	T(read_dir(data, &ents, 4, &read), "Failed listing contents of nand:/data");
	T(FSDIR_Close(data), "Failed closing directory");

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

	T(file_open(&nand, &disa_file, buf), "Failed opening BOSS DISA file for reading");
	T(make_dir(&sdmc, "/spotpass_cache", true), "Failed creating output directory on SD card. Make sure SD card is not set to read-only");
	T(file_open_write(&sdmc, &out_pa, "/spotpass_cache/partitionA.bin"), "Failed opening sd:/spotpass_cache/partitionA.bin");

	u32 part_count = 0;

	T(disa_extract_partition_a(disa_file, out_pa, &part_count), "Failed copying file to SD card. Make sure SD card is not set to read-only");

	printf("\x1b[32m\nCompleted successfully.\x1b[0m\n\n");
	printf("File dumped to: sd:/spotpass_cache/partitionA.bin\n\n");

	// try uploading the file
	printf("Attempting to upload...\n");

	if (R_FAILED(upload_dump(out_pa)))
		printf("\x1b[31m\nFailed uploading file. Please upload it\nyourself using the website.\x1b[0m\n\n");

	printf("Partition count: %ld\n", part_count);

	if (part_count > 1) {
		printf("\x1b[33m\nYou have partitionB!\x1b[0m\n\n");

		T(file_open_write(&sdmc, &out_disa, "/spotpass_cache/00000000"), "Failed opening sd:/spotpass_cache/00000000");
		T(file_copy(disa_file, out_disa), "Failed copying file to SD card. Make sure SD card is not set to read-only")

		printf("File dumped to: sd:/spotpass_cache/00000000\n");

		printf(
			"\x1b[32m\n\nHaving partitionB is rare. Please get\n"
			"in touch with us on Discord so that we\n"
			"can analyze your data further:\n\n"
			"\x1b[36mhttps://discord.gg/wxCEY8MHvh\x1b[0m\n\n");
	}

	FSFILE_Close(disa_file);
	FSFILE_Close(out_pa);
	FSFILE_Close(out_disa);
	disa_file = out_pa = out_disa = 0;

	FSUSER_CloseArchive(nand);
	FSUSER_CloseArchive(sdmc);
	nand = sdmc = 0;

exit:
	if (ents) free(ents);
	if (data) { FSDIR_Close(data); svcCloseHandle(data); }
	if (disa_file) { FSFILE_Close(disa_file); }
	if (out_pa) { FSFILE_Close(out_pa); }
	if (out_disa) { FSFILE_Close(out_disa); }
	if (nand) FSUSER_CloseArchive(nand);
	if (sdmc) FSUSER_CloseArchive(sdmc);

	return res;
}

int main(int argc, char* argv[])
{
	fsInit();
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	// TODO: detect existing file, upload, redump, upload again
	dump_boss_data();

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
