//
//  ViewController.m
//  PointsFusion
//
//  Created by admin_gip17 on 5/7/14.
//  Copyright (c) 2014 Daniel & Gal. All rights reserved.
//

#import "ViewController.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#define BUFFER_INDEX_OFFSET(i) ((GLuint *)NULL + (i))

#define LL_F 12 //Line Length in GLfloat.
#define LL (LL_F * sizeof(GLfloat)) //Line Length in bytes.
#define MAX_RADIUS 6.0f
#define MIN_CONFIDENCE 0.0f
#define STABLE_CONFIDENCE 10.0f
#define GESTURE_SENSITIVITY 0.1
#define FRUSTRUM_ANGLE 65.0f
#define SUPER_SAMPLE_FACTOR 4
#define X_OFFSET 0
#define Y_OFFSET 1
#define Z_OFFSET 2
#define NX_OFFSET 3
#define NY_OFFSET 4
#define NZ_OFFSET 5
#define RADIUS_OFFSET 6
#define RED_OFFSET 7
#define GREEN_OFFSET 8
#define BLUE_OFFSET 9
#define CONFIDENCE_OFFSET 10
#define TIMESTAMP_OFFSET 11
#define RESOLUTION_X    640
#define RESOLUTION_Y    480
#define MAX_TIMESTAMP 6
#define ITERATIONS_UPDATE_DELETE 25
#define DELTA_RADIUS 0.5f
#define MAX_DEGREE 20 //convert to radians


#define DEGREES_TO_RADIANS(degrees) ((degrees) / 180.0 * M_PI)
#define TEST_ERRORS(g)  err = glGetError();\
                        if (err)\
                            printf("error%d: %d\n", g, err);
#define TEST_FBO(g)     if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)\
                            printf("FrameBuffer not complete (%d)\n", g);
#define TEST_ALL(g) TEST_ERRORS(g) TEST_FBO(g)

//control options
#define WORLD_SPACE  0
#define OBJECT_SPACE  1

// Uniform index.
enum
{
    UNIFORM_MODELVIEWPROJECTION_MATRIX,
    UNIFORM_MODELVIEW_MATRIX,
    UNIFORM_NORMAL_MATRIX,
    UNIFORM_IS_NORMALS_MODE,
    UNIFORM_MODEL_SIZE,
    UNIFORM_TIME_STAMP,
    UNIFORM_INDEX_TEXTURE,
    UNIFORM_POSITION_TEXTURE,
    UNIFORM_NORMAL_TEXTURE,
    UNIFORM_X_FACTOR,
    UNIFORM_Y_FACTOR,
    UNIFORM_SUPER_SAMPLE_FACTOR,
    UNIFORM_MAX_DEGREE,
    UNIFORM_STABLE_CONFIDENCE,
    NUM_UNIFORMS
};
GLint uniforms[NUM_UNIFORMS];

// Attribute index.
enum
{
    ATTRIB_VERTEX,
    ATTRIB_NORMAL,
    ATTRIB_COLOR,
    ATTRIB_CONFIDENCE,
    ATTRIB_RADIUS,
    NUM_ATTRIBUTES
};


// Data layout for each line below is:
// position X, Y, Z,    normal X, Y, Z,     radius, color R, G, B


@interface ModelPoint {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
    GLfloat radius;
    GLfloat R;
    GLfloat G;
    GLfloat B;
    GLfloat conf;
    int timeStamp;
    bool isDynamic;
}
@end

//@interface VertexData () {
//    GLKVector3  position;
//    GLKVector3  normal;
//    GLfloat     radius;
//    GLKVector3  color;
//    GLint       confidence;
//    GLint       timestamp;
//    bool        isDynamic;
//    
//}

@interface ViewController () {
    GLuint _program;
    GLuint _fusionProg;
    GLuint _pointRemovalProg;
    //testing texture variables
    GLuint  _IndexMapProg;
    NSDate *programStartTime;
    NSTimeInterval interval;
    //NSDate *startUpdates;
    
    //geometry
    GLKMatrix4 _modelViewProjectionMatrix;
    GLKMatrix4 projectionMatrix;
    GLKMatrix4 modelViewMatrix;
    GLKMatrix3 _normalMatrix;
    
    // Buffers etc.
    GLuint  modelVA;
    GLuint  inputVA;
    GLuint _modelBuffer;
    GLuint _inputBuffer;
    GLuint indexStableBuffer;
    GLuint indexModelBuffer;
    GLuint indexInputBuffer;
    GLuint indexMapTexture;
    GLuint fusionTexture;
    GLuint positionTexture;
    GLuint normalTexture;
    GLuint indexMapFBO;
    GLuint fusionFBO;
    GLuint indexMapDepthBuffer;
    GLuint displayFBO;
    GLuint displayRenBuff;
    GLuint64 maxNumPointsGPU;
    // Loading data

    NSMutableArray* freeIndexQueue;
    GLfloat* modelData;
    GLfloat* inputData;
    GLfloat* fusionOutputTex;
    GLuint* indicesInput;
    GLuint* indicesStable;
    GLuint* indicesModel;
    int vertices_num;
    int vertices_file;
    int maxCurrIndex;
    int vertices_num_stable;
    GLuint64 updateIteration;
    //GLuint64 iterationPerSec;
    
    // Gestures variables
    GLfloat cameraYawPrev;
    GLfloat cameraPitchPrev;
    GLfloat cameraRollPrev;
    GLfloat modelScalePrev;
    GLfloat translateXprev;
    GLfloat translateYprev;
    
    // Modes
    GLboolean isNormalsMode;
    GLboolean controlMode;
    
    int err;
}
@property (strong, nonatomic) EAGLContext *context;

- (void)setupGL;
- (void)tearDownGL;
- (void)loadFile;

- (BOOL)loadShaders;
- (BOOL)loadFusionShaders;
- (BOOL)loadIndexMapShaders;
- (BOOL)compileShader:(GLuint *)shader type:(GLenum)type file:(NSString *)file;
- (BOOL)linkProgram:(GLuint)prog;
- (BOOL)validateProgram:(GLuint)prog;
@end

@implementation ViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    isNormalsMode = FALSE;
    
    //setting OpenGL version.
    self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!self.context)
    {
        NSLog(@"Failed to create ES context");
    }
    
    GLKView *view = (GLKView *)self.view;
    view.context = self.context;
    view.drawableDepthFormat = GLKViewDrawableDepthFormat24;
    maxNumPointsGPU = 1000000;

    [self loadFile];
    [self setupGL];
    
    //Gesture control
    UIRotationGestureRecognizer *rotate = [[UIRotationGestureRecognizer alloc] initWithTarget:self action:@selector(rotateRecognized:)];
    [self.view addGestureRecognizer:(rotate)];
    
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(panRecognized:)];
    [self.view addGestureRecognizer:(pan)];
    
    UIPinchGestureRecognizer *pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinchRecognized:)];
    [self.view addGestureRecognizer:(pinch)];
   
    self.view.multipleTouchEnabled = YES;
}

