// New native Cocoa/UIKit frontend. Executes the updated shader/CPU pipeline.
#include "../../src/live.hpp"
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#include <memory>
#if TARGET_OS_IPHONE
#import <OpenGLES/EAGL.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif

using pf::LiveFusion;
using pf::require;
static std::string resourceRoot() {
  return [[[NSBundle mainBundle].resourcePath
      stringByAppendingPathComponent:@"fusion"] UTF8String];
}
static std::string outputRoot() {
  NSString *base = NSSearchPathForDirectoriesInDomains(
#if TARGET_OS_IPHONE
                       NSDocumentDirectory,
#else
                       NSApplicationSupportDirectory,
#endif
                       NSUserDomainMask, YES)
                       .firstObject;
  return [[base stringByAppendingPathComponent:@"MobilePointFusionNative"]
      UTF8String];
}
static NSString *stats(LiveFusion *f) {
  if (!f)
    return @"Preparing native GL context…";
  return
      [NSString stringWithFormat:
                    @"View %d / 48  •  %zu points  •  %zu merges  •  %zu "
                    @"removals\n%zu fusion correspondences checked against CPU",
                    f->frame, f->model.size(), f->model.merged,
                    f->model.removed, f->compared];
}
#if TARGET_OS_IPHONE
@interface FusionView : UIView {
@public
  EAGLContext *_context;
  GLuint _framebuffer, _color, _depth;
  GLint _pixelWidth, _pixelHeight;
  std::unique_ptr<LiveFusion> _fusion;
  float _orbit;
  BOOL _canDraw;
  NSString *_error;
}
- (void)draw;
@end
@implementation FusionView
+ (Class)layerClass {
  return [CAEAGLLayer class];
}
- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _canDraw = UIApplication.sharedApplication.applicationState !=
               UIApplicationStateBackground;
    require(_canDraw, "Open the native app in the foreground to initialize GL");
    self.contentScaleFactor = UIScreen.mainScreen.scale;
    ((CAEAGLLayer *)self.layer).opaque = YES;
    ((CAEAGLLayer *)self.layer).drawableProperties = @{
      kEAGLDrawablePropertyRetainedBacking : @NO,
      kEAGLDrawablePropertyColorFormat : kEAGLColorFormatRGBA8
    };
    _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    require(_context != nil, "EAGL ES3 unavailable");
    [EAGLContext setCurrentContext:_context];
    _fusion = std::make_unique<LiveFusion>(resourceRoot(), outputRoot());
    UIPanGestureRecognizer *pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:self
                                                action:@selector(pan:)];
    [self addGestureRecognizer:pan];
  }
  return self;
}
- (void)pan:(UIPanGestureRecognizer *)g {
  if (!_canDraw)
    return;
  CGPoint delta = [g translationInView:self];
  _orbit += delta.x / 180.f;
  [g setTranslation:CGPointZero inView:self];
  [self draw];
}
- (void)layoutSubviews {
  [super layoutSubviews];
  if (!_canDraw || self.bounds.size.width <= 0 || self.bounds.size.height <= 0)
    return;
  GLint width =
      GLint(std::lround(self.bounds.size.width * self.contentScaleFactor));
  GLint height =
      GLint(std::lround(self.bounds.size.height * self.contentScaleFactor));
  if (width <= 0 || height <= 0)
    return;
  if (_framebuffer && width == _pixelWidth && height == _pixelHeight)
    return;
  try {
    [EAGLContext setCurrentContext:_context];
    if (_framebuffer)
      glDeleteFramebuffers(1, &_framebuffer);
    if (_color)
      glDeleteRenderbuffers(1, &_color);
    if (_depth)
      glDeleteRenderbuffers(1, &_depth);
    _framebuffer = _color = _depth = 0;
    _pixelWidth = _pixelHeight = 0;
    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glGenRenderbuffers(1, &_color);
    glBindRenderbuffer(GL_RENDERBUFFER, _color);
    require([_context renderbufferStorage:GL_RENDERBUFFER
                             fromDrawable:(CAEAGLLayer *)self.layer],
            "drawable storage failed");
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,
                                 &_pixelWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT,
                                 &_pixelHeight);
    if (_pixelWidth <= 0 || _pixelHeight <= 0)
      return;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, _color);
    glGenRenderbuffers(1, &_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, _depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, _pixelWidth,
                          _pixelHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, _depth);
    pf::GL::check_framebuffer("display", "RGBA8/depth24");
    _error = nil;
    [self draw];
  } catch (const std::exception &e) {
    _error = [NSString stringWithUTF8String:e.what()];
    NSLog(@"Fusion layout failure: %@", _error);
  }
}
- (void)draw {
  if (!_canDraw || _error || !_fusion || !_framebuffer || _pixelWidth <= 0 ||
      _pixelHeight <= 0)
    return;
  try {
    [EAGLContext setCurrentContext:_context];
    _fusion->render(_framebuffer, _pixelWidth, _pixelHeight, _orbit);
    glBindRenderbuffer(GL_RENDERBUFFER, _color);
    require([_context presentRenderbuffer:GL_RENDERBUFFER],
            "drawable present failed");
  } catch (const std::exception &e) {
    _error = [NSString stringWithUTF8String:e.what()];
    NSLog(@"Fusion display failure: %@", _error);
  }
}
- (void)dealloc {
  if (UIApplication.sharedApplication.applicationState !=
      UIApplicationStateBackground) {
    [EAGLContext setCurrentContext:_context];
    _fusion.reset();
    if (_framebuffer)
      glDeleteFramebuffers(1, &_framebuffer);
    if (_color)
      glDeleteRenderbuffers(1, &_color);
    if (_depth)
      glDeleteRenderbuffers(1, &_depth);
  } else {
    if (_fusion)
      _fusion->abandon_context();
    _fusion.reset();
  }
}

