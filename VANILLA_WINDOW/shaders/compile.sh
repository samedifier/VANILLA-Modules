#!/usr/bin/env bash
#
# VANILLA WINDOW internal shaders -> embedded SPIR-V headers in ../include/.
# Run from anywhere. Intermediate .spv files go to a temporary directory; the headers in ../include/ are only
# replaced when every shader compiled.
#
# material.glsl and env.glsl are the module's shader interface (GPU material struct + flags, SH basis):
# editor scene shaders that sample materials / environment lighting must #include copies of them.

set -u

SHADER_DIR="$(cd "$(dirname "$0")" && pwd)"
INCLUDE_DIR="$SHADER_DIR/../include"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

cd "$SHADER_DIR" || exit 1

compile_shader() {
    local src="$1"
    local dst="$2"
    shift 2

    echo "Compiling $src -> $(basename "$dst")"

    if ! glslangValidator -V "$@" "$src" -o "$dst"; then
        echo "ERROR: failed to compile $src" >&2
        return 1
    fi
}

# embed <header> <spv> <array name> [<spv> <array name> ...]
embed() {
    local header="$1"
    shift

    : > "$BUILD_DIR/$header"
    while [ "$#" -ge 2 ]; do
        python3 "$SHADER_DIR/bintoarr.py" "$BUILD_DIR/$1" "$2" >> "$BUILD_DIR/$header" || return 1
        shift 2
    done
}

B="$BUILD_DIR"

# Light volume bake
compile_shader lightbake.comp "$B/lightbake.comp.spv" || exit 1
embed vanilla_window_lightbake_spv.h lightbake.comp.spv VANILLA_lightBakeSpv || exit 1

# Shadow depth (opaque) + alpha-tested shadows (MASK)
compile_shader shadowdepth.vert "$B/shadowdepth.vert.spv" || exit 1
compile_shader shadowmask.vert  "$B/shadowmask.vert.spv"  || exit 1
compile_shader shadowmask.frag  "$B/shadowmask.frag.spv"  || exit 1
embed vanilla_window_shadowdepth_spv.h \
    shadowdepth.vert.spv VANILLA_shadowDepthSpv \
    shadowmask.vert.spv  VANILLA_shadowMaskVertSpv \
    shadowmask.frag.spv  VANILLA_shadowMaskFragSpv || exit 1

# Tone mapping (+ MSAA variant) and FXAA
compile_shader tonemap.vert    "$B/tonemap.vert.spv"    || exit 1
compile_shader tonemap.frag    "$B/tonemap.frag.spv"    || exit 1
compile_shader tonemap_ms.frag "$B/tonemap_ms.frag.spv" || exit 1
compile_shader fxaa.frag       "$B/fxaa.frag.spv"       || exit 1
embed vanilla_window_tonemap_spv.h \
    tonemap.vert.spv    VANILLA_toneMapVertSpv \
    tonemap.frag.spv    VANILLA_toneMapFragSpv \
    tonemap_ms.frag.spv VANILLA_toneMapMsFragSpv \
    fxaa.frag.spv       VANILLA_fxaaFragSpv || exit 1

# Atmosphere LUTs + environment lighting (atmosphere.glsl, atmosphere_scatter.glsl, env.glsl)
compile_shader atmo_transmittance.comp "$B/atmo_transmittance.comp.spv" || exit 1
compile_shader atmo_multiscatter.comp  "$B/atmo_multiscatter.comp.spv"  || exit 1
compile_shader atmo_skyview.comp       "$B/atmo_skyview.comp.spv"       || exit 1
compile_shader atmo_aerial.comp        "$B/atmo_aerial.comp.spv"        || exit 1
compile_shader env_cube.comp           "$B/env_cube.comp.spv"           || exit 1
compile_shader env_sh.comp             "$B/env_sh.comp.spv"             || exit 1
compile_shader env_prefilter.comp      "$B/env_prefilter.comp.spv"      || exit 1
compile_shader brdf_lut.comp           "$B/brdf_lut.comp.spv"           || exit 1
embed vanilla_window_atmosphere_spv.h \
    atmo_transmittance.comp.spv VANILLA_atmoTransmittanceSpv \
    atmo_multiscatter.comp.spv  VANILLA_atmoMultiScatterSpv \
    atmo_skyview.comp.spv       VANILLA_atmoSkyViewSpv \
    atmo_aerial.comp.spv        VANILLA_atmoAerialSpv \
    env_cube.comp.spv           VANILLA_envCubeSpv \
    env_sh.comp.spv             VANILLA_envShSpv \
    env_prefilter.comp.spv      VANILLA_envPrefilterSpv \
    brdf_lut.comp.spv           VANILLA_brdfLutSpv || exit 1

# Sky background
compile_shader sky.vert "$B/sky.vert.spv" || exit 1
compile_shader sky.frag "$B/sky.frag.spv" || exit 1
embed vanilla_window_sky_spv.h \
    sky.vert.spv VANILLA_skyVertSpv \
    sky.frag.spv VANILLA_skyFragSpv || exit 1

# Editor grid
compile_shader grid.vert "$B/grid.vert.spv" || exit 1
compile_shader grid.frag "$B/grid.frag.spv" || exit 1
embed vanilla_window_grid_spv.h \
    grid.vert.spv VANILLA_gridVertSpv \
    grid.frag.spv VANILLA_gridFragSpv || exit 1

# Compute skinning + morph targets
compile_shader skinning.comp "$B/skinning.comp.spv" || exit 1
embed vanilla_window_skinning_spv.h skinning.comp.spv VANILLA_skinningSpv || exit 1

# GPU frustum + occlusion culling (Hi-Z, with an MSAA depth variant)
compile_shader cull.comp       "$B/cull.comp.spv"        || exit 1
compile_shader hiz_init.comp   "$B/hiz_init.comp.spv"    || exit 1
compile_shader hiz_init.comp   "$B/hiz_init_ms.comp.spv" -DMSAA || exit 1
compile_shader hiz_reduce.comp "$B/hiz_reduce.comp.spv"  || exit 1
embed vanilla_window_cull_spv.h \
    cull.comp.spv       VANILLA_cullSpv \
    hiz_init.comp.spv   VANILLA_hizInitSpv \
    hiz_init_ms.comp.spv VANILLA_hizInitMsSpv \
    hiz_reduce.comp.spv VANILLA_hizReduceSpv || exit 1

# Every shader compiled: replace the embedded headers.
cp "$B"/vanilla_window_*_spv.h "$INCLUDE_DIR/" || exit 1
echo "done: headers written to $(cd "$INCLUDE_DIR" && pwd)"