-(void)rotateRecognized:(UIRotationGestureRecognizer *)rotateGes
{
    if (rotateGes.state == UIGestureRecognizerStateBegan)
    {
        cameraRollPrev = 0;
    }
    GLfloat currentCameraRoll = -rotateGes.rotation;
    currentCameraRoll -= cameraRollPrev;
    GLKMatrix4 auxMatrix = GLKMatrix4Identity;
    if (controlMode  == WORLD_SPACE)
    {
        auxMatrix = GLKMatrix4RotateZ(auxMatrix, currentCameraRoll);
        modelViewMatrix = GLKMatrix4Multiply(auxMatrix ,modelViewMatrix);
    }
    else if (controlMode == OBJECT_SPACE)
    {
        auxMatrix = GLKMatrix4RotateZ(auxMatrix, currentCameraRoll);
        modelViewMatrix = GLKMatrix4Multiply(modelViewMatrix, auxMatrix);
    }
    cameraRollPrev += currentCameraRoll;
}

-(void)panRecognized:(UIPanGestureRecognizer *)panGes
{
    if (panGes.state == UIGestureRecognizerStateBegan)
    {
        switch (panGes.numberOfTouches)
        {
            case 1:
                cameraYawPrev = 0.0f;
                cameraPitchPrev = 0.0f;
                break;
            case 2:
                translateXprev = 0.0f;
                translateYprev = 0.0f;
                break;
        }
    }
    CGPoint translationOnScreen = [panGes translationInView:(self.view)];
    GLfloat aspect = self.view.bounds.size.height / self.view.bounds.size.width;
    GLKVector3 translate;
    GLfloat currentCameraYaw, currentCameraPitch;
    GLKMatrix4 auxMatrix = GLKMatrix4Identity;
    GLfloat translateSense  = 1.0f/250.0f;
    switch (panGes.numberOfTouches)
    {
        case 1:
            if (controlMode == WORLD_SPACE)
            {
                currentCameraYaw = -(translationOnScreen.x / self.view.bounds.size.width)* GLKMathDegreesToRadians(FRUSTRUM_ANGLE);
                currentCameraPitch = -(translationOnScreen.y /self.view.bounds.size.height) * GLKMathDegreesToRadians(FRUSTRUM_ANGLE) * aspect;
                currentCameraPitch -= cameraPitchPrev;
                currentCameraYaw -= cameraYawPrev;auxMatrix = GLKMatrix4RotateX(auxMatrix, currentCameraPitch);
                modelViewMatrix = GLKMatrix4Multiply(auxMatrix ,modelViewMatrix);
                auxMatrix = GLKMatrix4Identity;
                auxMatrix = GLKMatrix4RotateY(auxMatrix, currentCameraYaw);
                modelViewMatrix = GLKMatrix4Multiply(auxMatrix ,modelViewMatrix);
            }
            else if (controlMode == OBJECT_SPACE)
            {
                currentCameraYaw = -(translationOnScreen.x / self.view.bounds.size.width) * 2 * M_PI;
                currentCameraPitch = -(translationOnScreen.y /self.view.bounds.size.height) * 2 * M_PI;
                currentCameraPitch -= cameraPitchPrev;
                currentCameraYaw -= cameraYawPrev;
                auxMatrix = GLKMatrix4RotateX(auxMatrix, currentCameraPitch);
                modelViewMatrix = GLKMatrix4Multiply(modelViewMatrix, auxMatrix);
                auxMatrix = GLKMatrix4Identity;
                auxMatrix = GLKMatrix4RotateY(auxMatrix, currentCameraYaw);
                modelViewMatrix = GLKMatrix4Multiply(modelViewMatrix, auxMatrix);
            }
            
            cameraYawPrev += currentCameraYaw;
            cameraPitchPrev += currentCameraPitch;
            break;
        
        case 2:
            translate = GLKVector3Make((translationOnScreen.x - translateXprev) * translateSense, 0.0f, (translationOnScreen.y - translateYprev) * translateSense);
            if (controlMode == WORLD_SPACE)
            {
                auxMatrix = GLKMatrix4TranslateWithVector3(auxMatrix, translate);
                modelViewMatrix = GLKMatrix4Multiply(auxMatrix ,modelViewMatrix);
            }
            else if (controlMode == OBJECT_SPACE)
            {
                auxMatrix = GLKMatrix4TranslateWithVector3(auxMatrix, translate);
                modelViewMatrix = GLKMatrix4Multiply(modelViewMatrix, auxMatrix);
            }
            translateXprev += translate.x / translateSense;
            translateYprev += translate.z / translateSense;
            break;
    }
}

-(void)pinchRecognized:(UIPinchGestureRecognizer *)pinchGes
{
    if (pinchGes.state == UIGestureRecognizerStateBegan)
    {
        modelScalePrev = 1;
    }
    GLfloat modelScale = pinchGes.scale / modelScalePrev;
    modelViewMatrix = GLKMatrix4Scale(modelViewMatrix, modelScale, modelScale , modelScale);
    modelScalePrev *= modelScale;
}

- (IBAction)toggleNormals:(UISwitch *)sender
{
    isNormalsMode = !isNormalsMode;
}

- (IBAction)toggleControlMode:(UISwitch *)sender
{
    controlMode = !controlMode;
}

