#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include <malloc.h>

#include <disa.h>
#include <upload.h>
#include <util.h>

#define T(x,msg) TRE((x), msg, exit);

#define DUMP_OUTPATH "/spotpass_cache"
#define FILE_COPY_BUFSIZE 0x10000

static Result mount_media(FS_Archive *archive, u32 archive_id) {
	return FSUSER_OpenArchive(archive, archive_id, fsMakePath(PATH_EMPTY, ""));
}

static Result file_open(FS_Archive *archive, Handle *file, const char *path, bool write) {
	u32 open_flags = write ? FS_OPEN_CREATE | FS_OPEN_WRITE : FS_OPEN_READ;
	return FSUSER_OpenFile(file, *archive, fsMakePath(PATH_ASCII, path), open_flags, 0);
}

static Result file_copy(Handle input, Handle output) {
	u64 filesize = 0;
	Result res = -1;
	u8 *buf = NULL;

	if (!(buf = (u8 *)malloc(FILE_COPY_BUFSIZE)))
		return MAKERESULT(RL_FATAL, RS_INVALIDSTATE, RM_OS, RD_OUT_OF_MEMORY);

	if (R_FAILED(res = FSFILE_GetSize(input, &filesize))) goto bad_exit;
	if (R_FAILED(res = FSFILE_SetSize(output, filesize))) goto bad_exit;

	u64 remain = filesize;
	u64 to_read = MIN(FILE_COPY_BUFSIZE, remain);
	u64 offset = 0;
	u32 read = 0, written = 0;

	printf("Copying file... (" CONSOLE_YELLOW "%.02f%%" CONSOLE_RESET ")", PERCENTAGE(offset, filesize));

	while (remain) {
		if (R_FAILED(res = FSFILE_Read(input, &read, offset, buf, to_read))) goto bad_exit;
		if (R_FAILED(res = FSFILE_Write(output, &written, offset, buf, read, FS_WRITE_FLUSH))) goto bad_exit;

		remain -= read;
		offset += read;
		to_read = MIN(FILE_COPY_BUFSIZE, remain);
		printf("\rCopying file... (" CONSOLE_YELLOW "%.02f%%" CONSOLE_RESET ")", PERCENTAGE(offset, filesize));
	}

	putchar('\n');

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


static Result upload_dump(Handle file) {
	Result res = 0xE7E3FFFF;

	T(upload_init(), "Failed initializing upload");
	puts("Connecting to the internet...");
	bool disable_sslverify = false;
	T(upload_connect(), "Failed connecting to wifi");
	if (R_FAILED(res = upload_conn_test(disable_sslverify))) {
		if (res == UL_RES(UCURL_ERROR) && upload_get_err() == CURLE_PEER_FAILED_VERIFICATION)
		{ T(upload_conn_test(disable_sslverify = true), "Internet connectivity not present"); }
		else ERR_EXIT("Internet connectivity not present", exit);
	}
	T(upload_send_partition_a(file, disable_sslverify), "Failed uploading dump");

	puts("\n" CONSOLE_GREEN "File uploaded successfully. Thank you!" CONSOLE_RESET);

exit:
	upload_exit();
	return res;
}

static void dump_and_upload() {
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
				printf("ID0: " CONSOLE_MAGENTA "%s" CONSOLE_RESET "\n\n", utf8);
				snprintf(buf, sizeof(buf), "/data/%s/sysdata/00010034/00000000", utf8);
				found_id0 = true;
				break;
			}
		}
	}

	if (ents) { free(ents); ents = NULL; }

	if (!found_id0) {
		puts("Could not find ID0 on NAND."); // this should never happen
		goto exit;
	}

	T(file_open(&nand, &disa_file, buf, false), "Failed opening BOSS DISA file for\nreading");
	T(make_dir(&sdmc, DUMP_OUTPATH, true), "Failed creating output directory on\nSD card. Make sure SD card is not set\nto read-only");
	T(file_open(&sdmc, &out_pa, DUMP_OUTPATH "/partitionA.bin", true), "Failed opening:\nsd:" DUMP_OUTPATH "/partitionA.bin");

	u32 part_count = 0;

	T(disa_extract_partition_a(disa_file, out_pa, &part_count), "Failed copying file to SD card. Make\nsure SD card is not set to\nread-only");

	printf("DISA partition count: %ld\n", part_count);

	puts("\n" CONSOLE_GREEN "Copied successfully." CONSOLE_RESET "\n");
	puts("File dumped to:\n" CONSOLE_CYAN "sd:" DUMP_OUTPATH "/partitionA.bin" CONSOLE_RESET "\n");

	// try uploading the file
	puts("Attempting to upload dump...");

	if (R_FAILED(upload_dump(out_pa))) {
		puts("\n" CONSOLE_RED "Failed uploading file. Please upload it\nyourself using the website." CONSOLE_RESET "\n\n");
		if (res == UL_RES(UCURL_ERROR))
			printf(CONSOLE_RED "CURL error information:\n%s" CONSOLE_RESET "\n", curl_easy_strerror(upload_get_err()));
	}

	if (part_count > 1) {
		puts("\n" CONSOLE_YELLOW "You have partitionB!" CONSOLE_RESET "\n");

		T(file_open(&sdmc, &out_disa, DUMP_OUTPATH "/00000000", true), "Failed opening:\nsd:" DUMP_OUTPATH "/00000000");
		T(file_copy(disa_file, out_disa), "Failed copying file to SD card. Make\nsure SD card is not set to\nread-only")

		puts("File dumped to:\n" CONSOLE_CYAN "sd:" DUMP_OUTPATH "/00000000" CONSOLE_RESET);

		puts(
			"\n\n" CONSOLE_GREEN
			"Having partitionB is rare. Please get\n"
			"in touch with us on Discord so that we\n"
			"can analyze your data further:\n\n"
			CONSOLE_CYAN "https://discord.gg/wxCEY8MHvh"
			CONSOLE_RESET "\n\n");
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
}

