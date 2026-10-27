/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_camera_math.h"
#include <assert.h>
#include <stdio.h>
static void close_matrix(const float a[12], const float b[12]) {
    for (unsigned i=0;i<12;i++) assert(fabsf(a[i]-b[i]) < .0001f);
}
int main(void) {
    const float a[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
    const float b[12]={0,-1,0,10, 1,0,0,4, 0,0,1,2};
    float out[12], inv[12], twice[12], q[4];
    assert(inkypaint_camera_blend(out,a,b,0)); close_matrix(out,a);
    assert(inkypaint_camera_blend(out,a,b,1)); close_matrix(out,b);
    assert(inkypaint_camera_blend(out,a,b,.5f));
    assert(fabsf(out[0]-sqrtf(.5f))<.0001f && fabsf(out[4]-sqrtf(.5f))<.0001f);
    assert(out[3]==5 && out[7]==2 && out[11]==1);
    assert(inkypaint_camera_rotation(out,q));
    inkypaint_camera_inverse(inv,out); inkypaint_camera_inverse(twice,inv); close_matrix(out,twice);
    for (unsigned axis=0;axis<3;axis++) {
        float turn[12]; memcpy(turn,a,sizeof turn);
        turn[4*((axis+1)%3)+(axis+1)%3]=-1; turn[4*((axis+2)%3)+(axis+2)%3]=-1;
        assert(inkypaint_camera_rotation(turn,q)); assert(fabsf(fabsf(q[axis])-1)<.0001f);
    }
    /* A turn through +/-180 degrees follows the short arc instead of snapping through zero. */
    const float p170[12]={-.98480775f,-.17364818f,0,0, .17364818f,-.98480775f,0,0, 0,0,1,0};
    const float n170[12]={-.98480775f,.17364818f,0,0, -.17364818f,-.98480775f,0,0, 0,0,1,0};
    assert(inkypaint_camera_blend(out,p170,n170,.5f));
    assert(fabsf(out[0]+1)<.0001f && fabsf(out[5]+1)<.0001f && fabsf(out[1])<.0001f);
    inkypaint_camera_inverse(inv,b);
    for (unsigned i=0;i<3;i++) {
        float origin=inv[4*i+3];
        for (unsigned j=0;j<3;j++) origin+=inv[4*i+j]*b[4*j+3];
        assert(fabsf(origin)<.0001f);
    }
    float bad[12]; memcpy(bad,a,sizeof bad); bad[0]=-1;
    assert(!inkypaint_camera_blend(out,a,bad,.5f));
    memcpy(bad,a,sizeof bad); bad[3]=101; assert(!inkypaint_camera_blend(out,a,bad,.5f));
    memcpy(bad,a,sizeof bad); bad[0]=NAN; assert(!inkypaint_camera_blend(out,a,bad,.5f));
    memcpy(bad,a,sizeof bad); bad[0]=2; assert(!inkypaint_camera_blend(out,a,bad,.5f));
    assert(!inkypaint_camera_blend(out,a,b,NAN));
    puts("camera interpolation: rigid rotation, position, inverse and history rejection passed");
    return 0;
}
