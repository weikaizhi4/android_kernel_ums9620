#!/usr/bin/env bash
# 打包 boot.img + vendor_dlkm.img（内容与设备上运行的一致）
#   boot.img      : 设备原厂 ramdisk（逐字节不变）+ 本次编译的 Image，v4 header
#   vendor_dlkm   : 原厂模块逐字节照搬 + 本次编译的额外模块(strip) + 设备上的模块清单
set -e
cd "$(dirname "$0")"
OUT=${OUT:-out_stock_lto2}
DUMP=${DUMP:-/home/twodays/AOSP/device_dump}
IMGDIR=${IMGDIR:-out_images}
WORK=$IMGDIR/work_pack
export PATH=/home/twodays/AOSP/LineageOS-23/out/host/linux-x86/bin:$PATH

[ -f "$OUT/arch/arm64/boot/Image" ] || { echo "缺少 $OUT/arch/arm64/boot/Image，请先编译"; exit 1; }
mkdir -p "$WORK" "$IMGDIR"

echo "== 1. boot.img =="
rm -rf "$WORK/boot"; mkdir -p "$WORK/boot"
unpack_bootimg --boot_img "$DUMP/boot_a.img" --out "$WORK/boot" >/dev/null
mkbootimg --header_version 4 --os_version 13.0.0 --os_patch_level 2024-04 \
          --kernel "$OUT/arch/arm64/boot/Image" \
          --ramdisk "$WORK/boot/ramdisk" \
          --output "$IMGDIR/boot_stock.img"
truncate -s 67108864 "$IMGDIR/boot_stock.img"      # 对齐到分区大小
echo "   $(stat -c%s "$IMGDIR/boot_stock.img") 字节"

echo "== 2. vendor_dlkm.img =="
STAGE="$WORK/vd"; rm -rf "$STAGE"; mkdir -p "$STAGE/lib/modules"
# 2.1 原厂模块逐字节照搬
cp -a "$IMGDIR/vendor_dlkm_extract/lib/modules/." "$STAGE/lib/modules/"
# 2.2 从设备 dump 解出模块清单（modules.load/dep/alias/softdep 等）
rm -rf "$WORK/vdref"; mkdir -p "$WORK/vdref"
fsck.erofs --extract="$WORK/vdref" "$DUMP/vendor_dlkm.img" >/dev/null 2>&1
RAMDISK_LOAD="$WORK/ramdisk_modules.load"
if [ ! -f "$RAMDISK_LOAD" ]; then
    # 原厂 vendor_boot 的 ramdisk 里有一阶段 modules.load（LZ4 + cpio）
    for cand in "$DUMP/vb_unpack/ex00/lib/modules/modules.load" \
                "$WORK/vbex/lib/modules/modules.load"; do
        [ -s "$cand" ] && cp -f "$cand" "$RAMDISK_LOAD" && break
    done
    if [ ! -s "$RAMDISK_LOAD" ]; then
        rm -rf "$WORK/vb" "$WORK/vbex"; mkdir -p "$WORK/vb" "$WORK/vbex"
        unpack_bootimg --boot_img "$DUMP/vendor_boot.img" --out "$WORK/vb" >/dev/null 2>&1 || true
        ( cd "$WORK/vbex" && (lz4 -d "$WORK/vb/vendor_ramdisk00" stdout 2>/dev/null || unlz4 -c "$WORK/vb/vendor_ramdisk00" 2>/dev/null) | cpio -idm --quiet 2>/dev/null ) || true
        cp -f "$WORK/vbex/lib/modules/modules.load" "$RAMDISK_LOAD" 2>/dev/null || : > "$RAMDISK_LOAD"
    fi
fi
for f in modules.load modules.dep modules.alias modules.softdep modules.load.cali modules.load.charger init.insmod.cfg; do
    [ -f "$WORK/vdref/lib/modules/$f" ] && cp -f "$WORK/vdref/lib/modules/$f" "$STAGE/lib/modules/$f"
done
# 2.3 额外模块：以设备上的 vendor_dlkm 为准（ROM 提取物里没有、但设备上有的），
#     二进制优先用本次编译的（strip 后），没有则沿用设备上的
added=0
for ko in "$WORK/vdref/lib/modules/"*.ko; do
    name=$(basename "$ko")
    [ -f "$STAGE/lib/modules/$name" ] && continue
    built=$(find "$OUT" -name "$name" -print -quit 2>/dev/null)
    if [ -n "$built" ]; then
        cp -f "$built" "$STAGE/lib/modules/$name"
        ( aarch64-linux-gnu-strip --strip-debug "$STAGE/lib/modules/$name" 2>/dev/null \
          || llvm-strip --strip-debug "$STAGE/lib/modules/$name" 2>/dev/null \
          || strip --strip-debug "$STAGE/lib/modules/$name" 2>/dev/null ) || true
    else
        cp -f "$ko" "$STAGE/lib/modules/$name"
    fi
    chmod 644 "$STAGE/lib/modules/$name"
    added=$((added+1))
done
echo "   额外模块 $added 个，模块总数 $(ls "$STAGE/lib/modules/"*.ko | wc -l)"
# 2.4 SELinux 上下文（与设备一致：/vendor_dlkm 下均为 vendor_file）
FC="$WORK/file_contexts"
cat > "$FC" <<'EOT'
/(vendor_dlkm|vendor/vendor_dlkm|system/vendor/vendor_dlkm)(/.*)?	u:object_r:vendor_file:s0
/(vendor_dlkm|vendor/vendor_dlkm|system/vendor/vendor_dlkm)/etc(/.*)?	u:object_r:vendor_configs_file:s0
EOT
find "$STAGE" -type d -exec chmod 755 {} \;
find "$STAGE" -type f -exec chmod 644 {} \;
rm -f "$IMGDIR/vendor_dlkm_stock.img"
mkfs.erofs -b 4096 -zlz4hc,9 --all-root -T 1712768737 \
    -U df370144-e703-4f5f-a8db-6ade567482e3 -E ^xattr-name-filter \
    --mount-point /vendor_dlkm --file-contexts="$FC" \
    "$IMGDIR/vendor_dlkm_stock.img" "$STAGE" >/dev/null
echo "   $(stat -c%s "$IMGDIR/vendor_dlkm_stock.img") 字节"

echo "== 3. 校验（与设备对比）=="
echo "   boot   ramdisk : $(cmp -s "$WORK/boot/ramdisk" <(unpack_bootimg --boot_img "$DUMP/boot_a.img" --out /tmp/chk_$$ >/dev/null 2>&1; cat /tmp/chk_$$/ramdisk) && echo 逐字节一致 || echo 不同)"
echo "   boot   kernel  : 设备 $(stat -c%s /tmp/chk_$$/kernel 2>/dev/null) / 本次 $(stat -c%s "$OUT/arch/arm64/boot/Image") 字节"
echo "   dlkm   模块数  : 设备 $(find "$WORK/vdref/lib/modules" -name '*.ko' | wc -l) / 本次 $(ls "$STAGE/lib/modules/"*.ko | wc -l)"
rm -rf /tmp/chk_$$
md5sum "$IMGDIR/boot_stock.img" "$IMGDIR/vendor_dlkm_stock.img"