static void handle_existing_dump(bool *should_continue) {
	Result res = 0xE7E3FFFF;
	Handle existing = 0;
	u64 file_size = 0;
	u32 read = 0;

	if (R_FAILED(res = FSUSER_OpenFileDirectly(&existing, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, DUMP_OUTPATH "/partitionA.bin"), FS_OPEN_READ, 0))) {
		*should_continue = true;
		goto exit; /* no existing file */
	}

	T(FSFILE_GetSize(existing, &file_size), "Failed getting size of existing\ndump");
	if (file_size != PARTITIONA_SIZE) {
		*should_continue = true;
		goto exit; /* the file is not the correct size */
	}

	char magic[4];
	T(FSFILE_Read(existing, &read, 0, magic, 4), "Failed reading magic from\nexisting dump");
	if (memcmp(magic, "SAVE", 4) != 0) {
		*should_continue = true;
		goto exit; /* the file does not have the correct magic */
	}

	puts("Uploading existing dump...\n");
	res = upload_dump(existing);
	if (res == UL_RES(UCURL_ERROR))
		printf(CONSOLE_RED "CURL error information:\n%s" CONSOLE_RESET "\n", curl_easy_strerror(upload_get_err()));
	*should_continue = R_SUCCEEDED(res);
	T(res, "Failed uploading existing dump");
	puts("\n========================================");
exit:
	if (existing) FSFILE_Close(existing);
}

int main(int argc, char* argv[])
{
	fsInit();
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	bool should_continue = false;
	handle_existing_dump(&should_continue);

	if (!should_continue)
		puts(CONSOLE_YELLOW "Failed processing existing dump.\nWill not continue." CONSOLE_RESET);
	else 
		dump_and_upload();
	
	puts("\n\nPress START to exit.");

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

#undef T
