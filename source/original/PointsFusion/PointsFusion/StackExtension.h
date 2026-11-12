//
//  StackExtension.h
//  PointsFusion
//
//  Created by Daniel Ben-Hoda on 1/3/15.
//  Copyright (c) 2015 Daniel & Gal. All rights reserved.
//

#ifndef PointsFusion_StackExtension_h
#define PointsFusion_StackExtension_h

@interface NSMutableArray (StackExtension)

- (void)push:(id)object;
- (id)pop;

@end

#endif
