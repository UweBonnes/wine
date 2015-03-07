/*
 * Direct3D wine OpenGL include file
 *
 * Copyright 2002-2003 The wine-d3d team
 * Copyright 2002-2004 Jason Edmeades
 *                     Raphael Junqueira
 * Copyright 2007 Roderick Colenbrander
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_WINED3D_GL_H
#define __WINE_WINED3D_GL_H

#include "wine/wgl.h"
#include "wine/wglext.h"

#define GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI 0x8837  /* not in the gl spec */

void (WINE_GLAPI *glDisableWINE)(GLenum cap) DECLSPEC_HIDDEN;
void (WINE_GLAPI *glEnableWINE)(GLenum cap) DECLSPEC_HIDDEN;

/* OpenGL extensions. */
enum wined3d_gl_extension
{
    WINED3D_GL_EXT_NONE,

    /* APPLE */
    APPLE_CLIENT_STORAGE,
    APPLE_FENCE,
    APPLE_FLOAT_PIXELS,
    APPLE_FLUSH_BUFFER_RANGE,
    APPLE_YCBCR_422,
    /* ARB */
    ARB_BLEND_FUNC_EXTENDED,
    ARB_COLOR_BUFFER_FLOAT,
    ARB_DEBUG_OUTPUT,
    ARB_DEPTH_BUFFER_FLOAT,
    ARB_DEPTH_TEXTURE,
    ARB_DRAW_BUFFERS,
    ARB_DRAW_ELEMENTS_BASE_VERTEX,
    ARB_DRAW_INSTANCED,
    ARB_FRAGMENT_PROGRAM,
    ARB_FRAGMENT_SHADER,
    ARB_FRAMEBUFFER_OBJECT,
    ARB_FRAMEBUFFER_SRGB,
    ARB_GEOMETRY_SHADER4,
    ARB_HALF_FLOAT_PIXEL,
    ARB_HALF_FLOAT_VERTEX,
    ARB_INSTANCED_ARRAYS,
    ARB_INTERNALFORMAT_QUERY2,
    ARB_MAP_BUFFER_ALIGNMENT,
    ARB_MAP_BUFFER_RANGE,
    ARB_MULTISAMPLE,
    ARB_MULTITEXTURE,
    ARB_OCCLUSION_QUERY,
    ARB_PIXEL_BUFFER_OBJECT,
    ARB_POINT_PARAMETERS,
    ARB_POINT_SPRITE,
    ARB_PROVOKING_VERTEX,
    ARB_SAMPLER_OBJECTS,
    ARB_SHADER_BIT_ENCODING,
    ARB_SHADER_OBJECTS,
    ARB_SHADER_TEXTURE_LOD,
    ARB_SHADING_LANGUAGE_100,
    ARB_SHADOW,
    ARB_SYNC,
    ARB_TEXTURE_BORDER_CLAMP,
    ARB_TEXTURE_COMPRESSION,
    ARB_TEXTURE_COMPRESSION_RGTC,
    ARB_TEXTURE_CUBE_MAP,
    ARB_TEXTURE_ENV_COMBINE,
    ARB_TEXTURE_ENV_DOT3,
    ARB_TEXTURE_FLOAT,
    ARB_TEXTURE_MIRRORED_REPEAT,
    ARB_TEXTURE_MIRROR_CLAMP_TO_EDGE,
    ARB_TEXTURE_NON_POWER_OF_TWO,
    ARB_TEXTURE_RECTANGLE,
    ARB_TEXTURE_RG,
    ARB_TIMER_QUERY,
    ARB_UNIFORM_BUFFER_OBJECT,
    ARB_VERTEX_ARRAY_BGRA,
    ARB_VERTEX_BLEND,
    ARB_VERTEX_BUFFER_OBJECT,
    ARB_VERTEX_PROGRAM,
    ARB_VERTEX_SHADER,
    /* ATI */
    ATI_FRAGMENT_SHADER,
    ATI_SEPARATE_STENCIL,
    ATI_TEXTURE_COMPRESSION_3DC,
    ATI_TEXTURE_ENV_COMBINE3,
    ATI_TEXTURE_MIRROR_ONCE,
    /* EXT */
    EXT_BLEND_COLOR,
    EXT_BLEND_EQUATION_SEPARATE,
    EXT_BLEND_FUNC_SEPARATE,
    EXT_BLEND_MINMAX,
    EXT_BLEND_SUBTRACT,
    EXT_DRAW_BUFFERS2,
    EXT_DEPTH_BOUNDS_TEST,
    EXT_FOG_COORD,
    EXT_FRAMEBUFFER_BLIT,
    EXT_FRAMEBUFFER_MULTISAMPLE,
    EXT_FRAMEBUFFER_OBJECT,
    EXT_GPU_PROGRAM_PARAMETERS,
    EXT_GPU_SHADER4,
    EXT_PACKED_DEPTH_STENCIL,
    EXT_POINT_PARAMETERS,
    EXT_PROVOKING_VERTEX,
    EXT_SECONDARY_COLOR,
    EXT_STENCIL_TWO_SIDE,
    EXT_STENCIL_WRAP,
    EXT_TEXTURE3D,
    EXT_TEXTURE_COMPRESSION_RGTC,
    EXT_TEXTURE_COMPRESSION_S3TC,
    EXT_TEXTURE_ENV_COMBINE,
    EXT_TEXTURE_ENV_DOT3,
    EXT_TEXTURE_FILTER_ANISOTROPIC,
    EXT_TEXTURE_LOD_BIAS,
    EXT_TEXTURE_MIRROR_CLAMP,
    EXT_TEXTURE_SNORM,
    EXT_TEXTURE_SRGB,
    EXT_TEXTURE_SRGB_DECODE,
    EXT_VERTEX_ARRAY_BGRA,
    /* NVIDIA */
    NV_FENCE,
    NV_FOG_DISTANCE,
    NV_FRAGMENT_PROGRAM,
    NV_FRAGMENT_PROGRAM2,
    NV_FRAGMENT_PROGRAM_OPTION,
    NV_HALF_FLOAT,
    NV_LIGHT_MAX_EXPONENT,
    NV_POINT_SPRITE,
    NV_REGISTER_COMBINERS,
    NV_REGISTER_COMBINERS2,
    NV_TEXGEN_REFLECTION,
    NV_TEXTURE_ENV_COMBINE4,
    NV_TEXTURE_SHADER,
    NV_TEXTURE_SHADER2,
    NV_VERTEX_PROGRAM,
    NV_VERTEX_PROGRAM1_1,
    NV_VERTEX_PROGRAM2,
    NV_VERTEX_PROGRAM2_OPTION,
    NV_VERTEX_PROGRAM3,
    /* SGI */
    SGIS_GENERATE_MIPMAP,
    /* WGL extensions */
    WGL_ARB_PIXEL_FORMAT,
    WGL_EXT_SWAP_CONTROL,
    WGL_WINE_PIXEL_FORMAT_PASSTHROUGH,
    /* Internally used */
    WINED3D_GL_NORMALIZED_TEXRECT,
    WINED3D_GL_VERSION_2_0,

    WINED3D_GL_EXT_COUNT,
};
#endif /* __WINE_WINED3D_GL */
