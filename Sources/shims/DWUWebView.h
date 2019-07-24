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

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, UIWebViewNavigationType) {
    UIWebViewNavigationTypeLinkClicked,
    UIWebViewNavigationTypeFormSubmitted,
    UIWebViewNavigationTypeBackForward,
    UIWebViewNavigationTypeReload,
    UIWebViewNavigationTypeFormResubmitted,
    UIWebViewNavigationTypeOther
};

typedef NS_ENUM(NSInteger, UIWebPaginationMode) {
    UIWebPaginationModeUnpaginated,
    UIWebPaginationModeLeftToRight,
    UIWebPaginationModeTopToBottom,
    UIWebPaginationModeBottomToTop,
    UIWebPaginationModeRightToLeft
};

typedef NS_ENUM(NSInteger, UIWebPaginationBreakingMode) {
    UIWebPaginationBreakingModePage,
    UIWebPaginationBreakingModeColumn
};

@protocol DWUWebViewProgressDelegate <NSObject>

@optional
- (void)webViewUpdateProgress:(double)progress;

@end

@interface DWUWebView : NSObject

- (nonnull instancetype)initWithFrame:(CGRect)frame NS_DESIGNATED_INITIALIZER;

@property (nullable, nonatomic, assign) id delegate;

@property (nullable, nonatomic, assign) id progressDelegate;

@property (nonnull, nonatomic, readonly, strong) id scrollView;

- (void)loadRequest:(nonnull NSURLRequest *)request;

- (void)loadHTMLString:(nonnull NSString *)string baseURL:(nullable NSURL *)baseURL;

- (void)loadData:(nonnull NSData *)data MIMEType:(nonnull NSString *)MIMEType textEncodingName:(nonnull NSString *)textEncodingName baseURL:(nonnull NSURL *)baseURL;

@property (nullable, nonatomic, readonly, strong) NSURLRequest *request;

- (void)reload;
- (void)stopLoading;

- (void)goBack;
- (void)goForward;

@property (nonatomic, readonly, getter=canGoBack) BOOL canGoBack;
@property (nonatomic, readonly, getter=canGoForward) BOOL canGoForward;
@property (nonatomic, readonly, getter=isLoading) BOOL loading;

- (nullable NSString *)stringByEvaluatingJavaScriptFromString:(nonnull NSString *)script;

@property (nonatomic) BOOL allowsLinkPreview;

// Below are APIs from UIWebView not supported by DWUWebView
// Calling them won't crash anything or having any effect over the web page that is being rendered.

@property (nonatomic) BOOL scalesPageToFit;

@property (nonatomic) NSUInteger dataDetectorTypes;

@property (nonatomic) BOOL allowsInlineMediaPlayback;

@property (nonatomic) BOOL mediaPlaybackRequiresUserAction;

@property (nonatomic) BOOL mediaPlaybackAllowsAirPlay;

@property (nonatomic) BOOL suppressesIncrementalRendering;

@property (nonatomic) BOOL keyboardDisplayRequiresUserAction;

@property (nonatomic) UIWebPaginationMode paginationMode;

@property (nonatomic) UIWebPaginationBreakingMode paginationBreakingMode;

@property (nonatomic) CGFloat pageLength;

@property (nonatomic) CGFloat gapBetweenPages;

@property (nonatomic, readonly) NSUInteger pageCount;

@property (nonatomic) BOOL allowsPictureInPictureMediaPlayback;


@end
