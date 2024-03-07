#ifndef sd11_disa_h
#define sd11_disa_h

#include <3ds.h>

#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include <util.h>

#define PARTITIONA_SIZE 4153344

typedef struct disa_header  {
	char magic[4];
	u32 magic2;
	u32 part_count;
	u32 _pad;
	u64 sec_tbl_off;
	u64 prim_tbl_off;
	u64 tbl_size;
	u64 pa_desc_off;
	u64 pa_desc_size;
	u64 pb_desc_off;
	u64 pb_desc_size;
	u64 pa_off;
	u64 pa_size;
	u64 pb_off;
	u64 pb_size;
	u64 active_tbl;
	u32 hash[8];
	u8 unused[0x74];
} __attribute__((packed)) disa_header;

typedef struct difi_header {
	char magic[4];
	u32 magic2;
	// all relative to part desc off
	u64 ivfc_desc_off;
	u64 ivfc_desc_size;
	u64 dpfs_desc_off;
	u64 dpfs_desc_size;
	u64 part_hash_offs;
	u64 part_hash_size;
	u8 enable_ivfc_lvl4;
	u8 dpfs_lvl1_select;
	u16 pad;
	u64 external_ivfc_lvl4_off; // relative to part off
} __attribute__((packed)) difi_header;

typedef struct dpfs_level {
	u64 offset; // relative to partition begin
	u64 size;
	u32 log2_blocksize;
	u32 pad;
} __attribute__((packed)) dpfs_level;

typedef struct dpfs_header {
	char magic[4];
	u32 magic2;
	// offsets rel to partition begin
	dpfs_level lvl1;
	dpfs_level lvl2;
	dpfs_level lvl3;
} __attribute__((packed)) dpfs_header;

Result disa_extract_partition_a(Handle disa, Handle out_pa, u32 *partition_coun);

#endif
