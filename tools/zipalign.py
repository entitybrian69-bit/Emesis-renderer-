#!/usr/bin/env python3
"""
Pure-Python APK zipalign tool supporting:
- 4-byte alignment for uncompressed resources.arsc and PNG assets (required for targetSdkVersion >= 30)
- 16KB (16384-byte) page alignment for uncompressed .so files if stored, or standard compressed .so when extractNativeLibs="true"
"""
import struct
import sys
import zipfile


def align_apk(src_path: str, dst_path: str) -> None:
    with zipfile.ZipFile(src_path, "r") as zin:
        entries = []
        for info in zin.infolist():
            data = zin.read(info.filename)
            entries.append((info, data))

    with open(dst_path, "wb") as fout:
        central_dir = []
        for info, data in entries:
            fname_bytes = info.filename.encode("utf-8")
            # Force resources.arsc and PNGs to ZIP_STORED so Android 11+ (targetSdk 34) mmaps them directly
            force_store = (
                info.filename == "resources.arsc"
                or info.filename.lower().endswith(".png")
            )
            if force_store:
                compress_type = zipfile.ZIP_STORED
                comp_data = data
                align = 4
            elif info.compress_type == zipfile.ZIP_STORED:
                compress_type = zipfile.ZIP_STORED
                comp_data = data
                align = 16384 if info.filename.endswith(".so") else 4
            else:
                compress_type = zipfile.ZIP_DEFLATED
                comp_obj = zipfile._get_compressor(zipfile.ZIP_DEFLATED, compresslevel=6)
                comp_data = comp_obj.compress(data) + comp_obj.flush()
                align = 0

            crc = zipfile.crc32(data) & 0xFFFFFFFF
            lfh_offset = fout.tell()
            base_header_len = 30 + len(fname_bytes)

            extra = b""
            if align > 0:
                rem = (lfh_offset + base_header_len) % align
                if rem != 0:
                    pad_len = (align - rem) % align
                    if pad_len < 4:
                        pad_len += align
                    # Use Android zipalign 0xD935 padding extra field tag
                    extra = struct.pack("<HH", 0xD935, pad_len - 4) + (b"\x00" * (pad_len - 4))

            dos_date = ((info.date_time[0] - 1980) << 9) | (info.date_time[1] << 5) | info.date_time[2]
            dos_time = (info.date_time[3] << 11) | (info.date_time[4] << 5) | (info.date_time[5] // 2)

            lfh = struct.pack(
                "<4sHHHHHIIIHH",
                b"PK\x03\x04",
                20,
                0,
                compress_type,
                dos_time,
                dos_date,
                crc,
                len(comp_data),
                len(data),
                len(fname_bytes),
                len(extra),
            )
            fout.write(lfh)
            fout.write(fname_bytes)
            fout.write(extra)
            data_start = fout.tell()
            if align > 0 and (data_start % align) != 0:
                raise RuntimeError(f"Alignment failed for {info.filename}: offset {data_start} not % {align}")
            fout.write(comp_data)

            cd_hdr = struct.pack(
                "<4sHHHHHHIIIHHHHHII",
                b"PK\x01\x02",
                20,
                20,
                0,
                compress_type,
                dos_time,
                dos_date,
                crc,
                len(comp_data),
                len(data),
                len(fname_bytes),
                0,
                0,
                0,
                0,
                info.external_attr,
                lfh_offset,
            )
            central_dir.append(cd_hdr + fname_bytes)

        cd_offset = fout.tell()
        for cd in central_dir:
            fout.write(cd)
        cd_size = fout.tell() - cd_offset

        eocd = struct.pack(
            "<4sHHHHIIH",
            b"PK\x05\x06",
            0,
            0,
            len(central_dir),
            len(central_dir),
            cd_size,
            cd_offset,
            0,
        )
        fout.write(eocd)

    print(f"Verified zipalign OK: {dst_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: zipalign.py <unaligned.apk> <aligned.apk>", file=sys.stderr)
        sys.exit(1)
    align_apk(sys.argv[1], sys.argv[2])
