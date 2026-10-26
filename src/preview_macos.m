/* preview_macos.m — a read-only AppKit window around the shared HTML renderer. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "preview.h"

@interface GlanceDocument : NSObject <WKNavigationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) WKWebView *webView;
@property(nonatomic, strong) NSURL *documentURL;
@property(nonatomic, copy) NSString *themeName;
@property(nonatomic) BOOL loaded;
@property(nonatomic, strong) NSError *loadError;
- (BOOL)showDocument:(NSURL *)url error:(NSError **)error;
@end

@implementation GlanceDocument

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

@interface GlancePreview : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSMutableArray<GlanceDocument *> *documents;
@property(nonatomic, strong) NSMutableArray<NSArray<NSString *> *> *pendingBatches;
@property(nonatomic, strong) NSURL *initialDocumentURL;
@property(nonatomic, copy) NSString *themeName;
@property(nonatomic, strong) NSOpenPanel *openPanel;
@property(nonatomic) BOOL launched;
@property(nonatomic) BOOL receivedOpenRequest;
@property(nonatomic) BOOL terminateAfterLastWindowClosed;
- (void)openDocument:(id)sender;
- (BOOL)openURLs:(NSArray<NSURL *> *)urls;
- (void)presentOpenErrors:(NSArray<NSError *> *)errors;
@end

/* Build standard menus, including an explicit target so Open works without windows. */
static void install_menu(GlancePreview *delegate) {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Glance"];
    [app addItemWithTitle:@"Quit Glance" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;

    NSMenuItem *fileItem = [NSMenuItem new];
    [bar addItem:fileItem];
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *open = [file addItemWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
    open.target = delegate;
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

/* Retain each live document and defer Finder batches until AppKit finishes launching. */
- (instancetype)init {
    self = [super init];
    if (self) {
        _documents = [NSMutableArray new];
        _pendingBatches = [NSMutableArray new];
    }
    return self;
}

/* Open every readable file independently and focus an already-open canonical path. */
- (BOOL)openURLs:(NSArray<NSURL *> *)urls {
    self.receivedOpenRequest = YES;
    if (self.openPanel) [self.openPanel cancel:nil];
    NSMutableArray<NSError *> *errors = [NSMutableArray new];
    for (NSURL *url in urls) {
        NSURL *canonical = url.URLByStandardizingPath.URLByResolvingSymlinksInPath;
        GlanceDocument *existing = nil;
        for (GlanceDocument *document in self.documents) {
            if ([document.documentURL isEqual:canonical]) { existing = document; break; }
        }
        if (existing) {
            [existing.window deminiaturize:nil];
            [existing.window makeKeyAndOrderFront:nil];
            continue;
        }
        GlanceDocument *document = [GlanceDocument new];
        document.themeName = self.themeName;
        NSError *error = nil;
        if ([document showDocument:canonical error:&error]) {
            document.window.delegate = self;
            [self.documents addObject:document];
        } else {
            [errors addObject:[NSError errorWithDomain:NSCocoaErrorDomain code:error.code
                userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"%@: %@",
                    url.lastPathComponent, error.localizedDescription ?: @"The file could not be opened."]}]];
        }
    }
    if (urls.count) [NSApp activateIgnoringOtherApps:YES];
    if (errors.count) [self presentOpenErrors:errors];
    return errors.count == 0;
}

/* Report a batch failure after opening its valid files, without closing other previews. */
- (void)presentOpenErrors:(NSArray<NSError *> *)errors {
    NSMutableArray<NSString *> *messages = [NSMutableArray new];
    for (NSError *error in errors) [messages addObject:error.localizedDescription];
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Unable to preview Markdown";
    alert.informativeText = [messages componentsJoinedByString:@"\n\n"];
    NSWindow *parent = NSApp.keyWindow;
    if (parent && !parent.attachedSheet) [alert beginSheetModalForWindow:parent completionHandler:nil];
    else [alert runModal];
}

/* Offer the native multi-file picker, reusing it when Open is invoked twice. */
- (void)openDocument:(id)sender {
    (void)sender;
    if (self.openPanel) { [self.openPanel makeKeyAndOrderFront:nil]; return; }
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = YES;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"md"],
                                 [UTType typeWithFilenameExtension:@"markdown"]];
    self.openPanel = panel;
    [panel beginWithCompletionHandler:^(NSModalResponse response) {
        self.openPanel = nil;
        if (response == NSModalResponseOK) [self openURLs:panel.URLs];
    }];
}