- (void)loadFile
{
    NSString* filePath = @"/monkey";//file path...
    NSString* fileRoot = [[NSBundle mainBundle]
                          pathForResource:filePath ofType:@"obj"];
    
    // read everything from text
    NSString* fileContents = [NSString stringWithContentsOfFile:fileRoot
                            encoding:NSUTF8StringEncoding error:nil];
    
    // first, separate by new line
    NSArray* allLinedStrings =
    [fileContents componentsSeparatedByCharactersInSet:
     [NSCharacterSet newlineCharacterSet]];
    unsigned long int size = [allLinedStrings count];
    
    vertices_file = 0;
    for (int i = 0; i < size; ++i)
    {
        // then break down even further
        NSString* currentLine =
        [allLinedStrings objectAtIndex:i];
        
        // choose whatever input identity you have decided. in this case ;
        NSArray* singleStrs = [currentLine componentsSeparatedByCharactersInSet:
                               [NSCharacterSet characterSetWithCharactersInString:@" "]];
        
        if ([singleStrs[0] isEqualToString:@"v"])
        {
            ++vertices_file;
        }
    }
    ++vertices_file;
    //modelData = [[NSMutableArray alloc] initWithCapacity: vertices_num];
    modelData = malloc(maxNumPointsGPU * LL);
    //inputData = malloc(RESOLUTION_X * RESOLUTION_Y * LL);
    inputData = malloc(vertices_file * LL);
 
    
    GLfloat* normalBuff = malloc(vertices_file * sizeof(GLfloat) * 3);
    //not real point 0
    inputData[X_OFFSET] = 0.0;
    inputData[Y_OFFSET] = 0.0;
    inputData[Z_OFFSET] = 0.0;
    inputData[NX_OFFSET] = 0.0;
    inputData[NY_OFFSET] = 0.0;
    inputData[NZ_OFFSET] = 1.0;
    int currVertex = 1;
    int currNormal = 1;
    
    for (int i = 0; i < size; ++i)
    {
        // then break down even further
        NSString* currentLine =
        [allLinedStrings objectAtIndex:i];
        
        // choose whatever input identity you have decided. in this case ;
        NSArray* singleStrs = [currentLine componentsSeparatedByCharactersInSet:
                               [NSCharacterSet characterSetWithCharactersInString:@" "]];
        
        if ([singleStrs[0] isEqualToString:@"v"])
        {
            inputData[currVertex * LL_F + X_OFFSET] = [singleStrs[1] floatValue];
            inputData[currVertex * LL_F + Y_OFFSET] = [singleStrs[2] floatValue];
            inputData[currVertex * LL_F + Z_OFFSET] = [singleStrs[3] floatValue];
            ++currVertex;
        }
        else if ([singleStrs[0] isEqualToString:@"vn"] &&
                 (currNormal <= vertices_file))
        {
            normalBuff[currNormal * 3] = [singleStrs[1] floatValue];
            normalBuff[currNormal * 3 + 1] = [singleStrs[2] floatValue];
            normalBuff[currNormal * 3 + 2] = [singleStrs[3] floatValue];
            ++currNormal;
        }
        else if ([singleStrs[0] isEqualToString:@"f"])
        {
            for (int j = 1; j < singleStrs.count; ++j)
            {
                NSArray* faceVertices = [singleStrs[j] componentsSeparatedByCharactersInSet:
                                       [NSCharacterSet characterSetWithCharactersInString:@"/"]];
                if (faceVertices.count >= 3)
                {
                    int vertexID = [faceVertices[0] intValue];
                    int normalID = [faceVertices[2] intValue] ;
                    inputData[vertexID * LL_F + NX_OFFSET] = normalBuff[normalID * 3];
                    inputData[vertexID * LL_F + NY_OFFSET] = normalBuff[normalID * 3 + 1];
                    inputData[vertexID * LL_F + NZ_OFFSET] = normalBuff[normalID * 3 + 2];
                }
                else
                {
                    //only texture do nothing
                }
            }
        }
    }
    free(normalBuff);
}

- (void)dealloc
{    
    [self tearDownGL];
    free(modelData);
    free(inputData);
    free(indicesInput);
    free(indicesModel);
    free(indicesStable);
    free(fusionOutputTex);
    if ([EAGLContext currentContext] == self.context)
    {
        [EAGLContext setCurrentContext:nil];
    }
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    if ([self isViewLoaded] && ([[self view] window] == nil))
    {
        self.view = nil;
        [self tearDownGL];
        if ([EAGLContext currentContext] == self.context)
        {
            [EAGLContext setCurrentContext:nil];
        }
        self.context = nil;
    }
    // Dispose of any resources that can be recreated.
}

- (void)setupGL
{
    [EAGLContext setCurrentContext:self.context];
    [self loadShaders];
    [self loadIndexMapShaders];
    [self loadFusionShaders];
    [self loadRemovalShaders];
    freeIndexQueue = [[NSMutableArray alloc] init];
    
    updateIteration = 0;
    vertices_num = 0;
    maxCurrIndex = 0;
    //Setting the time the program started
    programStartTime = [NSDate date];
    
    // Enabling hiding of surfaces.
    glEnable(GL_DEPTH_TEST);

    // Geometry
    float aspect = fabsf(self.view.bounds.size.width / self.view.bounds.size.height);
    projectionMatrix =  GLKMatrix4MakePerspective(GLKMathDegreesToRadians(FRUSTRUM_ANGLE), aspect, 0.1f, 100.0f);
    modelViewMatrix = GLKMatrix4Identity;
    modelViewMatrix = GLKMatrix4Translate(modelViewMatrix, 0.0f, 0.0f, -8.0f);
    
    //int width = self.view.bounds.size.width;
    //int hight = self.view.bounds.size.height;
    
    //Allocate the temp array for the textures read from GPU.
    fusionOutputTex = malloc(RESOLUTION_X * RESOLUTION_Y * sizeof(GLfloat) * 4);
    
    //Allocate the stable updated points array for the pointsRemoval shader
    indicesStable = malloc(RESOLUTION_X * RESOLUTION_Y * sizeof(GLuint));
    
    glGenBuffers(1, &indexStableBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexStableBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, RESOLUTION_X * RESOLUTION_Y * sizeof(GLuint), NULL, GL_DYNAMIC_DRAW);
    
    // Set input array and buffer
    glGenVertexArrays(1, &inputVA);
    glBindVertexArray(inputVA);
    
    glGenBuffers(1, &_inputBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _inputBuffer);
    //glBufferData(GL_ARRAY_BUFFER, RESOLUTION_X * RESOLUTION_Y * sizeof(GLfloat) * 3, inputData, GL_DYNAMIC_DRAW);
    glBufferData(GL_ARRAY_BUFFER, vertices_file * LL, inputData, GL_DYNAMIC_DRAW);
    
    
    // Set index buffer and fill it
    
    //indicesInput = malloc(RESOLUTION_X * RESOLUTION_Y * sizeof(GLuint));
    //for(int i = 0; i < RESOLUTION_X * RESOLUTION_Y; ++i)
    indicesInput = malloc(vertices_file * sizeof(GLuint));
    for(int i = 0; i < vertices_file - 1; ++i) //changed size only for presentation
    {
        indicesInput[i] = i + 1;
    }
    glGenBuffers(1, &indexInputBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexInputBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, RESOLUTION_X * RESOLUTION_Y * sizeof(GLuint), indicesInput, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(ATTRIB_VERTEX);
    glEnableVertexAttribArray(ATTRIB_NORMAL);
    
    glVertexAttribPointer(ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, LL, 0);
    glVertexAttribPointer(ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(3 * sizeof(GLfloat)));
    
    
    // Set vertex array and buffer
    glGenVertexArrays(1, &modelVA);
    glBindVertexArray(modelVA);
    
    glGenBuffers(1, &_modelBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
    
    TEST_ERRORS(0)
    glBufferData(GL_ARRAY_BUFFER, maxNumPointsGPU * LL, NULL, GL_DYNAMIC_DRAW);
    
    indicesModel = malloc(maxNumPointsGPU * sizeof(GLuint));
    for(int i = 0; i < maxNumPointsGPU; ++i)
    {
        indicesModel[i] = 0;
    }
    glGenBuffers(1, &indexModelBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, maxNumPointsGPU * sizeof(GLuint), indicesModel, GL_DYNAMIC_DRAW);
    
    
    // Specify all the vertices attributes and set their pointers
    glEnableVertexAttribArray(ATTRIB_VERTEX);
    glEnableVertexAttribArray(ATTRIB_NORMAL);
    glEnableVertexAttribArray(ATTRIB_COLOR);
    glEnableVertexAttribArray(ATTRIB_RADIUS);
    glEnableVertexAttribArray(ATTRIB_CONFIDENCE);
   
    glVertexAttribPointer(ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(0));
    glVertexAttribPointer(ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(3 * sizeof(GLfloat)));
    glVertexAttribPointer(ATTRIB_COLOR, 3, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(7 * sizeof(GLfloat)));
    glVertexAttribPointer(ATTRIB_RADIUS, 1, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(6 * sizeof(GLfloat)));
    glVertexAttribPointer(ATTRIB_CONFIDENCE, 1, GL_FLOAT, GL_FALSE, LL, BUFFER_OFFSET(10 * sizeof(GLfloat)));

    
    //Adding textures
    glGenFramebuffers(1, &indexMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, indexMapFBO);
    
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &indexMapTexture);
    glBindTexture(GL_TEXTURE_2D, indexMapTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32I, RESOLUTION_X * SUPER_SAMPLE_FACTOR, RESOLUTION_Y * SUPER_SAMPLE_FACTOR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, indexMapTexture, 0);
  
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &positionTexture);
    glBindTexture(GL_TEXTURE_2D, positionTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, RESOLUTION_X * SUPER_SAMPLE_FACTOR, RESOLUTION_Y * SUPER_SAMPLE_FACTOR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, positionTexture, 0);
    
    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &normalTexture);
    glBindTexture(GL_TEXTURE_2D, normalTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, RESOLUTION_X * SUPER_SAMPLE_FACTOR, RESOLUTION_Y * SUPER_SAMPLE_FACTOR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, normalTexture, 0);
    
    GLenum indexBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, indexBuffers);
    glGenFramebuffers(1, &fusionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, fusionFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, indexMapTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, positionTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, normalTexture, 0);
    
    glActiveTexture(GL_TEXTURE3);
    glGenTextures(1, &fusionTexture);
    glBindTexture(GL_TEXTURE_2D, fusionTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, RESOLUTION_X, RESOLUTION_Y);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, fusionTexture, 0);
    
    GLenum fusionBuffers[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, fusionBuffers);
    TEST_ERRORS(1)
    TEST_ALL(2)
}

