/*
 Copyright (c) 2015 Di Wu diwup@foxmail.com
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#import "DWUWebView.h"
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

@interface NSObject ()
- (CGRect)bounds;
- (id)scrollView;
- (BOOL)webView:(id)webView shouldStartLoadWithRequest:(NSURLRequest *)request navigationType:(UIWebViewNavigationType)navigationType;
- (void)webViewDidStartLoad:(id)webView;
- (void)webViewDidFinishLoad:(id)webView;
- (void)webView:(id)webView didFailLoadWithError:(id)error;
- (void)addSubview:(id)subview;
@end

@interface WKWebViewConfiguration (iOS)
- (BOOL)allowsInlineMediaPlayback;
- (BOOL)mediaPlaybackRequiresUserAction;
- (BOOL)mediaPlaybackAllowsAirPlay;
- (BOOL)allowsPictureInPictureMediaPlayback;
@end

static void * DWUWebViewKVOContext = &DWUWebViewKVOContext;

@interface DWUWebView () <WKNavigationDelegate>

@property (nonnull, nonatomic, strong) WKWebView *wkWebView;

@end

static id (*UIViewInitWithFrame)(id self, SEL _cmd, CGRect frame);
static id (*UIViewInitWithCoder)(id self, SEL _cmd, id coder);
static id (*UIViewSetFrame)(id self, SEL _cmd, CGRect frame);
static id (*UIViewSetBounds)(id self, SEL _cmd, CGRect frame);
static id (*UIViewSetCenter)(id self, SEL _cmd, CGPoint center);

@implementation DWUWebView

+ (void)load {
    Class thisClass = objc_getClass("DWUWebView");
    Class uiViewClass = objc_getClass("UIView");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    //class_setSuperclass(thisClass, uiViewClass);
#pragma clang diagnostic pop
    Class UIWebView = objc_allocateClassPair(uiViewClass, "UIWebView", 0);
    class_setIvarLayout(UIWebView, class_getIvarLayout(thisClass));
    class_setWeakIvarLayout(UIWebView, class_getWeakIvarLayout(thisClass));
    unsigned int numMethods;
    Method *methods = class_copyMethodList(thisClass, &numMethods);
    for (unsigned int i = 0; i < numMethods; i++) {
        Method method = methods[i];
        class_addMethod(UIWebView, method_getName(method), method_getImplementation(method), method_getTypeEncoding(method));
    }
    free(methods);
    objc_registerClassPair(UIWebView);
    UIViewInitWithFrame = class_getMethodImplementation(uiViewClass, @selector(initWithFrame:));
    UIViewInitWithCoder = class_getMethodImplementation(uiViewClass, @selector(initWithCoder:));
    UIViewSetFrame = class_getMethodImplementation(uiViewClass, @selector(setFrame:));
    UIViewSetBounds = class_getMethodImplementation(uiViewClass, @selector(setBounds:));
    UIViewSetCenter = class_getMethodImplementation(uiViewClass, @selector(setCenter:));
}

- (void)setFrame:(CGRect)frame {
    UIViewSetFrame(self, _cmd, frame);
    self.wkWebView.frame = [self bounds];
}

- (void)setCenter:(CGPoint)center {
    UIViewSetCenter(self, _cmd, center);
    self.wkWebView.frame = [self bounds];
}

- (void)setBounds:(CGRect)bounds {
    UIViewSetBounds(self, _cmd, bounds);
    self.wkWebView.frame = [self bounds];
}

+ (UIWebViewNavigationType)_enumHelperForNavigationType:(WKNavigationType)wkNavigationType {
    switch (wkNavigationType) {
        case WKNavigationTypeLinkActivated:
            return UIWebViewNavigationTypeLinkClicked;
            break;
        case WKNavigationTypeFormSubmitted:
            return UIWebViewNavigationTypeFormSubmitted;
            break;
        case WKNavigationTypeBackForward:
            return UIWebViewNavigationTypeBackForward;
            break;
        case WKNavigationTypeReload:
            return UIWebViewNavigationTypeReload;
            break;
        case WKNavigationTypeFormResubmitted:
            return UIWebViewNavigationTypeFormResubmitted;
            break;
        case WKNavigationTypeOther:
        default:
            return UIWebViewNavigationTypeOther;
            break;
    }
}

- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    if ([self.delegate respondsToSelector:@selector(webView:shouldStartLoadWithRequest:navigationType:)]) {
        BOOL result = [self.delegate webView:(id)self shouldStartLoadWithRequest:[NSURLRequest requestWithURL:webView.URL] navigationType:[DWUWebView _enumHelperForNavigationType:navigationAction.navigationType]];
        if (result) {
            decisionHandler(WKNavigationActionPolicyAllow);
        } else {
            decisionHandler(WKNavigationActionPolicyCancel);
        }
    } else {
        decisionHandler(WKNavigationActionPolicyAllow);
    }
}

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation {
    if ([self.delegate respondsToSelector:@selector(webViewDidStartLoad:)]) {
        [self.delegate webViewDidStartLoad:(id)self];
    }
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    if ([self.delegate respondsToSelector:@selector(webViewDidFinishLoad:)]) {
        [self.delegate webViewDidFinishLoad:(id)self];
    }
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    if ([self.delegate respondsToSelector:@selector(webView:didFailLoadWithError:)]) {
        [self.delegate webView:(id)self didFailLoadWithError:error];
    }
}

- (void)_createWkWebView {
    _wkWebView = [[WKWebView alloc] initWithFrame:self.bounds];
    _wkWebView.navigationDelegate = self;
    [self addSubview:_wkWebView];
    [_wkWebView addObserver:self forKeyPath:@"estimatedProgress" options:0 context:DWUWebViewKVOContext];
}

- (instancetype)initWithCoder:(NSCoder *)aDecoder {
    if ((self = UIViewInitWithCoder(self, _cmd, aDecoder))) {
        [self _createWkWebView];
    }
    return self;
}

- (nonnull instancetype)initWithFrame:(CGRect)frame {
    if ((self = UIViewInitWithFrame(self, _cmd, frame))) {
        [self _createWkWebView];
    }
    return self;
}

- (void)dealloc {
    [self.wkWebView removeObserver:self forKeyPath:@"estimatedProgress" context:DWUWebViewKVOContext];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSString *,id> *)change context:(void *)context {
    if (context == DWUWebViewKVOContext) {
        if ([self.progressDelegate respondsToSelector:@selector(webViewUpdateProgress:)]) {
            [self.progressDelegate webViewUpdateProgress:self.wkWebView.estimatedProgress];
        }
    }
}

- (id)scrollView {
    return self.wkWebView.scrollView;
}

- (void)loadRequest:(nonnull NSURLRequest *)request {
    //Ignore the returned WKNavigation object
    [self.wkWebView loadRequest:request];
}

- (void)loadHTMLString:(nonnull NSString *)string baseURL:(nullable NSURL *)baseURL {
    //Ignore the returned WKNavigation object
    NSBundle *mainBundle = [NSBundle mainBundle];
    if ([baseURL isEqual:mainBundle.bundleURL]) {
        baseURL = mainBundle.resourceURL;
    }
    [self.wkWebView loadHTMLString:string baseURL:baseURL];
}

- (void)loadData:(nonnull NSData *)data MIMEType:(nonnull NSString *)MIMEType textEncodingName:(nonnull NSString *)textEncodingName baseURL:(nonnull NSURL *)baseURL {
    //Ignore the returned WKNavigation object
    [self.wkWebView loadData:data MIMEType:MIMEType characterEncodingName:textEncodingName baseURL:baseURL];
}

- (NSURLRequest *)request {
    return [NSURLRequest requestWithURL:self.wkWebView.URL];
}

- (void)reload {
    //Ignore the returned WKNavigation object
    [self.wkWebView reload];
}

- (void)stopLoading {
    [self.wkWebView stopLoading];
}

- (void)goBack {
    //Ignore the returned WKNavigation object
    [self.wkWebView goBack];
}

- (void)goForward {
    //Ignore the returned WKNavigation object
    [self.wkWebView goForward];
}

- (BOOL)canGoBack {
    return self.wkWebView.canGoBack;
}

- (BOOL)canGoForward {
    return self.wkWebView.canGoForward;
}

- (BOOL)isLoading {
    return self.wkWebView.isLoading;
}

- (nullable NSString *)stringByEvaluatingJavaScriptFromString:(nonnull NSString *)script {
    __block NSString *resultString = @"Garbage Value.";
    [self.wkWebView evaluateJavaScript:script completionHandler:^(id _Nullable result, NSError * _Nullable error) {
            if (error == nil) {
                if (result != nil) {
                    resultString = [NSString stringWithFormat:@"%@", result];
                }
            }
    }];
    return resultString;
}

- (BOOL)allowsLinkPreview {
    return self.wkWebView.allowsLinkPreview;
}

- (void)setAllowsLinkPreview:(BOOL)allowsLinkPreview {
    self.wkWebView.allowsLinkPreview = allowsLinkPreview;
}

- (BOOL)scalesPageToFit {
    //This API is not found in WKWebView
    return NO;
}

- (void)setScalesPageToFit:(BOOL)scalesPageToFit {
    //This API is not found in WKWebView
}

- (NSUInteger)dataDetectorTypes {
    //This API is not found in WKWebView
    return 0;
}

- (void)setDataDetectorTypes:(NSUInteger)dataDetectorTypes {
    //This API is not found in WKWebView
}

- (BOOL)allowsInlineMediaPlayback {
    return self.wkWebView.configuration.allowsInlineMediaPlayback;
}

- (void)setAllowsInlineMediaPlayback:(BOOL)allowsInlineMediaPlayback {
    //This property is not settable after web view initialization
}

- (BOOL)mediaPlaybackRequiresUserAction {
    return self.wkWebView.configuration.mediaPlaybackRequiresUserAction;
}

- (void)setMediaPlaybackRequiresUserAction:(BOOL)mediaPlaybackRequiresUserAction {
    //This property is not settable after web view initialization
}

- (BOOL)mediaPlaybackAllowsAirPlay {
    return self.wkWebView.configuration.mediaPlaybackAllowsAirPlay;
}

- (void)setMediaPlaybackAllowsAirPlay:(BOOL)mediaPlaybackAllowsAirPlay {
    //This property is not settable after web view initialization
}

- (BOOL)suppressesIncrementalRendering {
    return self.wkWebView.configuration.suppressesIncrementalRendering;
}

- (void)setSuppressesIncrementalRendering:(BOOL)suppressesIncrementalRendering {
    //This property is not settable after web view initialization
}

- (BOOL)keyboardDisplayRequiresUserAction {
    //This API is not found in WKWebView
    return NO;
}

- (void)setKeyboardDisplayRequiresUserAction:(BOOL)keyboardDisplayRequiresUserAction {
    //This API is not found in WKWebView
}

- (UIWebPaginationMode)paginationMode {
    //This API is not found in WKWebView
    return UIWebPaginationModeUnpaginated;
}

- (void)setPaginationMode:(UIWebPaginationMode)paginationMode {
    //This API is not found in WKWebView
}

- (UIWebPaginationBreakingMode)paginationBreakingMode {
    //This API is not found in WKWebView
    return UIWebPaginationBreakingModePage;
}

- (CGFloat)pageLength {
    //This API is not found in WKWebView
    return 0;
}

- (void)setPageLength:(CGFloat)pageLength {
    //This API is not found in WKWebView
}

- (CGFloat)gapBetweenPages {
    //This API is not found in WKWebView
    return 0;
}

- (void)setGapBetweenPages:(CGFloat)gapBetweenPages {
    //This API is not found in WKWebView
}

- (NSUInteger)pageCount {
    //This API is not found in WKWebView
    return 0;
}

- (BOOL)allowsPictureInPictureMediaPlayback {
    return self.wkWebView.configuration.allowsPictureInPictureMediaPlayback;
}

- (void)setAllowsPictureInPictureMediaPlayback:(BOOL)allowsPictureInPictureMediaPlayback {
    //This property is not settable after web view initialization
}

@end
