package com.mojang.blaze3d.vertex;

import com.mojang.renderpearl.api.vertex.VertexFormat;

public final class DefaultVertexFormat {
    private DefaultVertexFormat() {}

    public static final VertexFormat POSITION = new VertexFormat("position");
    public static final VertexFormat POSITION_COLOR = new VertexFormat("position_color");
    public static final VertexFormat POSITION_COLOR_NORMAL_LINE_WIDTH = new VertexFormat("position_color_normal_line_width");
    public static final VertexFormat POSITION_TEX_COLOR = new VertexFormat("position_tex_color");
    public static final VertexFormat POSITION_COLOR_TEX_LIGHTMAP = new VertexFormat("position_color_tex_lightmap");
}
