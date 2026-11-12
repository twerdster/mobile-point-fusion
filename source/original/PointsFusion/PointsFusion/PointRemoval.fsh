//
//  Shader.fsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es






flat in highp vec4 projCoords;
flat in highp vec4 newNormal; //in cam coords
flat in highp vec4 newCoords; //in cam cords
flat in highp float newRadius;

layout (location = 3) out highp vec4 fusion;
//out idtext

uniform highp isampler2D idTexture;
uniform sampler2D positionTexture;
uniform sampler2D normalTexture;
uniform highp float xfactor;
uniform highp float yfactor;
uniform highp int superSampleFactor;
uniform highp float maxDegree;



void main()
{
    
    highp vec2 uv = ((projCoords.xy / projCoords.w) + 1.0) / 2.0 ;
    fusion.x = 0.0;
    fusion.y = 0.0;
    fusion.z = 0.0;
    fusion.w = 0.0;
    uv.x *= xfactor * float(superSampleFactor);
    uv.y *= yfactor * float(superSampleFactor);
    highp vec3 newPoint = newCoords.xyz / newCoords.w;
    highp float dist = length(newPoint);
    newPoint = normalize(newPoint);
    highp ivec4 id;
    highp vec4 position;
    highp vec4 normal;
    highp float tempRadius;
    highp ivec2 iuv = ivec2(int(floor(uv.x)),int(floor(uv.y)));
    highp ivec2 offset;
    for (int i = 0; i < superSampleFactor; ++i) {
        for(int j = 0 ; j < superSampleFactor; ++j) {
            //check bounderies
            offset.x = i;
            offset.y = j;
            id = texelFetch(idTexture, iuv + offset , 0);
            if (id.x > 0 ) {
                //test point
                position = texelFetch(positionTexture, iuv + offset, 0);
                position.w = 1.0;
                normal = texelFetch(normalTexture, iuv + offset, 0);
                tempRadius = normal.w;
                normal.w = 1.0;
                //calculate distance between new averged point and the other points in the neighberhood
                highp float tempDist = distance(newPoint, position.xyz);
                //if they are close
                //remove this is a temorary fix TODO
                highp float r = newRadius;
                r = 0.01;
                if (tempDist < (r + tempRadius)) {//the spheres overlap
                    //if degree is less then 20 degree continue // pass as uniform must pass in radians
                    if ( dot(newNormal.xyz,normal.xyz) > cos(maxDegree)) {
                        fusion[int(fusion.x) % 3 + 1] = float(id.x);
                        fusion.x += 1.0;
                    }
                }
                //calculate the scalar projection of position onto the view ray
                tempDist = dot(newPoint, position.xyz);
                if (tempDist < dist) { //is simply checking z values sufficient??
                    //position is in front of newPoint therefore is probably an outliar cause we should have got it in the new input
                    fusion[int(fusion.x) % 3 + 1] = float(id.x);
                    fusion.x += 1.0;
                }
            }
        }
    }
}//look at article for point protection