- (void)tearDownGL
{
    [EAGLContext setCurrentContext:self.context];
    glDeleteBuffers(1, &_modelBuffer);
    if (_program)
    {
        glDeleteProgram(_program);
        _program = 0;
    }
    if (_IndexMapProg)
    {
        glDeleteProgram(_IndexMapProg);
        _IndexMapProg = 0;
    }
    if (_fusionProg)
    {
        glDeleteProgram(_fusionProg);
        _fusionProg = 0;
    }
    if (_pointRemovalProg)
    {
        glDeleteProgram(_pointRemovalProg);
        _pointRemovalProg = 0;
    }
    
}

#pragma mark - GLKView and GLKViewController delegate methods

- (void)update
{
//    if (!updateIteration){
//        startUpdates = [NSDate date];
//        iterationPerSec = 0;
//    }
//    float timeinterval = [[NSDate date] timeIntervalSinceDate:startUpdates];
//    if ( timeinterval > 1) {
//        NSLog(@"number of updates per sec = %llu in %f time", updateIteration - iterationPerSec, timeinterval);
//        iterationPerSec = updateIteration;
//        startUpdates = [NSDate date];
//        
//    }
    // Compute the model view matrix for the object rendered with GLKit
    _normalMatrix = GLKMatrix3InvertAndTranspose(GLKMatrix4GetMatrix3(modelViewMatrix), NULL);
    _modelViewProjectionMatrix = GLKMatrix4Multiply(projectionMatrix, modelViewMatrix);
    glUseProgram(_IndexMapProg);
    glBindFramebuffer(GL_FRAMEBUFFER, indexMapFBO);
    glViewport(0,0,RESOLUTION_X * SUPER_SAMPLE_FACTOR, RESOLUTION_Y * SUPER_SAMPLE_FACTOR);

    
    TEST_ALL(10)
    
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glUniformMatrix4fv(uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX], 1, 0, _modelViewProjectionMatrix.m);
    glUniformMatrix4fv(uniforms[UNIFORM_MODELVIEW_MATRIX], 1, 0, modelViewMatrix.m);
    glUniformMatrix3fv(uniforms[UNIFORM_NORMAL_MATRIX], 1, 0, _normalMatrix.m);
  
    glBindVertexArray(modelVA);
    glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer);
    glDrawElements(GL_POINTS, maxCurrIndex , GL_UNSIGNED_INT, 0);
    
    TEST_ALL(11)
    
    //TESTING TEXTURES - to remove
        TEST_ALL(12)
    
    glBindVertexArray(inputVA);
    glBindFramebuffer(GL_FRAMEBUFFER, fusionFBO);
    glViewport(0,0, RESOLUTION_X, RESOLUTION_Y);
 
    // FUSION
    glUseProgram(_fusionProg);

    glUniformMatrix4fv(uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX], 1, 0, _modelViewProjectionMatrix.m);
    glUniformMatrix3fv(uniforms[UNIFORM_NORMAL_MATRIX], 1, 0, _normalMatrix.m);
    glUniform1i(uniforms[UNIFORM_INDEX_TEXTURE], 0);
    glUniform1i(uniforms[UNIFORM_POSITION_TEXTURE], 1);
    glUniform1i(uniforms[UNIFORM_NORMAL_TEXTURE], 2);
    glUniform1f(uniforms[UNIFORM_X_FACTOR],  RESOLUTION_X);
    glUniform1f(uniforms[UNIFORM_Y_FACTOR], RESOLUTION_Y);
    glUniform1i(uniforms[UNIFORM_SUPER_SAMPLE_FACTOR], SUPER_SAMPLE_FACTOR);
    glUniform1f(uniforms[UNIFORM_MAX_DEGREE], DEGREES_TO_RADIANS(MAX_DEGREE));


    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexInputBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _inputBuffer);
    //change number of vertices to res x * res y
    glDrawElements(GL_POINTS, vertices_file , GL_UNSIGNED_INT, 0);
 
   TEST_ALL(13)
    

    glReadBuffer(GL_COLOR_ATTACHMENT3);
    glReadPixels(0,0, RESOLUTION_X, RESOLUTION_Y, GL_RGBA, GL_FLOAT, fusionOutputTex);
    
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    

    
    //Fuse the points that the Fusion shader returned:
    //using inputVA atm
   // glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, _inputBuffer);
    //change to resx * res y
    void* _GPUinputDataPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, vertices_file * LL, GL_MAP_WRITE_BIT | GL_MAP_READ_BIT);
    
    glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
    void* _GPUmodelDataPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, maxNumPointsGPU * LL, GL_MAP_WRITE_BIT |
                                              GL_MAP_READ_BIT); //define or current max index
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer); //maybe not needed
    
    void* _GPUmodelIndexPtr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, maxNumPointsGPU * sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_READ_BIT);
 
    GLint modelID = 0;
    GLint inputID = 0;
    GLfloat currConf = 0;
    interval = [[NSDate date] timeIntervalSinceDate:programStartTime];
    GLfloat* currPointInputPtr = NULL;
    GLfloat* currPointModelPtr = NULL;
    GLuint* currIndexModelPtr = NULL;
    
    
    vertices_num_stable = 0;
    for(int i = 0; i < RESOLUTION_Y; ++i)
    {
        for(int j = 0; j < RESOLUTION_X; j += 4)
        {
            modelID = (GLint)(fusionOutputTex[(i * RESOLUTION_X + j)*4]);
            inputID = (GLint)(fusionOutputTex[(i * RESOLUTION_X + j)*4 + 1]);

            if(modelID != 0) //Got ID or -1 (new point)
            {
                //Read the point from GPU.
                currPointInputPtr = (GLfloat*)(_GPUinputDataPtr + (inputID * LL));
                currPointModelPtr = (GLfloat*)(_GPUmodelDataPtr + (modelID * LL));
                
                currConf = currPointModelPtr[CONFIDENCE_OFFSET];
                //find correct alpha gamma^2 = u^2 + v^ 2 s.t. projected coords are inv(k)*(u,v,1)
                float gamma2 = (fusionOutputTex[(i * RESOLUTION_X + j)*4 + 2]) * (fusionOutputTex[(i * RESOLUTION_X + j)*4 + 2]) + (fusionOutputTex[(i * RESOLUTION_X + j)*4 + 3])* (fusionOutputTex[(i * RESOLUTION_X + j)*4 + 3]);
                float sigma = 0.6f;
                float alpha = exp(-(gamma2)/(2* sigma * sigma));
                //float alpha = 1.0;
                if(modelID == -1) //New point
                {
                    //Add the point to the GPU.
                    ++vertices_num;
                    if([freeIndexQueue count])
                    {
                        NSNumber* freeIndex;
                        freeIndex = [freeIndexQueue pop];
                        currPointModelPtr = (GLfloat*)(_GPUmodelDataPtr + ([freeIndex integerValue]* LL));
                        currIndexModelPtr = (GLuint*)(_GPUmodelIndexPtr + ([freeIndex integerValue]* sizeof(GLuint)));
                        *currIndexModelPtr = [freeIndex integerValue];
                        //indicesModel[[freeIndex integerValue]] = [freeIndex integerValue];
                        
                        // possible memory leakage. need [freeIndex dealloc];
                    }else
                    {
                        currPointModelPtr = (GLfloat*)(_GPUmodelDataPtr + (++maxCurrIndex * LL));
                        currIndexModelPtr = (GLuint*)(_GPUmodelIndexPtr + (maxCurrIndex * sizeof(GLuint)));
                        
                        *currIndexModelPtr = maxCurrIndex;
                        //indicesModel[maxCurrIndex] = maxCurrIndex;
                    }
                    currPointModelPtr[X_OFFSET] = currPointInputPtr[X_OFFSET];
                    currPointModelPtr[Y_OFFSET] = currPointInputPtr[Y_OFFSET];
                    currPointModelPtr[Z_OFFSET] = currPointInputPtr[Z_OFFSET];
                    currPointModelPtr[NX_OFFSET] = currPointInputPtr[NX_OFFSET];
                    currPointModelPtr[NY_OFFSET] = currPointInputPtr[NY_OFFSET];
                    currPointModelPtr[NZ_OFFSET] = currPointInputPtr[NZ_OFFSET];
                    currPointModelPtr[CONFIDENCE_OFFSET] = alpha;
                    currPointModelPtr[TIMESTAMP_OFFSET] = interval;
                    currPointModelPtr[RADIUS_OFFSET] = MAX_RADIUS;
                    //divide by f
                    //currPointModelPtr[RADIUS_OFFSET] =  MIN(MAX_RADIUS,  MAX_RADIUS * (-currPointInputPtr[Z_OFFSET]/(sqrt(2)*currPointInputPtr[NZ_OFFSET])));
                }
                else //Match found
                {
                    //Calculate and updating new model values in GPU.
         
                    currPointModelPtr[CONFIDENCE_OFFSET] = currConf + alpha;
                    currPointModelPtr[TIMESTAMP_OFFSET] = interval;
                    //need to divid by f
                    //float newRadius = MIN(MAX_RADIUS,MAX_RADIUS * ( -currPointInputPtr[Z_OFFSET]/(sqrt(2)*currPointInputPtr[NZ_OFFSET])));
                    //if (newRadius < (1 + DELTA_RADIUS)*currPointModelPtr[RADIUS_OFFSET] ) {
                        currPointModelPtr[X_OFFSET] = (currConf * currPointModelPtr[X_OFFSET] + alpha * currPointInputPtr[X_OFFSET]) / (currConf + alpha);
                        currPointModelPtr[Y_OFFSET] = (currConf * currPointModelPtr[Y_OFFSET] + alpha * currPointInputPtr[Y_OFFSET]) / (currConf + alpha);
                        currPointModelPtr[Z_OFFSET] = (currConf * currPointModelPtr[Z_OFFSET] + alpha * currPointInputPtr[Z_OFFSET]) / (currConf + alpha);
                    
                        currPointModelPtr[NX_OFFSET] = (currConf * currPointModelPtr[NX_OFFSET] + alpha * currPointInputPtr[NX_OFFSET]) / (currConf + alpha);
                        currPointModelPtr[NY_OFFSET] = (currConf * currPointModelPtr[NY_OFFSET] + alpha * currPointInputPtr[NY_OFFSET]) / (currConf + alpha);
                        currPointModelPtr[NZ_OFFSET] = (currConf * currPointModelPtr[NZ_OFFSET] + alpha * currPointInputPtr[NZ_OFFSET]) / (currConf + alpha);
                   // }
                    
                    
                    if(currConf > STABLE_CONFIDENCE)
                    {
                        //point to be fused with is stable
                        indicesStable[vertices_num_stable] = modelID;
                        ++vertices_num_stable;
                    }
                }
            }

        }
    }
 
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    glUnmapBuffer(GL_ARRAY_BUFFER); //Flushes the whole _modelBuffer buffer
    glBindBuffer(GL_ARRAY_BUFFER, _inputBuffer);
    glUnmapBuffer(GL_ARRAY_BUFFER); //Flushes the whole _inputBuffer buffer
    
    //printf("Merged %d points\n", merged);
    
    
    //TODO: Change shader to pointsRemoval shader.
    glUseProgram(_pointRemovalProg);
    glUniformMatrix4fv(uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX], 1, 0, _modelViewProjectionMatrix.m);
    glUniformMatrix3fv(uniforms[UNIFORM_NORMAL_MATRIX], 1, 0, _normalMatrix.m);
    glUniform1i(uniforms[UNIFORM_INDEX_TEXTURE], 0);
    glUniform1i(uniforms[UNIFORM_POSITION_TEXTURE], 1);
    glUniform1i(uniforms[UNIFORM_NORMAL_TEXTURE], 2);
    glUniform1f(uniforms[UNIFORM_X_FACTOR],  RESOLUTION_X);
    glUniform1f(uniforms[UNIFORM_Y_FACTOR], RESOLUTION_Y);
    glUniform1i(uniforms[UNIFORM_SUPER_SAMPLE_FACTOR], SUPER_SAMPLE_FACTOR);
    glUniform1f(uniforms[UNIFORM_MAX_DEGREE], DEGREES_TO_RADIANS(MAX_DEGREE));
    
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    //Render the points indicated by stableUpdatedPoints array
    // maybe glBindBuffer for a GL_ELEMENT_ARRAY_BUFFER of a third kind would be better???
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexStableBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, vertices_num_stable * sizeof(GLint), indicesStable);
    
    
    glDrawElements(GL_POINTS, vertices_num_stable , GL_UNSIGNED_INT, 0);
    
    //Read the new texture to determine which points should be removed
    //and add them to the freeIndexQueue.
    // still active: glReadBuffer(GL_COLOR_ATTACHMENT3);
    glReadPixels(0,0, RESOLUTION_X, RESOLUTION_Y, GL_RGBA, GL_FLOAT, fusionOutputTex);
    //printf("Removing points\n");
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer);
    _GPUmodelIndexPtr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, maxNumPointsGPU * sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_READ_BIT);
    int IDToRemove;
    int num_removed = 0;
    int num_to_remove = 0;
    for(int i = 0; i < RESOLUTION_Y; ++i)
    {
        for(int j = 0; j < RESOLUTION_X; j += 4)
        {
            num_to_remove = (int)fusionOutputTex[(i * RESOLUTION_X  + j ) * 4];
            if (num_to_remove)
            {
                //Add to freeIndexQueue of available places (holes).
                for (int k = 1; k <= num_to_remove; ++k)
                {
                    IDToRemove = (int)fusionOutputTex[(i * RESOLUTION_X  + j ) * 4 + k];
                    NSNumber *currPointIDToRemove = [NSNumber numberWithInt:IDToRemove];
                    indicesModel[IDToRemove] = 0;
                    currIndexModelPtr = (GLuint*)(_GPUmodelIndexPtr + (IDToRemove * sizeof(GLuint)));
                    *currIndexModelPtr = 0;
                    num_removed++;
                    if(IDToRemove == maxCurrIndex)
                    {
                        //update maxCurrIndex if we removed last
                        NSNumber* freeIndexToRemove;
                        for(int k = maxCurrIndex - 1; k >= 0; --k)
                        {
                            if(indicesModel[k] > 0)
                            {
                                maxCurrIndex = k;
                                break;
                            }
                            else
                            {
                                freeIndexToRemove = [NSNumber numberWithInt:k];
                                [freeIndexQueue removeObject:freeIndexToRemove];
                            }
                        }
                    } else {
                        [freeIndexQueue push: currPointIDToRemove];
                    }
                    
                }
            }
        }
    }
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    //printf("number of points removed = %d\n", num_removed);
    
    if (++updateIteration % ITERATIONS_UPDATE_DELETE == 0) {
        glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
        void* _GPUmodelDataPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, maxNumPointsGPU * LL, GL_MAP_WRITE_BIT |
                                                  GL_MAP_READ_BIT); //define or current max index
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer);
        _GPUmodelIndexPtr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, maxNumPointsGPU * sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_READ_BIT);

        for(int i = 0; i < maxCurrIndex; ++i ) {
            currPointModelPtr = (GLfloat*)(_GPUmodelDataPtr + (i * LL));
            if (((interval - currPointModelPtr[TIMESTAMP_OFFSET]) > MAX_TIMESTAMP) &&
                (currPointModelPtr[CONFIDENCE_OFFSET] < STABLE_CONFIDENCE)) {
                //remove point
                currIndexModelPtr = (GLuint*)(_GPUmodelIndexPtr + (i * sizeof(GLuint)));
                *currIndexModelPtr = 0;
            }
        }
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    }
    

}


