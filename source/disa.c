#include <disa.h>

static inline Result read_bit(Handle file, u8 *outval, u64 lvloff, u32 bit) {
	u32 val, read;
	Result ret = FSFILE_Read(file, &read, lvloff + 4 * (bit / 32), &val, 4);
	if (R_FAILED(ret)) return ret;
	*outval = (val >> (31 - (bit % 32))) & 1;
	return ret;
}

static inline Result read_bit_select(Handle file, u8 *outval, u64 lvloff, u32 bit, u64 lvlsize, u32 select) {
	u32 val, read;
	Result ret = FSFILE_Read(file, &read, (lvloff + (lvlsize * select)) + 4 * (bit / 32), &val, 4);
	if (R_FAILED(ret)) return ret;
	*outval = (val >> (31 - (bit % 32))) & 1;
	return ret;
}

#define pow2(x) (1 << ((x)))
#define ERR_EXIT(msg) { printf("%s: %08lX\n", msg, res); goto exit; }

Result disa_extract_partition_a(Handle disa, Handle out_pa, u32 *partition_count) {
	u8 *buf = NULL, *dpfs_desc = NULL, *active_tbl = NULL;
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

		u64 actual_lv1_off = (hdr.pa_off + dpfs->lvl1.offset) + (difi->dpfs_lvl1_select * dpfs->lvl1.size);
		u64 actual_lv2_off = hdr.pa_off + dpfs->lvl2.offset;
		u64 actual_lv3_off = hdr.pa_off + dpfs->lvl3.offset;

		u8 lv2_select;
		if (R_FAILED(res = read_bit(disa, &lv2_select, actual_lv1_off, lvl1bitidx)))
			ERR_EXIT("failed reading lv1 bit");

		u8 lv3_select;
		if (R_FAILED(res = read_bit_select(disa, &lv3_select, actual_lv2_off, lvl2bitidx, dpfs->lvl2.size, lv2_select)))
			ERR_EXIT("failed reading lv2 bit");

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
	return res;
}
#undef pow2
#undef ERR_EXIT
