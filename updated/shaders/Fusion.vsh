// Updated 2026: camera-space coordinates; original source preserved separately.
//
//  Shader.vsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es


in vec3 position;
in vec3 normal;

flat out highp int newVertexID;
flat out highp vec4 projCoords;
flat out highp vec4 newNormal; //in cam coords
flat out highp vec4 newCoords;
// Keep these query outputs invariant for the diagnostic transform-feedback draw.
invariant newCoords;
invariant newNormal;

uniform mat4 modelViewProjectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;


void main()
{
    gl_PointSize  = 1.0f;
    projCoords.w = 1.0;
    projCoords.xyz = position;
    gl_Position = modelViewProjectionMatrix * projCoords; //need to change to only projection 
    projCoords = gl_Position;
    newVertexID = gl_VertexID; // remember to check base index ??
   //need to pass new cords and normal
    newCoords = modelViewMatrix * vec4(position, 1.0);
    newNormal.xyz = normalize(normalMatrix * normal); // no nneed to mult with matrix
    newNormal.w = 1.0;
    
}
