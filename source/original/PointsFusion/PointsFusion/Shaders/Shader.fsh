//
//  Shader.fsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es

in lowp vec4 colorVarying;
out lowp vec4 fragmentColor;

void main()
{
    fragmentColor = colorVarying;
}