@end
@interface FusionController : UIViewController {
  FusionView *_scene;
  UILabel *_status;
  UIButton *_pause;
  BOOL _running;
  CFTimeInterval _last;
  CADisplayLink *_displayLink;
}
@end
@implementation FusionController
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor colorWithRed:.025
                                              green:.045
                                               blue:.08
                                              alpha:1];
  UIStackView *stack = [[UIStackView alloc] init];
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 10;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.leadingAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor
                       constant:16],
    [stack.trailingAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor
                       constant:-16],
    [stack.topAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor
                       constant:12],
    [stack.bottomAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor
                       constant:-12]
  ]];
  UILabel * (^label)(NSString *, CGFloat) =
      ^UILabel *(NSString *text, CGFloat size) {
        UILabel *l = [[UILabel alloc] init];
        l.text = text;
        l.textColor = UIColor.whiteColor;
        l.font = [UIFont systemFontOfSize:size];
        l.numberOfLines = 0;
        [l setContentHuggingPriority:UILayoutPriorityRequired
                             forAxis:UILayoutConstraintAxisVertical];
        return l;
      };
  [stack addArrangedSubview:label(@"Mobile Point Fusion", 26)];
  [stack addArrangedSubview:
             label(@"On-device shader + CPU execution · generated input", 15)];
  [stack
      addArrangedSubview:label(@"Supplied poses · no real RGB-D · no tracking",
                               13)];
  UIStackView *head = [[UIStackView alloc] init];
  head.distribution = UIStackViewDistributionFillEqually;
  [head addArrangedSubview:label(@"CURRENT OBSERVATION", 12)];
  [head addArrangedSubview:label(@"ACCUMULATED MODEL", 12)];
  [stack addArrangedSubview:head];
  try {
    _scene = [[FusionView alloc] initWithFrame:CGRectZero];
    [stack addArrangedSubview:_scene];
    [_scene.heightAnchor constraintGreaterThanOrEqualToConstant:180].active =
        YES;
  } catch (const std::exception &e) {
    [stack
        addArrangedSubview:label([NSString stringWithUTF8String:e.what()], 15)];
    [stack addArrangedSubview:[UIView new]];
    return;
  }
  _status = label(stats(_scene->_fusion.get()), 13);
  _status.font = [UIFont monospacedSystemFontOfSize:12
                                             weight:UIFontWeightRegular];
  [stack addArrangedSubview:_status];
  UIStackView *buttons = [[UIStackView alloc] init];
  buttons.spacing = 16;
  _pause = [UIButton buttonWithType:UIButtonTypeSystem];
  [_pause setTitle:@"Pause" forState:UIControlStateNormal];
  [_pause addTarget:self
                action:@selector(toggle)
      forControlEvents:UIControlEventTouchUpInside];
  [buttons addArrangedSubview:_pause];
  UIButton *reset = [UIButton buttonWithType:UIButtonTypeSystem];
  [reset setTitle:@"Reset and run" forState:UIControlStateNormal];
  [reset addTarget:self
                action:@selector(reset)
      forControlEvents:UIControlEventTouchUpInside];
  [buttons addArrangedSubview:reset];
  [stack addArrangedSubview:buttons];
  [stack addArrangedSubview:
             label(@"Drag to orbit. Each new view runs correspondence shaders, "
                   @"weighted fusion and overlap removal on this device.",
                   12)];
  [stack
      addArrangedSubview:
          label(@"2015: D. Ben-Hoda, G. Kamar, A. Wetzler, Y. Honen, R. "
                @"Kimmel\n2026 adapter · Daniel & Gal source notices preserved",
                10)];
  _running = YES;
  _displayLink = [CADisplayLink displayLinkWithTarget:self
                                             selector:@selector(tick:)];
  _displayLink.preferredFramesPerSecond = 30;
  [_displayLink addToRunLoop:NSRunLoop.mainRunLoop
                     forMode:NSRunLoopCommonModes];
  [NSNotificationCenter.defaultCenter
      addObserver:self
         selector:@selector(resignActive:)
             name:UIApplicationWillResignActiveNotification
           object:nil];
  [NSNotificationCenter.defaultCenter
      addObserver:self
         selector:@selector(becomeActive:)
             name:UIApplicationDidBecomeActiveNotification
           object:nil];
}
- (void)toggle {
  _running = !_running;
  [_pause setTitle:_running ? @"Pause" : @"Resume"
          forState:UIControlStateNormal];
}
- (void)resignActive:(NSNotification *)note {
  _displayLink.paused = YES;
  _scene->_canDraw = NO;
  [EAGLContext setCurrentContext:_scene->_context];
  glFinish();
  NSLog(@"FUSION_LIFECYCLE inactive GL flushed");
}
- (void)becomeActive:(NSNotification *)note {
  _scene->_canDraw = YES;
  [_scene setNeedsLayout];
  _last = 0;
  _displayLink.paused = NO;
  NSLog(@"FUSION_LIFECYCLE active");
}
- (void)reset {
  if (!_scene || !_scene->_canDraw)
    return;
  try {
    [EAGLContext setCurrentContext:_scene->_context];
    _scene->_fusion->reset();
    _scene->_orbit = 0;
    _running = YES;
    [_pause setTitle:@"Pause" forState:UIControlStateNormal];
  } catch (const std::exception &e) {
    _running = NO;
    _status.text = [NSString stringWithUTF8String:e.what()];
  }
}
- (void)dealloc {
  [_displayLink invalidate];
  [NSNotificationCenter.defaultCenter removeObserver:self];
}
- (void)tick:(CADisplayLink *)link {
  if (!_scene || !_scene->_canDraw ||
      UIApplication.sharedApplication.applicationState !=
          UIApplicationStateActive)
    return;
  if (_scene->_error) {
    _running = NO;
    _status.text = _scene->_error;
    return;
  }
  try {
    if (_running && link.timestamp - _last > .15) {
      [EAGLContext setCurrentContext:_scene->_context];
      _scene->_fusion->step();
      _last = link.timestamp;
    }
    [_scene draw];
    _status.text = stats(_scene->_fusion.get());
  } catch (const std::exception &e) {
    _running = NO;
    _status.text =
        [NSString stringWithFormat:@"Execution failed: %s", e.what()];
    NSLog(@"Fusion failure: %s", e.what());
  }
}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options {
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = [FusionController new];
  [self.window makeKeyAndVisible];
  return YES;
}
@end
int main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(AppDelegate.class));
  }
}
#else
@interface FusionView : NSOpenGLView {
@public
  std::unique_ptr<LiveFusion> _fusion;
  float _orbit;
  NSString *_error;
}
@end
@implementation FusionView
- (instancetype)initWithFrame:(NSRect)frame {
  NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAOpenGLProfile,
                                          NSOpenGLProfileVersion4_1Core,
                                          NSOpenGLPFADoubleBuffer,
                                          NSOpenGLPFAColorSize,
                                          24,
                                          NSOpenGLPFADepthSize,
                                          24,
                                          0};
  self = [super
      initWithFrame:frame
        pixelFormat:[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs]];
  if (self) {
    self.wantsBestResolutionOpenGLSurface = YES;
    [self.openGLContext makeCurrentContext];
    _fusion = std::make_unique<LiveFusion>(resourceRoot(), outputRoot());
  }
  return self;
}
- (void)drawRect:(NSRect)dirtyRect {
  if (self.bounds.size.width <= 0 || self.bounds.size.height <= 0)
    return;
  try {
    [self.openGLContext makeCurrentContext];
    NSRect b = [self convertRectToBacking:self.bounds];
    _fusion->render(0, b.size.width, b.size.height, _orbit);
    [self.openGLContext flushBuffer];
  } catch (const std::exception &e) {
    _error = [NSString stringWithUTF8String:e.what()];
    NSLog(@"Fusion draw failure: %@", _error);
  }
}
- (void)dealloc {
  [self.openGLContext makeCurrentContext];
  _fusion.reset();
}
- (void)mouseDragged:(NSEvent *)event {
  _orbit += event.deltaX / 180.f;
  [self setNeedsDisplay:YES];
}
@end
@interface AppDelegate : NSObject <NSApplicationDelegate> {
  NSWindow *_window;
  FusionView *_scene;
  NSTextField *_status;
  NSButton *_pause;
  BOOL _running;
}
@end
@implementation AppDelegate
- (NSTextField *)label:(NSString *)text frame:(NSRect)frame size:(CGFloat)size {
  NSTextField *l = [NSTextField labelWithString:text];
  l.frame = frame;
  l.font = [NSFont systemFontOfSize:size];
  l.textColor = NSColor.whiteColor;
  l.maximumNumberOfLines = 0;
  [_window.contentView addSubview:l];
  return l;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  _window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 1180, 800)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _window.title = @"Mobile Point Fusion — native execution";
  _window.backgroundColor = [NSColor colorWithRed:.025
                                            green:.045
                                             blue:.08
                                            alpha:1];
  [_window center];
  [self label:@"Mobile Point Fusion"
        frame:NSMakeRect(24, 742, 1000, 36)
         size:28];
  [self label:@"Native shader + CPU execution · generated observations · "
              @"supplied poses · no real RGB-D or tracking"
        frame:NSMakeRect(24, 710, 1100, 25)
         size:15];
  [self label:@"CURRENT OBSERVATION"
        frame:NSMakeRect(30, 674, 550, 24)
         size:13];
  [self label:@"ACCUMULATED MODEL" frame:NSMakeRect(606, 674, 550, 24) size:13];
  try {
    _scene = [[FusionView alloc] initWithFrame:NSMakeRect(24, 174, 1132, 490)];
    [_window.contentView addSubview:_scene];
  } catch (const std::exception &e) {
    [self label:[NSString stringWithUTF8String:e.what()]
          frame:NSMakeRect(24, 250, 1100, 250)
           size:16];
  }
  _status = [self label:stats(_scene ? _scene->_fusion.get() : nullptr)
                  frame:NSMakeRect(24, 112, 850, 55)
                   size:14];
  _pause = [NSButton buttonWithTitle:@"Pause"
                              target:self
                              action:@selector(toggle)];
  _pause.frame = NSMakeRect(920, 126, 95, 30);
  [_window.contentView addSubview:_pause];
  NSButton *reset = [NSButton buttonWithTitle:@"Reset and run"
                                       target:self
                                       action:@selector(reset)];
  reset.frame = NSMakeRect(1016, 126, 140, 30);
  [_window.contentView addSubview:reset];
  [self label:@"Drag either view to orbit. Each view executes correspondence, "
              @"fusion and bounded overlap removal."
        frame:NSMakeRect(24, 73, 1100, 24)
         size:13];
  [self label:@"2015: D. Ben-Hoda, G. Kamar, A. Wetzler, Y. Honen, R. Kimmel · "
              @"2026 adapter · Daniel & Gal notices preserved"
        frame:NSMakeRect(24, 30, 1100, 24)
         size:12];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  _running = YES;
  [NSTimer scheduledTimerWithTimeInterval:.15
                                   target:self
                                 selector:@selector(tick:)
                                 userInfo:nil
                                  repeats:YES];
}
- (void)toggle {
  _running = !_running;
  _pause.title = _running ? @"Pause" : @"Resume";
}
- (void)reset {
  if (!_scene)
    return;
  [_scene.openGLContext makeCurrentContext];
  _scene->_fusion->reset();
  _scene->_orbit = 0;
  _running = YES;
  _pause.title = @"Pause";
}
- (void)tick:(NSTimer *)timer {
  if (!_scene)
    return;
  try {
    [_scene.openGLContext makeCurrentContext];
    if (_running)
      _scene->_fusion->step();
    _status.stringValue =
        _scene->_error ? _scene->_error : stats(_scene->_fusion.get());
    [_scene setNeedsDisplay:YES];
  } catch (const std::exception &e) {
    _running = NO;
    _status.stringValue =
        [NSString stringWithFormat:@"Execution failed: %s", e.what()];
    NSLog(@"Fusion failure: %s", e.what());
  }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  return YES;
}
@end
int main(int argc, char **argv) {
  @autoreleasepool {
    NSApplication *app = NSApplication.sharedApplication;
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    AppDelegate *delegate = [AppDelegate new];
    app.delegate = delegate;
    [app run];
    return 0;
  }
}
#endif
