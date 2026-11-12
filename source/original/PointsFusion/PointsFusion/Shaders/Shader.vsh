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

out lowp vec4 colorVarying;

uniform mat4 modelViewProjectionMatrix;
uniform mat3 normalMatrix;
uniform bool _isNormalsMode;
uniform float stableConfidence;


void main()
{

    vec3 eyeNormal = normalize(normalMatrix * normal);
    vec3 lightPosition = vec3(0.0, 0.0, 1.0);
    vec4 diffuseColor = vec4(1.0, 1.0, 1.0, 1.0);
    //float /Users/archival-user/Downloads/PointsFusion/PointsFusion/Shaders/Shader.vshdet = determinant(normalMatrix);
    //float nDotVP = max(0.0, dot(eyeNormal, normalize(lightPosition)));
    float nDotVP = dot(eyeNormal, normalize(lightPosition));
    if (nDotVP < 0.0)
    {
        nDotVP *= -1.0;
    }
    
    vec4 temp = vec4(1.0,0.0,0.0, 1.0);
    
    if (confidence > stableConfidence) {
        temp = vec4(0.0,1.0,0.0, 1.0);
    }
    if (_isNormalsMode)
    {
        temp = vec4(eyeNormal[0], eyeNormal[1], eyeNormal[2], 1.0);
    }
    
    colorVarying = temp * nDotVP;
   
    gl_Position = modelViewProjectionMatrix * position;
   
    if(eyeNormal.z < 0.0)
    {
        gl_PointSize = radius*abs(2.0 * gl_Position.z/8.0);
    }
    else
    {
        gl_PointSize = radius*abs(2.0 * 8.0/gl_Position.z);
    }
    
}