- (void)glkView:(GLKView *)view drawInRect:(CGRect)rect
{
   
    glUseProgram(_program);
    glUniformMatrix4fv(uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX], 1, 0, _modelViewProjectionMatrix.m);
    glUniformMatrix3fv(uniforms[UNIFORM_NORMAL_MATRIX], 1, 0, _normalMatrix.m);
    glUniform1f(uniforms[UNIFORM_STABLE_CONFIDENCE], STABLE_CONFIDENCE);
    glUniform1i(uniforms[UNIFORM_IS_NORMALS_MODE], isNormalsMode);
    glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(modelVA);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexModelBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _modelBuffer);
    glDrawElements(GL_POINTS, maxCurrIndex , GL_UNSIGNED_INT, 0);

    
    
    TEST_ALL(100)
}

#pragma mark -  OpenGL ES shader compilation

- (BOOL)loadShaders
{
    GLuint vertShader, fragShader;
    NSString *vertShaderPathname, *fragShaderPathname;
    
    // Create shader program.
    _program = glCreateProgram();
    
    // Create and compile vertex shader.
    vertShaderPathname = [[NSBundle mainBundle] pathForResource:@"Shader" ofType:@"vsh"];
    if (![self compileShader:&vertShader type:GL_VERTEX_SHADER file:vertShaderPathname]) {
        NSLog(@"Failed to compile vertex shader");
        return NO;
    }   
    
    // Create and compile fragment shader.
    fragShaderPathname = [[NSBundle mainBundle] pathForResource:@"Shader" ofType:@"fsh"];
    if (![self compileShader:&fragShader type:GL_FRAGMENT_SHADER file:fragShaderPathname]) {
        NSLog(@"Failed to compile fragment shader");
        return NO;
    }
    
    
    // Attach vertex shader to program.
    glAttachShader(_program, vertShader);
    
    // Attach fragment shader to program.
    glAttachShader(_program, fragShader);
    
  
    // Bind attribute locations.
    // This needs to be done prior to linking.
    glBindAttribLocation(_program, ATTRIB_VERTEX, "position");
    glBindAttribLocation(_program, ATTRIB_NORMAL, "normal");
    glBindAttribLocation(_program, ATTRIB_CONFIDENCE, "confidence");
    glBindAttribLocation(_program, ATTRIB_RADIUS, "radius");
    
    
    // Link program.
    if (![self linkProgram:_program]) {
        NSLog(@"Failed to link program: %d", _program);
        
        if (vertShader) {
            glDeleteShader(vertShader);
            vertShader = 0;
        }
        if (fragShader) {
            glDeleteShader(fragShader);
            fragShader = 0;
        }
        if (_program) {
            glDeleteProgram(_program);
            _program = 0;
        }
        
        return NO;
    }
    
    // Get uniform locations.
    uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX] = glGetUniformLocation(_program, "modelViewProjectionMatrix");
    uniforms[UNIFORM_NORMAL_MATRIX] = glGetUniformLocation(_program, "normalMatrix");
    uniforms[UNIFORM_IS_NORMALS_MODE] = glGetUniformLocation(_program, "_isNormalsMode");
    uniforms[UNIFORM_STABLE_CONFIDENCE] = glGetUniformLocation(_program, "stableConfidence");
    
    
    
    // Release vertex and fragment shaders.
    if (vertShader) {
        glDetachShader(_program, vertShader);
        glDeleteShader(vertShader);
    }
    if (fragShader) {
        glDetachShader(_program, fragShader);
        glDeleteShader(fragShader);
    }
    return YES;
}

