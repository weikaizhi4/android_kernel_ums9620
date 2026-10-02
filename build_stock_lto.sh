#!/usr/bin/env bash
# stock 配置 + ThinLTO/CFI（与原厂内核同 ABI）；KSU/ZTE 选项从 defconfig 导入；SUSFS 关闭，manual hook
set -e
cd "$(dirname "$0")"
OUT=${OUT:-out_stock_lto2}
export PATH=/home/twodays/AOSP/LineageOS-23/prebuilts/clang/host/linux-x86/clang-r563880c/bin:$PATH
CFG=$OUT/.config
DEFCONFIG=arch/arm64/configs/sprd_p720s11_defconfig
mkdir -p "$OUT"
if [ ! -f "$CFG" ]; then
  for cand in out_stock_lto2/.config out_lto/.config; do
    if [ -f "$cand" ] && [ "$cand" != "$CFG" ]; then cp "$cand" "$CFG"; break; fi
  done
fi
[ -f "$CFG" ] || { echo "缺少 $CFG"; exit 1; }
python3 - "$CFG" "$DEFCONFIG" <<'PY'
import re, sys
cfg, deff = sys.argv[1], sys.argv[2]
want = {}
for l in open(deff):
    l = l.rstrip('\n')
    m = re.match(r'^(# )?(CONFIG_[A-Z0-9_]+)(=| is not set)', l)
    if m and m.group(2).startswith('CONFIG_KSU'):
        want[m.group(2)] = l
out = []
for l in open(cfg).read().split('\n'):
    m = re.match(r'^(# )?(CONFIG_[A-Z0-9_]+)(=| is not set)', l)
    if m and m.group(2) in want:
        continue
    out.append(l)
out += list(want.values())
open(cfg, 'w').write('\n'.join(out) + '\n')
print(f"[config] 从 defconfig 导入 {len(want)} 个符号")
PY
scripts/config --file "$CFG" \
  -e LTO_CLANG -d LTO_CLANG_FULL -e LTO_CLANG_THIN -e CFI_CLANG \
  -e VENDOR_COMMON_COMPILE -e VENDOR_SOC_SPRD_COMPILE \
  -m TOUCHSCREEN_VENDOR_V2 -m TOUCHSCREEN_LCD_NOTIFY \
  -e KSU -d KSU_SUSFS -e KSU_MANUAL_HOOK -d KSU_TRACEPOINT_HOOK \
  -d WERROR
for s in $(grep -oE '^CONFIG_KSU_SUSFS[A-Z0-9_]*' "$CFG"); do scripts/config --file "$CFG" -d "${s#CONFIG_}"; done
MAKE_ARGS=(O="$OUT" ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=
           KCFLAGS="-Wno-error=enum-compare -Wno-error=compare-distinct-pointer-types -Wno-error=strict-prototypes")
make "${MAKE_ARGS[@]}" olddefconfig
make "${MAKE_ARGS[@]}" -j"$(nproc)" Image modules
echo "== Image =="; ls -la "$OUT/arch/arm64/boot/Image"
echo "== modules =="; find "$OUT" -name '*.ko' | wc -l
