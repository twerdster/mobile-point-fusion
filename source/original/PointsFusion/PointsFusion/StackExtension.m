//
//  StackExtension.m
//  PointsFusion
//
//  Created by Daniel Ben-Hoda on 1/3/15.
//  Copyright (c) 2015 Daniel & Gal. All rights reserved.
//

#import <Foundation/Foundation.h>

#import "StackExtension.h"

@implementation NSMutableArray (StackExtension)

- (void)push:(id)object {
    [self insertObject:object atIndex:0];
}

- (id)pop {
    id lastObject = [self lastObject];
    [self removeLastObject];
    return lastObject;
}

@end