- (BOOL)loadRemovalShaders
{
    GLuint removalVertShader, removalFragShader;
    NSString *removalVertShaderPathname, *removalFragShaderPathname;
    
    // Create shader program.
    _pointRemovalProg = glCreateProgram();
    
    // Create and compile vertex shader.
    removalVertShaderPathname = [[NSBundle mainBundle] pathForResource:@"PointRemoval" ofType:@"vsh"];
    if (![self compileShader:&removalVertShader type:GL_VERTEX_SHADER file:removalVertShaderPathname]) {
        NSLog(@"Failed to compile point removal vertex shader");
        return NO;
    }
    
    // Create and compile fragment shader.
    removalFragShaderPathname = [[NSBundle mainBundle] pathForResource:@"PointRemoval" ofType:@"fsh"];
    if (![self compileShader:&removalFragShader type:GL_FRAGMENT_SHADER file:removalFragShaderPathname]) {
        NSLog(@"Failed to compile point removal fragment shader");
        return NO;
    }
    
    
    // Attach vertex shader to program.
    glAttachShader(_pointRemovalProg, removalVertShader);
    
    // Attach fragment shader to program.
    glAttachShader(_pointRemovalProg, removalFragShader);
    
    
    // Bind attribute locations.
    // This needs to be done prior to linking.
    glBindAttribLocation(_pointRemovalProg, ATTRIB_VERTEX, "position");
    glBindAttribLocation(_pointRemovalProg, ATTRIB_NORMAL, "normal");
    glBindAttribLocation(_pointRemovalProg, ATTRIB_RADIUS, "radius");
    
    
    // Link program.
    if (![self linkProgram:_pointRemovalProg]) {
        NSLog(@"Failed to link program: %d", _pointRemovalProg);
        
        if (removalVertShader) {
            glDeleteShader(removalVertShader);
            removalVertShader = 0;
        }
        if (removalFragShader) {
            glDeleteShader(removalFragShader);
            removalFragShader = 0;
        }
        if (_pointRemovalProg) {
            glDeleteProgram(_pointRemovalProg);
            _pointRemovalProg = 0;
        }
        
        return NO;
    }
    
    // Get uniform locations.
    uniforms[UNIFORM_INDEX_TEXTURE] = glGetUniformLocation(_pointRemovalProg, "idTexture");
    uniforms[UNIFORM_POSITION_TEXTURE] = glGetUniformLocation(_pointRemovalProg, "positionTexture");
    uniforms[UNIFORM_NORMAL_TEXTURE] = glGetUniformLocation(_pointRemovalProg, "normalTexture");
    
    uniforms[UNIFORM_X_FACTOR] = glGetUniformLocation(_pointRemovalProg, "xfactor");
    uniforms[UNIFORM_Y_FACTOR] = glGetUniformLocation(_pointRemovalProg, "yfactor");
    uniforms[UNIFORM_SUPER_SAMPLE_FACTOR] = glGetUniformLocation(_pointRemovalProg, "superSampleFactor");
    uniforms[UNIFORM_MAX_DEGREE] = glGetUniformLocation(_pointRemovalProg, "maxDegree");
    
    // uniforms[UNIFORM_MODELVIEW_MATRIX] = glGetUniformLocation(_fusionProg, "modelViewMatrix");
    uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX] = glGetUniformLocation(_pointRemovalProg, "modelViewProjectionMatrix");
    uniforms[UNIFORM_NORMAL_MATRIX] = glGetUniformLocation(_pointRemovalProg, "normalMatrix");
    
    // Release vertex and fragment shaders.
    if (removalVertShader) {
        glDetachShader(_pointRemovalProg, removalVertShader);
        glDeleteShader(removalVertShader);
    }
    if (removalFragShader) {
        glDetachShader(_pointRemovalProg, removalFragShader);
        glDeleteShader(removalFragShader);
    }
    return YES;
}


