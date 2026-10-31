#!/usr/bin/env bash
# Build Unisoc BSP out-of-tree modules (from kernel_modules/) against our kernel.
#   OUT=<build dir> ./build_bsp_modules.sh <module> [<module> ...]
# Module names are the KO_MODULE_NAME in each Kbuild (== the .ko basename).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-out_stageb}
export PATH=/home/twodays/AOSP/LineageOS-23/prebuilts/clang/host/linux-x86/clang-r563880c/bin:$PATH
KDIR=$(pwd)/$OUT
BSP_OUT=$KDIR/bsp_modules
export BSP_MODULES_OUT=$BSP_OUT
export BSP_KERNEL_PATH=$KDIR
# BSP modules gate board-specific code on this variable, e.g.
#   ifeq ($(BSP_KERNEL_DEFCONFIG),sprd_qogirn6pro_defconfig)
#     ccflags-y += -DCONFIG_SND_SOC_UNISOC_CODEC_UMP9620
# qogirn6pro is this device's platform (UMS9620), so it must be set or the
# per-platform defines (and headers) silently disappear.
export BSP_KERNEL_DEFCONFIG=sprd_qogirn6pro_defconfig
# Board variables the BSP Kbuilds require.  Values are derived from the source
# paths embedded in the vendor .ko files of this exact device:
#   camera isp2.6/adpt/qogirn6pro/...   sensor csi2/sprd/receiver_r3p1v2/...
#   cpp cpp_lite/hw/lite_r6p0/...       mmdvfs r2p0/...
#   wcn/bluetooth/driver/tty-sdio/...   npu vdsp/products/qogirn6pro/...
export BSP_KERNEL_VERSION=kernel5.4
export BSP_BOARD_CAMERA_MODULE_ISP_VERSION=isp2.6
export BSP_BOARD_CAMERA_MODULE_ISP_ADAPT_VERSION=qogirn6pro
export BSP_BOARD_CAMERA_MODULE_CSI_VERSION=receiver_r3p1v2
export BSP_BOARD_CAMERA_MODULE_CPP_VERSION=lite_r6p0
export BSP_BOARD_CAMERA_MODULE_DVFS=qogirn6pro
export BSP_BOARD_CAMERA_PRODECT_NAME=qogirn6pro
export BSP_BOARD_PRODUCT_USING_VDSP=qogirn6pro
export BSP_VDSP_TARGET_DTB=ums9620_2h10
export BSP_BOARD_UNISOC_WCN_SOCKET=sdio
mkdir -p "$BSP_OUT"

MAKEARGS=(O="$KDIR" ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=
           KCFLAGS="-Wno-error=enum-compare -Wno-error=compare-distinct-pointer-types -Wno-error=strict-prototypes")

built=0; failed=0
for name in "$@"; do
    # KO_MODULE_NAME lives in Kbuild for most modules, but in the plain
    # Makefile for a few (video/*), so search both.
    src=$(grep -rlE "^[[:space:]]*(export[[:space:]]+)?KO_MODULE_NAME[[:space:]]*:=[[:space:]]*$name\$" \
            kernel_modules --include=Kbuild --include=Makefile 2>/dev/null \
          | grep -v "/Kbuild\$" | head -1 | xargs -r dirname)
    if [ -z "$src" ]; then
        src=$(grep -rlE "^[[:space:]]*(export[[:space:]]+)?KO_MODULE_NAME[[:space:]]*:=[[:space:]]*$name\$" \
                kernel_modules --include=Kbuild 2>/dev/null | head -1 | xargs -r dirname)
    fi
    if [ -z "$src" ]; then echo "  !! $name: 找不到 Kbuild"; failed=$((failed+1)); continue; fi
    od="$BSP_OUT/$name"
    rm -rf "$od"; mkdir -p "$od"
    ln -snf "$(pwd)/$src/Kbuild" "$od/Kbuild"
    ln -snf "$(pwd)/$src"        "$od/source"
    # KBUILD_MODPOST_WARN: BSP modules reference symbols exported by *other*
    # BSP modules, which modpost cannot see when building one module at a time.
    # Those are resolved at insmod time in modules.load order, so a modpost
    # warning is expected and harmless here.
    if make "${MAKEARGS[@]}" KBUILD_MODPOST_WARN=1 M="$od" src="$(pwd)/$src" modules > "$od/build.log" 2>&1; then
        ko=$(find "$od" -name "$name.ko" | head -1)
        if [ -n "$ko" ]; then printf "  ✔ %-34s %9s B  (%s)\n" "$name.ko" "$(stat -c%s "$ko")" "${src#kernel_modules/}"; built=$((built+1));
        else echo "  ~ $name: 编译通过但无 .ko"; failed=$((failed+1)); fi
    else
        err=$(grep -m1 -E "error:|Error [0-9]" "$od/build.log" | head -1)
        printf "  ✗ %-34s %s\n" "$name" "${err:-见 $od/build.log}"; failed=$((failed+1))
    fi
done
echo "== 成功 $built / 失败 $failed =="
