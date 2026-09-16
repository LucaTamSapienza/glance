/* preview_macos.m — a read-only AppKit window around the shared HTML renderer. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "preview.h"

@interface GlancePreview : NSObject <NSApplicationDelegate, WKNavigationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) WKWebView *webView;
@property(nonatomic, strong) NSURL *documentURL;
@property(nonatomic, copy) NSString *themeName;
@property(nonatomic) BOOL loaded;
@property(nonatomic, strong) NSError *loadError;
- (BOOL)showDocument:(NSURL *)url error:(NSError **)error;
@end

/* Build the standard app menus needed for copying, selecting and closing. */
static void install_menu(void) {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Glance"];
    [app addItemWithTitle:@"Quit Glance" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;

    NSMenuItem *fileItem = [NSMenuItem new];
    [bar addItem:fileItem];
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    fileItem.submenu = file;

    NSMenuItem *editItem = [NSMenuItem new];
    [bar addItem:editItem];
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = edit;
    NSApp.mainMenu = bar;
}

@implementation GlancePreview

/* Render a UTF-8 Markdown file with its own directory as the resource base. */
- (BOOL)showDocument:(NSURL *)url error:(NSError **)error {
    NSNumber *regular = nil;
    if (![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error]) return NO;
    if (!regular.boolValue) {
        if (error) *error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadUnsupportedSchemeError
            userInfo:@{NSLocalizedDescriptionKey: @"Choose a Markdown file, not a folder."}];
        return NO;
    }
    NSString *markdown = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:error];
    if (!markdown) return NO;
    NSAppearanceName appearance = [NSApp.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    int dark = [appearance isEqualToString:NSAppearanceNameDarkAqua];
    NSData *data = [markdown dataUsingEncoding:NSUTF8StringEncoding];
    char *html = preview_html(data.bytes ? data.bytes : "", data.length,
        url.lastPathComponent.UTF8String, self.themeName.UTF8String, dark, &dark);
    NSString *page = html ? [[NSString alloc] initWithUTF8String:html] : nil;
    free(html);
    if (!page) {
        if (error) *error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError
            userInfo:@{NSLocalizedDescriptionKey: @"Could not render this Markdown file."}];
        return NO;
    }
    NSRange head = [page rangeOfString:@"<head>"];
    page = [page stringByReplacingCharactersInRange:head withString:
        @"<head><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
         "img-src file: data: https: http:; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">"];
    if (!self.window) {
        WKWebViewConfiguration *config = [WKWebViewConfiguration new];
        config.defaultWebpagePreferences.allowsContentJavaScript = NO;
        config.websiteDataStore = WKWebsiteDataStore.nonPersistentDataStore;
        self.webView = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 900, 760) configuration:config];
        self.webView.navigationDelegate = self;
        self.webView.allowsLinkPreview = NO;
        self.window = [[NSWindow alloc] initWithContentRect:self.webView.frame
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        self.window.releasedWhenClosed = NO;
        self.window.contentMinSize = NSMakeSize(360, 260);
        self.window.contentView = self.webView;
        [self.window center];
    }
    self.documentURL = url;
    self.loaded = NO;
    self.loadError = nil;
    self.window.representedURL = url;
    self.window.title = url.lastPathComponent;
    self.window.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    [self.webView loadHTMLString:page baseURL:url.URLByDeletingLastPathComponent];
    [self.window makeKeyAndOrderFront:nil];
    return YES;
}

/* Start the requested document after AppKit has connected to WindowServer. */
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    install_menu();
    NSError *error = nil;
    if (![self showDocument:self.documentURL error:&error]) {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"Unable to preview Markdown";
        alert.informativeText = error.localizedDescription ?: @"The file could not be opened.";
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }
    [NSApp activateIgnoringOtherApps:YES];
}

/* Quit when the preview closes, so no invisible helper remains running. */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

/* Keep the preview on its document; explicit web/mail links open externally. */
- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    NSURL *url = action.request.URL;
    if (action.navigationType == WKNavigationTypeLinkActivated) {
        NSString *scheme = url.scheme.lowercaseString;
        if ([scheme isEqualToString:@"https"] || [scheme isEqualToString:@"http"] ||
            [scheme isEqualToString:@"mailto"]) {
            [NSWorkspace.sharedWorkspace openURL:url];
        }
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    BOOL initial = !self.loaded && ([url.scheme isEqualToString:@"file"] ||
                                   [url.absoluteString isEqualToString:@"about:blank"]);
    decisionHandler(initial ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}

/* Record completion for the native integration check. */
- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    (void)webView; (void)navigation;
    self.loaded = YES;
}

/* Surface WebKit loading failures in the window instead of leaving it blank. */
- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation
    withError:(NSError *)error {
    (void)webView; (void)navigation;
    self.loadError = error;
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Unable to display Markdown";
    alert.informativeText = error.localizedDescription;
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

/* Handle failures after a navigation has committed with the same visible error. */
- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    [self webView:webView didFailProvisionalNavigation:navigation withError:error];
}

@end

#ifndef GLANCE_PREVIEW_TEST
/* Enter the native app loop; this executable is launched by `glance --ui`. */
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2 && !(argc == 4 && !strcmp(argv[2], "--theme"))) {
            fprintf(stderr, "usage: Glance FILE [--theme NAME]\n");
            return 2;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        GlancePreview *delegate = [GlancePreview new];
        delegate.documentURL = [NSURL fileURLWithFileSystemRepresentation:argv[1] isDirectory:NO relativeToURL:nil];
        if (argc == 4) delegate.themeName = [NSString stringWithUTF8String:argv[3]];
        NSApp.delegate = delegate;
        [NSApp run];
    }
    return 0;
}
#endif