- (BOOL)loadFusionShaders
{
    GLuint fusionVertShader, fusionFragShader;
    NSString *fusionVertShaderPathname, *fusionFragShaderPathname;
    
    // Create shader program.
    _fusionProg = glCreateProgram();
    
    // Create and compile vertex shader.
    fusionVertShaderPathname = [[NSBundle mainBundle] pathForResource:@"Fusion" ofType:@"vsh"];
    if (![self compileShader:&fusionVertShader type:GL_VERTEX_SHADER file:fusionVertShaderPathname]) {
        NSLog(@"Failed to compile fusion vertex shader");
        return NO;
    }
    
    // Create and compile fragment shader.
    fusionFragShaderPathname = [[NSBundle mainBundle] pathForResource:@"Fusion" ofType:@"fsh"];
    if (![self compileShader:&fusionFragShader type:GL_FRAGMENT_SHADER file:fusionFragShaderPathname]) {
        NSLog(@"Failed to compile fusion fragment shader");
        return NO;
    }
    
    
    // Attach vertex shader to program.
    glAttachShader(_fusionProg, fusionVertShader);
    
    // Attach fragment shader to program.
    glAttachShader(_fusionProg, fusionFragShader);
    
    
    // Bind attribute locations.
    // This needs to be done prior to linking.
    glBindAttribLocation(_fusionProg, ATTRIB_VERTEX, "position");
    glBindAttribLocation(_fusionProg, ATTRIB_NORMAL, "normal");
    
    
    // Link program.
    if (![self linkProgram:_fusionProg]) {
        NSLog(@"Failed to link program: %d", _fusionProg);
        
        if (fusionVertShader) {
            glDeleteShader(fusionVertShader);
            fusionVertShader = 0;
        }
        if (fusionFragShader) {
            glDeleteShader(fusionFragShader);
            fusionFragShader = 0;
        }
        if (_fusionProg) {
            glDeleteProgram(_fusionProg);
            _fusionProg = 0;
        }
        
        return NO;
    }
    
 
   
    
    
    // Get uniform locations.
    uniforms[UNIFORM_INDEX_TEXTURE] = glGetUniformLocation(_fusionProg, "idTexture");
    uniforms[UNIFORM_POSITION_TEXTURE] = glGetUniformLocation(_fusionProg, "positionTexture");
    uniforms[UNIFORM_NORMAL_TEXTURE] = glGetUniformLocation(_fusionProg, "normalTexture");
    uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX] = glGetUniformLocation(_fusionProg, "modelViewProjectionMatrix");
    uniforms[UNIFORM_X_FACTOR] = glGetUniformLocation(_fusionProg, "xfactor");
    uniforms[UNIFORM_Y_FACTOR] = glGetUniformLocation(_fusionProg, "yfactor");
    uniforms[UNIFORM_SUPER_SAMPLE_FACTOR] = glGetUniformLocation(_fusionProg, "superSampleFactor");
    uniforms[UNIFORM_MAX_DEGREE] = glGetUniformLocation(_fusionProg, "maxDegree");
    
   // uniforms[UNIFORM_MODELVIEW_MATRIX] = glGetUniformLocation(_fusionProg, "modelViewMatrix");
    uniforms[UNIFORM_NORMAL_MATRIX] = glGetUniformLocation(_fusionProg, "normalMatrix");
    
    // Release vertex and fragment shaders.
    if (fusionVertShader) {
        glDetachShader(_fusionProg, fusionVertShader);
        glDeleteShader(fusionVertShader);
    }
    if (fusionFragShader) {
        glDetachShader(_fusionProg, fusionFragShader);
        glDeleteShader(fusionFragShader);
    }
    return YES;
}

