//
//  Shader.vsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es




in vec4 position;
in vec3 normal;
in float confidence;
in float radius;

flat out highp int vertexID;
flat out highp vec4 vertexPosition;
flat out highp vec3 vertexNormal;
flat out highp float  vertexConfidence;
flat out highp float  vertexRadius;


uniform mat4 modelViewProjectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;

void main()
{
    gl_PointSize  = 1.0f;
    vertexID = gl_VertexID;
    gl_Position = modelViewProjectionMatrix * position;
    vertexPosition = modelViewMatrix * position;
    vertexNormal = normalize(normalMatrix * normal);
    vertexConfidence = confidence;
    vertexRadius = radius;
}
