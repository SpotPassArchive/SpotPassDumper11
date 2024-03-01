#include <disa.h>

static inline u8 bit(u32 *bitarr, u32 bi) {
	return (bitarr[bi / 32] >> (31 - (bi % 32))) & 1;
}

static inline u8 bit_select(u32 *bitarr, u32 bi, u64 lvlsize, u8 select) {
	return bit(&bitarr[select * lvlsize / 4], bi);
}

#define pow2(x) (1 << ((x)))
#define ERR_EXIT(msg) { printf("%s: %08lX\n", msg, res); goto exit; }

Result disa_extract_partition_a(Handle disa, Handle out_pa, u32 *partition_count) {
	u8 *buf = NULL, *dpfs_desc = NULL, *active_tbl = NULL;
	u32 *lvl1 = NULL, *lvl2 = NULL;
	u32 read = 0;

	disa_header hdr;

	Result res = -1;

	if (R_FAILED(res = FSFILE_Read(disa, &read, 0x100, &hdr, sizeof(disa_header))))
		ERR_EXIT("failed reading header");

	*partition_count = hdr.part_count;

	// partition table = partition descriptor

	active_tbl = (u8 *)malloc(hdr.tbl_size);
	u64 active_table_off = hdr.active_tbl ? hdr.sec_tbl_off : hdr.prim_tbl_off;

	if (R_FAILED(res = FSFILE_Read(disa, &read, active_table_off, active_tbl, hdr.tbl_size)))
		ERR_EXIT("failed reading active partition table");

	// this is for partitionA

	difi_header *difi = (difi_header *)active_tbl; // need for dpfs and ivfc

	u64 dpfs_desc_off = active_table_off + difi->dpfs_desc_off;
	dpfs_desc = (u8 *)malloc(difi->dpfs_desc_size);

	if (R_FAILED(res = FSFILE_Read(disa, &read, dpfs_desc_off, dpfs_desc, difi->dpfs_desc_size)))
		ERR_EXIT("failed reading DPFS descriptor");

	dpfs_header *dpfs = (dpfs_header *)dpfs_desc;

	// we can read lvl1 and lvl2 entirely; they shouldn't be that large
	u64 actual_lv1_off = (hdr.pa_off + dpfs->lvl1.offset) + (difi->dpfs_lvl1_select * dpfs->lvl1.size);
	u64 actual_lv2_off = hdr.pa_off + dpfs->lvl2.offset;

	lvl1 = (u32 *)malloc(dpfs->lvl1.size * 2);

	if (R_FAILED(res = FSFILE_Read(disa, &read, actual_lv1_off, lvl1, dpfs->lvl1.size * 2)))
		ERR_EXIT("failed reading DPFS lvl1");

	lvl2 = (u32 *)malloc(dpfs->lvl2.size * 2);

	if (R_FAILED(res = FSFILE_Read(disa, &read, actual_lv2_off, lvl2, dpfs->lvl2.size * 2)))
		ERR_EXIT("failed reading DPFS lvl2");

	buf = (u8 *)malloc(pow2(dpfs->lvl3.log2_blocksize));

	u64 pa_size = dpfs->lvl3.size - 0x9000;
	u64 written = 0;

	if (R_FAILED(res = FSFILE_SetSize(out_pa, pa_size)))
		ERR_EXIT("failed setting output file size");

	printf("extracting partitionA.bin: 0/%lld", pa_size);

	for (u64 off = 0x9000; off < dpfs->lvl3.size; off += pow2(dpfs->lvl3.log2_blocksize)) {
		u64 lvl2bitidx = off / pow2(dpfs->lvl3.log2_blocksize);
		u64 lvl2idx = lvl2bitidx / 8;
		u64 lvl1bitidx = lvl2idx / pow2(dpfs->lvl2.log2_blocksize);

		u64 actual_lv3_off = hdr.pa_off + dpfs->lvl3.offset;

		u8 lv2_select = bit(lvl1, lvl1bitidx);
		u8 lv3_select = bit_select(lvl2, lvl2bitidx, dpfs->lvl2.size, lv2_select);

		if (R_FAILED(res = FSFILE_Read(disa, &read, (actual_lv3_off + (dpfs->lvl3.size * lv3_select)) + off, buf, pow2(dpfs->lvl3.log2_blocksize))))
			ERR_EXIT("failed reading DISA lv3 data chunk");

		if (R_FAILED(res = FSFILE_Write(out_pa, &read, off - 0x9000, buf, pow2(dpfs->lvl3.log2_blocksize), FS_WRITE_FLUSH)))
			ERR_EXIT("failed writing DISA lv3 data chunk");

		written += read;

		printf("\rextracting partitionA.bin: %lld/%lld", written, pa_size);
	}
	printf("\n");

exit:
	if (buf) free(buf);
	if (dpfs_desc) free(dpfs_desc);
	if (active_tbl) free(active_tbl);
	if (lvl1) free(lvl1);
	if (lvl2) free(lvl2);
	return res;
}
#undef pow2
#undef ERR_EXIT
