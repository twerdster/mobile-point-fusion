//
//  ViewController.h
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import "StackExtension.h"


@interface ViewController : GLKViewController
- (IBAction)toggleNormals:(UISwitch *)sender;
- (IBAction)toggleControlMode:(UISwitch *)sender;

//@interface APLGestureRecognizerViewController : UIViewController <UIGestureRecognizerDelegate>


@end
