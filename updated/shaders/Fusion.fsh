// Updated 2026: aligned/bounded neighborhood, high-precision samples, geometric gate.
//
//  Shader.fsh
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#version 300 es





flat in highp int  newVertexID;
flat in highp vec4 projCoords;
flat in highp vec4 newNormal; //in cam coords
flat in highp vec4 newCoords; //in cam cords
#ifndef GL_ES
// GLSL330 requires matching invariant inputs; ESSL300 forbids them.
invariant newCoords;
invariant newNormal;
#endif

layout (location = 3) out highp vec4 fusion;
//out idtext

uniform highp isampler2D idTexture;
uniform highp sampler2D positionTexture;
uniform highp sampler2D normalTexture;
uniform highp float xfactor;
uniform highp float yfactor;
uniform highp int superSampleFactor;
uniform highp float maxDistance;
// Updated: evaluate the same strict 20-degree FP32 cutoff once on the CPU.
uniform highp float minNormalDot;

highp float delta(highp float depth) {
    // Historical unused helper. Neighborhood alignment now uses gl_FragCoord.
    return 10.0 * (depth + 0.5);
}

void main()
{
    
    
    highp vec2 uv = projCoords.xy / projCoords.w;
    fusion.z = uv.x;
    fusion.w = uv.y;
    //fusion.z = 0.0;
    //fusion.w = 0.0;
    uv = (uv  + 1.0) / 2.0 ;
    fusion.x = -1.0;
    fusion.y = float(newVertexID);
    uv.x *= xfactor * float(superSampleFactor);
    uv.y *= yfactor * float(superSampleFactor);
    highp vec3 newPoint = newCoords.xyz / newCoords.w;
    highp float depth = newPoint.z;
    newPoint = normalize(newPoint);
    highp ivec4 id;
    highp vec4 position;
    highp vec4 normal;
    highp float confidence = -1.0;
    highp float radius;
    highp float dist = 100.0; //max dist
    highp ivec2 iuv = ivec2(gl_FragCoord.xy) * superSampleFactor;
    //iuv *= superSampleFactor;
    highp ivec2 offset;
    for (int i = 0; i < superSampleFactor; ++i) {
        for(int j = 0 ; j < superSampleFactor; ++j) {
            //check bounderies
            offset.x = i;
            offset.y = j;
            if (any(greaterThanEqual(iuv + offset, textureSize(idTexture, 0)))) continue;
            id = texelFetch(idTexture, iuv + offset , 0);
            if (id.x > 0 ) {
                //test point
                position = texelFetch(positionTexture, iuv + offset, 0);
                highp float tempConfidence = position.w; //pointless
                position.w = 1.0;
                //calculate distance to viewing ray viewray is normalized
                highp float tempDist = length(cross(newPoint, position.xyz));
                
                //if dist to ray < +- delta depth continue and save dist
                //if (tempDist <= abs(delta(depth))) {
                if (distance(newCoords.xyz, position.xyz) <= maxDistance) {
                    normal = texelFetch(normalTexture, iuv + offset, 0);
                    radius = normal.w;
                    normal.w = 1.0;
                    //if degree is less then 20 degree continue // pass as uniform must pass in radians 
                    if ( dot(newNormal.xyz,normal.xyz) > minNormalDot ) { // dot of vec 3 not vec 4 need to be
                        if (confidence < tempConfidence) {
                            
                            confidence = tempConfidence;
                            dist = tempDist;
                            fusion.x = float(id.x);
                        } else if (confidence == tempConfidence) {
                                    if (dist > tempDist) {
                                        dist = tempDist;
                                        fusion.x = float(id.x);
                                    }
                        }
                    }
                }
            }
        }
    }
}

    
    
