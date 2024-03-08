#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>

#include <3ds.h>

#include <disa.h>
#include <upload.h>
#include <util.h>

#define T(x,msg) TRE((x), msg, exit);

/* 40 chars max to fit on bottom screen */
#define SITE_URL "https://spotpassarchive.github.io"
#define UPLOAD_URL SITE_URL "/upload"
#define DISCORD_URL "https://discord.gg/wxCEY8MHvh"
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

static void dump_and_upload(bool *warn_user) {
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
		puts("Could not find ID0 on NAND."); /* this should never happen */
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
			CONSOLE_CYAN DISCORD_URL
			CONSOLE_RESET "\n\n");
	}

	/* try uploading the file */
	puts("Attempting to upload dump...");

	if (R_FAILED(res = upload_dump(out_pa))) {
		puts("\n" CONSOLE_RED "Failed uploading file." CONSOLE_RESET "\n");
		if (res == UL_RES(UCURL_ERROR))
			printf(CONSOLE_RED "CURL error information:\n%s" CONSOLE_RESET "\n", curl_easy_strerror(upload_get_err()));
		*warn_user = true;
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

static bool handle_existing_dump(bool *warn_user, time_t *existing_timestamp) {
	Result res = 0xE7E3FFFF;
	char baknamebuf[64];
	Handle existing = 0;
	FS_Archive sdmc = 0;
	u64 file_size = 0;
	u32 read = 0;

	if (R_FAILED(res = FSUSER_OpenFileDirectly(&existing, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, DUMP_OUTPATH "/partitionA.bin"), FS_OPEN_READ, 0))) {
		*warn_user = false;
		goto exit; /* no existing file */
	}

	T(FSFILE_GetSize(existing, &file_size), "Failed getting size of existing\ndump");
	if (file_size != PARTITIONA_SIZE) {
		*warn_user = false;
		goto exit; /* the file is not the correct size */
	}

	char magic[4];
	T(FSFILE_Read(existing, &read, 0, magic, 4), "Failed reading magic from\nexisting dump");
	if (memcmp(magic, "SAVE", 4) != 0) {
		*warn_user = false;
		goto exit; /* the file does not have the correct magic */
	}

	puts("Uploading existing dump...\n");
	res = upload_dump(existing);
	if (res == UL_RES(UCURL_ERROR))
		printf(CONSOLE_RED "CURL error information:\n%s" CONSOLE_RESET "\n", curl_easy_strerror(upload_get_err()));
	*warn_user = R_FAILED(res);
	TRE(res, "Failed uploading existing dump", ren_exit);
	puts("\n========================================");
exit:
	if (existing) FSFILE_Close(existing);
	return true;
ren_exit:
	if (existing) FSFILE_Close(existing);
	/* rename the file to partitionA_{timestamp}.bin */
	time_t t = time(NULL);
	snprintf(baknamebuf, sizeof(baknamebuf), DUMP_OUTPATH "/partitionA_%lld.bin", t);
	TRE(mount_media(&sdmc, ARCHIVE_SDMC), "Failed mounting SDMC archive", ren_err_exit);
	TRE(FSUSER_RenameFile(sdmc, fsMakePath(PATH_ASCII, DUMP_OUTPATH "/partitionA.bin"), sdmc, fsMakePath(PATH_ASCII, baknamebuf)), "Failed renaming to backup name\n", ren_err_exit);
	printf("Renamed existing file to:\n" CONSOLE_CYAN "sd:%s" CONSOLE_RESET "\n", baknamebuf);
	*existing_timestamp = t;
ren_err_exit:
	if (sdmc) FSUSER_CloseArchive(sdmc);
	return R_SUCCEEDED(res);
}

bool retry_upload_existing(time_t existing_timestamp) {
	Result res = 0xE7E3FFFF;
	Handle existing = 0;
	char baknamebuf[64];

	snprintf(baknamebuf, sizeof(baknamebuf), DUMP_OUTPATH "/partitionA_%lld.bin", existing_timestamp);
	T(FSUSER_OpenFileDirectly(&existing, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, baknamebuf), FS_OPEN_READ,0), "Failed opening existing backup file");
	T(upload_dump(existing), "Failed uploading existing backup file");
exit:
	if (existing) FSFILE_Close(existing);
	return R_SUCCEEDED(res);
}

static void print_warn_user() {
	puts(CONSOLE_YELLOW "Warning: Failed uploading dump(s).\n"
		"Please upload all partitionA files in\n"
		CONSOLE_MAGENTA "sd:" DUMP_OUTPATH CONSOLE_YELLOW
		" manually here:" CONSOLE_GREEN "\n"
		UPLOAD_URL CONSOLE_RESET "\n\n");
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	/* CFW check */

	Handle hbldr = 0;
	if R_FAILED(svcConnectToPort(&hbldr, "hb:ldr")) {
		puts(CONSOLE_RED "Luma3DS CFW is required to use this\n"
			"application. Emulators and stock\n"
			"firmware are not supported.\n\n" CONSOLE_GREEN
			"To dump your data on a stock system,\n"
			"go to the following website and use\n"
			"the " CONSOLE_CYAN "'No CFW'" CONSOLE_GREEN
			" guide corresponding\n"
			"to your system:\n\n" CONSOLE_MAGENTA
			SITE_URL CONSOLE_RESET "\n");

		goto exit;
	}
	svcCloseHandle(hbldr);

	bool warn_user_existing = false;
	bool warn_user_current = false;
	time_t existing_timestamp = 0;
	Result res = 0xE7E3FFFF;

	fsInit();
	aptInit();
	ndmuInit();

	/* disable HOME button & sleep mode & make sleep mode not break wifi */

	aptSetSleepAllowed(false);
	aptSetHomeAllowed(false);

	T(NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE), "Failed entering NDM exclusive state");
	T(NDMU_LockState(), "Failed locking NDM state");

	if (!handle_existing_dump(&warn_user_existing, &existing_timestamp)) {
		/* something is very wrong */
		puts(CONSOLE_RED "Failed handling existing dump.\n"
			"Please contact us on Discord so we can\n"
			"Find out what went wrong:\n" CONSOLE_CYAN
			DISCORD_URL CONSOLE_RESET "\n");
		goto exit;
	}

	dump_and_upload(&warn_user_current);

	if (warn_user_current) { /* current failed upload */
		print_warn_user();
		goto exit;
	}

	if (warn_user_existing) { /* existing failed upload but current did not */
		puts("Attempting to upload the existing\nfile again...");
		if (!retry_upload_existing(existing_timestamp))
			print_warn_user();
	}

exit:
	/* unlock NDM state, sleep mode and home button */
	NDMU_UnlockState();
	NDMU_LeaveExclusiveState();
	aptSetHomeAllowed(true);
	aptSetSleepAllowed(true);
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
	ndmuExit();
	aptExit();
	return 0;
}

#undef T
