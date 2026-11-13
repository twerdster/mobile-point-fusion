// Updated 2026, adapted from the recovered Daniel & Gal (2014) PointRemoval.fsh.
// Copyright (c) 2014 Daniel & Gal. All rights reserved.
// Original copyright/notices and complete shader remain unchanged in source/.
// Repair actual-coordinate distance, exclude self, bound/deduplicate RGBA IDs.
#version 300 es
flat in highp vec4 projCoords;
flat in highp vec4 newNormal;
flat in highp vec4 newCoords;
flat in highp float newRadius;
flat in highp int newVertexID;
flat in highp float newConfidence;
layout(location=3) out highp vec4 fusion;
uniform highp isampler2D idTexture;
uniform highp sampler2D positionTexture;
uniform highp sampler2D normalTexture;
uniform highp int superSampleFactor;
uniform highp float maxDegree;
void main(){
 fusion=vec4(0.0);
 highp ivec2 base=ivec2(gl_FragCoord.xy)*superSampleFactor;
 for(int i=0;i<superSampleFactor;i++)for(int j=0;j<superSampleFactor;j++){
  highp ivec2 uv=base+ivec2(i,j);
  if(any(greaterThanEqual(uv,textureSize(idTexture,0))))continue;
  highp int id=texelFetch(idTexture,uv,0).x;
  if(id<=0||id==newVertexID)continue;
  highp vec4 candidate=texelFetch(positionTexture,uv,0);
  // Total order prevents two overlapping queries from deleting each other.
  if(candidate.w>newConfidence || (candidate.w==newConfidence && id<newVertexID))continue;
  highp vec3 p=candidate.xyz;
  highp vec4 n=texelFetch(normalTexture,uv,0);
  if(distance(newCoords.xyz,p)>=newRadius+n.w || dot(newNormal.xyz,n.xyz)<=cos(maxDegree))continue;
  bool duplicate=false;
  for(int k=1;k<=3;k++)if(k<=int(fusion.x)&&int(fusion[k])==id)duplicate=true;
  if(!duplicate&&fusion.x<3.0){fusion.x+=1.0;fusion[int(fusion.x)]=float(id);}
 }
}