- (BOOL)loadIndexMapShaders
{
    GLuint textureVertShader, textureFragShader;
    NSString *textureVertShaderPathname, *textureFragShaderPathname;
    
    // Create shader program.
    
    _IndexMapProg = glCreateProgram();
    
    
    // create and compile texture shaders
    textureVertShaderPathname = [[NSBundle mainBundle] pathForResource:@"indexMap" ofType:@"vsh"];
    if (![self compileShader:&textureVertShader type:GL_VERTEX_SHADER file:textureVertShaderPathname]) {
        NSLog(@"Failed to compile texture vertex shader");
        return NO;
    }
    textureFragShaderPathname = [[NSBundle mainBundle] pathForResource:@"indexMap" ofType:@"fsh"];
    if (![self compileShader:&textureFragShader type:GL_FRAGMENT_SHADER file:textureFragShaderPathname]) {
        NSLog(@"Failed to compile texture fragment shader");
        return NO;
    }
    
    glAttachShader(_IndexMapProg, textureVertShader);
    glAttachShader(_IndexMapProg, textureFragShader);
    
    // Bind attribute locations.
    // This needs to be done prior to linking.
    glBindAttribLocation(_IndexMapProg, ATTRIB_VERTEX, "position");
    glBindAttribLocation(_IndexMapProg, ATTRIB_NORMAL, "normal");
    glBindAttribLocation(_IndexMapProg, ATTRIB_CONFIDENCE, "confidence");
    glBindAttribLocation(_IndexMapProg, ATTRIB_RADIUS, "radius");
    
    // Link program.
    if (![self linkProgram:_IndexMapProg])
    {
        NSLog(@"Failed to link program: %d", _IndexMapProg);
        if (textureVertShader)
        {
            glDeleteShader(textureVertShader);
            textureVertShader = 0;
        }
        if (textureFragShader)
        {
            glDeleteShader(textureFragShader);
            textureFragShader = 0;
        }
        if (_IndexMapProg)
        {
            glDeleteProgram(_IndexMapProg);
            _IndexMapProg = 0;
        }
        return NO;
    }
    
    // Get uniform locations.
    uniforms[UNIFORM_MODELVIEWPROJECTION_MATRIX] = glGetUniformLocation(_IndexMapProg, "modelViewProjectionMatrix");
    uniforms[UNIFORM_MODELVIEW_MATRIX] = glGetUniformLocation(_IndexMapProg, "modelViewMatrix");
    uniforms[UNIFORM_NORMAL_MATRIX] = glGetUniformLocation(_IndexMapProg, "normalMatrix");
    
    // Release vertex and fragment shaders.
    if (textureVertShader)
    {
        glDetachShader(_IndexMapProg, textureVertShader);
        glDeleteShader(textureVertShader);
    }
    if (textureFragShader)
    {
        glDetachShader(_IndexMapProg, textureFragShader);
        glDeleteShader(textureFragShader);
    }
    return YES;
}

- (BOOL)compileShader:(GLuint *)shader type:(GLenum)type file:(NSString *)file
{
    GLint status;
    const GLchar *source;
    
    source = (GLchar *)[[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:nil] UTF8String];
    if (!source) {
        NSLog(@"Failed to load vertex shader");
        return NO;
    }
    
    *shader = glCreateShader(type);
    glShaderSource(*shader, 1, &source, NULL);
    glCompileShader(*shader);
    
#if defined(DEBUG)
    GLint logLength;
    glGetShaderiv(*shader, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 0) {
        GLchar *log = (GLchar *)malloc(logLength);
        glGetShaderInfoLog(*shader, logLength, &logLength, log);
        NSLog(@"Shader compile log:\n%s", log);
        free(log);
    }
#endif
    
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);
    if (status == 0) {
        glDeleteShader(*shader);
        return NO;
    }
    
    return YES;
}

- (BOOL)linkProgram:(GLuint)prog
{
    GLint status;
    glLinkProgram(prog);
    
#if defined(DEBUG)
    GLint logLength;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 0) {
        GLchar *log = (GLchar *)malloc(logLength);
        glGetProgramInfoLog(prog, logLength, &logLength, log);
        NSLog(@"Program link log:\n%s", log);
        free(log);
    }
#endif
    
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == 0) {
        return NO;
    }
    
    return YES;
}

- (BOOL)validateProgram:(GLuint)prog
{
    GLint logLength, status;
    
    glValidateProgram(prog);
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 0) {
        GLchar *log = (GLchar *)malloc(logLength);
        glGetProgramInfoLog(prog, logLength, &logLength, log);
        NSLog(@"Program validate log:\n%s", log);
        free(log);
    }
    
    glGetProgramiv(prog, GL_VALIDATE_STATUS, &status);
    if (status == 0) {
        return NO;
    }
    
    return YES;
}


@end
