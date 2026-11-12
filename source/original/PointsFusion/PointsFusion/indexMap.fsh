//
//  Shader.fsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es



flat in highp int vertexID;
flat in highp vec4 vertexPosition;
flat in highp vec3 vertexNormal;
flat in highp float  vertexConfidence;
flat in highp float  vertexRadius;


layout (location = 0) out highp int id;
layout (location = 1) out highp vec4 positionTex;
layout (location = 2) out highp vec4 normalTex;




void main()
{
    id = vertexID;
    positionTex = vertexPosition;
    positionTex.w = vertexConfidence;
    normalTex.xyz = vertexNormal;
    normalTex.w = vertexRadius;
    

}