/* Offer a picker only after launch-time Finder events have had a chance to arrive. */
- (void)openInitialDocumentIfNeeded {
    if (!self.receivedOpenRequest && !self.documents.count) [self openDocument:nil];
}

/* Initialize menus, drain cold-launch requests and defer the documentless fallback. */
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    install_menu(self);
    self.launched = YES;
    if (self.initialDocumentURL) {
        [self openURLs:@[self.initialDocumentURL]];
        self.initialDocumentURL = nil;
    }
    NSArray<NSArray<NSString *> *> *batches = self.pendingBatches.copy;
    [self.pendingBatches removeAllObjects];
    for (NSArray<NSString *> *batch in batches) [self application:NSApp openFiles:batch];
    [self performSelector:@selector(openInitialDocumentIfNeeded) withObject:nil afterDelay:0];
    [NSApp activateIgnoringOtherApps:YES];
}

/* Queue cold Finder events; acknowledge each batch after all its files were attempted. */
- (void)application:(NSApplication *)sender openFiles:(NSArray<NSString *> *)filenames {
    self.receivedOpenRequest = YES;
    if (!self.launched) { [self.pendingBatches addObject:filenames.copy]; return; }
    NSMutableArray<NSURL *> *urls = [NSMutableArray new];
    for (NSString *path in filenames) [urls addObject:[NSURL fileURLWithPath:path]];
    BOOL success = [self openURLs:urls];
    [sender replyToOpenOrPrint:success ? NSApplicationDelegateReplySuccess : NSApplicationDelegateReplyFailure];
}

/* Let the deferred launch fallback supply the picker instead of an untitled document. */
- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *)sender {
    (void)sender;
    return NO;
}

/* Reopening from the Dock offers Open when no document windows remain. */
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
    (void)sender;
    if (!visible && self.launched) [self openDocument:nil];
    return YES;
}

/* Keep Finder launches available for Open; retire independent CLI instances on last close. */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return self.terminateAfterLastWindowClosed;
}

/* Release closed document controllers so reopening reads a fresh snapshot from disk. */
- (void)windowWillClose:(NSNotification *)notification {
    for (GlanceDocument *document in self.documents.copy) {
        if (document.window == notification.object) [self.documents removeObject:document];
    }
}

@end

/* Accept documentless Finder launches and preserve the CLI file/theme invocation. */
static BOOL configure_arguments(GlancePreview *delegate, int argc, char **argv) {
    if (argc == 1 || (argc == 2 && !strncmp(argv[1], "-psn_", 5))) return YES;
    if (argc != 2 && !(argc == 4 && !strcmp(argv[2], "--theme"))) return NO;
    delegate.terminateAfterLastWindowClosed = YES;
    delegate.initialDocumentURL = [NSURL fileURLWithFileSystemRepresentation:argv[1]
        isDirectory:NO relativeToURL:nil];
    if (argc == 4) delegate.themeName = [NSString stringWithUTF8String:argv[3]];
    return YES;
}

#ifndef GLANCE_PREVIEW_TEST
/* Enter the native app loop for either Finder events or `glance --ui` arguments. */
int main(int argc, char **argv) {
    @autoreleasepool {
        GlancePreview *delegate = [GlancePreview new];
        if (!configure_arguments(delegate, argc, argv)) {
            fprintf(stderr, "usage: Glance [FILE [--theme NAME]]\n");
            return 2;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSApp.delegate = delegate;
        [NSApp run];
    }
    return 0;
}
#endif
