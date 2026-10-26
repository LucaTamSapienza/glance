/* preview_ui_test.m — exercise rendering in the actual native WebKit window. */
#define GLANCE_PREVIEW_TEST
#import "../src/preview_macos.m"
#include <assert.h>

/* Pump AppKit events until an asynchronous WebKit operation completes. */
static BOOL wait_until(BOOL (^done)(void)) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while (!done() && deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    return done();
}

/* Evaluate inspection code; page-provided JavaScript remains disabled. */
static id evaluate(WKWebView *webView, NSString *script) {
    __block BOOL done = NO;
    __block id value = nil;
    [webView evaluateJavaScript:script completionHandler:^(id result, NSError *error) {
        if (error) fprintf(stderr, "%s\n", error.localizedDescription.UTF8String);
        assert(!error);
        value = result;
        done = YES;
    }];
    assert(wait_until(^BOOL { return done; }));
    return value;
}

@interface PreviewTestApplication : NSApplication
@property(nonatomic, strong) NSMutableArray<NSNumber *> *openReplies;
@end

@implementation PreviewTestApplication

/* Capture the actual delegate replies without requiring an external Apple event sender. */
- (void)replyToOpenOrPrint:(NSApplicationDelegateReply)reply {
    if (!self.openReplies) self.openReplies = [NSMutableArray new];
    [self.openReplies addObject:@(reply)];
}

@end

@interface PreviewTestDelegate : GlancePreview
@property(nonatomic) NSUInteger pickerRequests;
@property(nonatomic, strong) NSArray<NSError *> *reportedErrors;
@end

@implementation PreviewTestDelegate

/* Observe documentless launch without leaving an interactive picker blocking the suite. */
- (void)openDocument:(id)sender {
    (void)sender;
    self.pickerRequests++;
}

/* Inspect failure isolation without requiring a person to dismiss an alert. */
- (void)presentOpenErrors:(NSArray<NSError *> *)errors {
    self.reportedErrors = errors;
}

@end

/* Close every document through the real window delegate cleanup path. */
static void close_documents(GlancePreview *delegate) {
    for (GlanceDocument *document in delegate.documents.copy) [document.window close];
    assert(delegate.documents.count == 0);
}

/* Check the shipped metadata declares an imported Markdown Viewer without taking defaults. */
static void test_metadata(void) {
    NSDictionary *source = [NSDictionary dictionaryWithContentsOfFile:@"macos/Info.plist"];
    NSDictionary *built = [NSDictionary dictionaryWithContentsOfFile:@"build/Glance.app/Contents/Info.plist"];
    assert(source && [source isEqual:built]);
    NSArray *types = built[@"CFBundleDocumentTypes"];
    assert(types.count == 1);
    assert([types[0][@"CFBundleTypeRole"] isEqual:@"Viewer"]);
    assert([types[0][@"LSHandlerRank"] isEqual:@"Alternate"]);
    assert([types[0][@"LSItemContentTypes"] containsObject:@"net.daringfireball.markdown"]);
    NSArray *imports = built[@"UTImportedTypeDeclarations"];
    assert(imports.count == 1);
    assert([imports[0][@"UTTypeIdentifier"] isEqual:@"net.daringfireball.markdown"]);
    assert([imports[0][@"UTTypeConformsTo"] containsObject:@"public.plain-text"]);
    NSArray *extensions = imports[0][@"UTTypeTagSpecification"][@"public.filename-extension"];
    assert([extensions containsObject:@"md"] && [extensions containsObject:@"markdown"]);
    assert(!built[@"UTExportedTypeDeclarations"]);
}

/* Exercise cold/warm Finder batches, canonical reuse, cleanup, errors and CLI launch arguments. */
static void test_open_events(NSString *dir, NSString *path) {
    PreviewTestApplication *app = (PreviewTestApplication *)NSApp;
    PreviewTestDelegate *delegate = [PreviewTestDelegate new];
    NSString *second = [dir stringByAppendingPathComponent:@"Second 日本語 document.markdown"];
    assert([@"# Second window\n" writeToFile:second atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
    NSData *before = [NSData dataWithContentsOfFile:path];
    NSData *secondBefore = [NSData dataWithContentsOfFile:second];
    [delegate application:app openFiles:@[path, second, path]];
    [delegate application:app openFiles:@[second]];
    assert(delegate.documents.count == 0 && delegate.pendingBatches.count == 2);
    assert(app.openReplies.count == 0);
    [delegate applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    assert(delegate.documents.count == 2 && delegate.pendingBatches.count == 0);
    assert(app.openReplies.count == 2);
    assert(app.openReplies[0].integerValue == NSApplicationDelegateReplySuccess);
    assert(app.openReplies[1].integerValue == NSApplicationDelegateReplySuccess);
    assert(wait_until(^BOOL {
        for (GlanceDocument *doc in delegate.documents) if (!doc.loaded && !doc.loadError) return NO;
        return YES;
    }));
    for (GlanceDocument *doc in delegate.documents) assert(!doc.loadError);
    GlanceDocument *first = delegate.documents[0];
    GlanceDocument *other = delegate.documents[1];
    assert(first.window != other.window && first.webView != other.webView);
    assert([other.window.title isEqualToString:second.lastPathComponent]);
    assert([evaluate(other.webView, @"document.querySelector('h1').textContent") isEqual:@"Second window"]);
    evaluate(first.webView, @"document.body.dataset.reopenMarker = 'preserved'");
    NSString *alias = [dir stringByAppendingPathComponent:@"Alias.md"];
    assert([NSFileManager.defaultManager createSymbolicLinkAtPath:alias withDestinationPath:path error:NULL]);
    [delegate application:app openFiles:@[alias, path]];
    assert(delegate.documents.count == 2 && delegate.documents[0] == first);
    assert([evaluate(first.webView, @"document.body.dataset.reopenMarker") isEqual:@"preserved"]);
    [delegate openInitialDocumentIfNeeded];
    assert(delegate.pickerRequests == 0);

    NSString *third = [dir stringByAppendingPathComponent:@"Third.md"];
    assert([@"# Third\n" writeToFile:third atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
    NSString *missing = [dir stringByAppendingPathComponent:@"Missing.md"];
    [delegate application:app openFiles:@[missing, third, dir]];
    assert(delegate.documents.count == 3 && delegate.reportedErrors.count == 2);
    assert(app.openReplies.lastObject.integerValue == NSApplicationDelegateReplyFailure);
    assert([delegate.reportedErrors[0].localizedDescription containsString:@"Missing.md"]);
    assert(delegate.documents[0] == first && delegate.documents[1] == other);
    assert([[NSData dataWithContentsOfFile:path] isEqual:before]);
    assert([[NSData dataWithContentsOfFile:second] isEqual:secondBefore]);
    [other.window close];
    assert(delegate.documents.count == 2);
    assert([@"# Fresh snapshot\n" writeToFile:second atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
    [delegate application:app openFiles:@[second]];
    assert(delegate.documents.count == 3 && delegate.documents.lastObject != other);
    GlanceDocument *fresh = delegate.documents.lastObject;
    assert(wait_until(^BOOL { return fresh.loaded || fresh.loadError != nil; }));
    assert(!fresh.loadError);
    assert([evaluate(fresh.webView, @"document.querySelector('h1').textContent") isEqual:@"Fresh snapshot"]);
    close_documents(delegate);
    assert(![delegate applicationShouldTerminateAfterLastWindowClosed:app]);
    [delegate applicationShouldHandleReopen:app hasVisibleWindows:NO];
    assert(delegate.pickerRequests == 1);

    PreviewTestDelegate *empty = [PreviewTestDelegate new];
    char *noArgs[] = {"Glance", NULL};
    assert(configure_arguments(empty, 1, noArgs));
    assert(!empty.initialDocumentURL);
    assert(![empty applicationShouldTerminateAfterLastWindowClosed:app]);
    [empty applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    assert(wait_until(^BOOL { return empty.pickerRequests == 1; }));
    assert(empty.documents.count == 0 && !empty.reportedErrors);
    NSMenuItem *open = [NSApp.mainMenu itemAtIndex:1].submenu.itemArray[0];
    assert(open.action == @selector(openDocument:) && open.target == empty);
    assert([open.keyEquivalent isEqual:@"o"]);
    assert([NSApp sendAction:open.action to:open.target from:open]);
    assert(empty.pickerRequests == 2);
    char *psn[] = {"Glance", "-psn_0_123", NULL};
    assert(configure_arguments(empty, 2, psn) && !empty.initialDocumentURL);
    char *bad[] = {"Glance", "file", "--other", NULL};
    assert(!configure_arguments(empty, 3, bad));

    PreviewTestDelegate *cli = [PreviewTestDelegate new];
    char *args[] = {"Glance", (char *)path.fileSystemRepresentation, "--theme", "github-light", NULL};
    assert(configure_arguments(cli, 4, args));
    assert([cli.themeName isEqual:@"github-light"]);
    assert([cli applicationShouldTerminateAfterLastWindowClosed:app]);
    [cli application:app openFiles:@[path]];
    [cli applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    assert(cli.documents.count == 1 && !cli.initialDocumentURL);
    GlanceDocument *themed = cli.documents[0];
    assert(wait_until(^BOOL { return themed.loaded || themed.loadError != nil; }));
    assert(!themed.loadError);
    assert([evaluate(themed.webView, @"getComputedStyle(document.body).backgroundColor") isEqual:@"rgb(255, 255, 255)"]);
    [cli openInitialDocumentIfNeeded];
    assert(cli.pickerRequests == 0);
    close_documents(cli);
    PreviewTestDelegate *plainCLI = [PreviewTestDelegate new];
    assert(configure_arguments(plainCLI, 2, args));
    assert(plainCLI.initialDocumentURL && !plainCLI.themeName);
    assert([plainCLI applicationShouldTerminateAfterLastWindowClosed:app]);

    PreviewTestDelegate *late = [PreviewTestDelegate new];
    [late applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    [late application:app openFiles:@[path]];
    [late openInitialDocumentIfNeeded];
    assert(late.documents.count == 1 && late.pickerRequests == 0);
    close_documents(late);

    PreviewTestDelegate *failed = [PreviewTestDelegate new];
    NSUInteger repliesBefore = app.openReplies.count;
    [failed application:app openFiles:@[missing]];
    [failed applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    assert(failed.documents.count == 0 && failed.reportedErrors.count == 1);
    assert(app.openReplies.count == repliesBefore + 1);
    assert(app.openReplies.lastObject.integerValue == NSApplicationDelegateReplyFailure);
    [failed openInitialDocumentIfNeeded];
    assert(failed.pickerRequests == 0);
    [failed application:app openFiles:@[path]];
    assert(failed.documents.count == 1);
    close_documents(failed);

    GlancePreview *picker = [GlancePreview new];
    [picker openDocument:nil];
    NSOpenPanel *panel = picker.openPanel;
    assert(panel && panel.canChooseFiles && !panel.canChooseDirectories && panel.allowsMultipleSelection);
    assert([panel.allowedContentTypes containsObject:[UTType typeWithFilenameExtension:@"md"]]);
    assert([panel.allowedContentTypes containsObject:[UTType typeWithFilenameExtension:@"markdown"]]);
    [picker openDocument:nil];
    assert(picker.openPanel == panel);
    [picker application:app openFiles:@[path]];
    [picker applicationDidFinishLaunching:[NSNotification notificationWithName:NSApplicationDidFinishLaunchingNotification object:app]];
    assert(wait_until(^BOOL { return picker.openPanel == nil; }));
    assert(picker.documents.count == 1);
    close_documents(picker);
}

/* Verify semantic rendering, local resources, themes, safety and no file writes. */
int main(void) {
    @autoreleasepool {
        [PreviewTestApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        [NSApp finishLaunching];
        NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        NSFileManager *fm = NSFileManager.defaultManager;
        assert([fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL]);
        NSString *image = @"<svg xmlns='http://www.w3.org/2000/svg' width='32' height='20'><rect width='32' height='20' fill='green'/></svg>";
        assert([image writeToFile:[dir stringByAppendingPathComponent:@"image with space.svg"]
            atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
        NSString *markdown = @"# Caffè & preview\n\n**Bold** and *italic*.\n\n"
            "| A | B |\n|---|---|\n| 1 | 2 |\n\n- [x] Complete\n\n"
            "```c\nint value = 42;\n```\n\n![local](image%20with%20space.svg)\n\n"
            "<script>document.body.dataset.executed = 'yes';</script>\n";
        NSString *path = [dir stringByAppendingPathComponent:@"Caffè & spaces.md"];
        assert([markdown writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
        NSData *before = [NSData dataWithContentsOfFile:path];
        GlanceDocument *preview = [GlanceDocument new];
        NSError *error = nil;
        assert([preview showDocument:[NSURL fileURLWithPath:path] error:&error]);
        assert(wait_until(^BOOL { return preview.loaded || preview.loadError != nil; }));
        assert(!preview.loadError);
        assert([preview.window.title.precomposedStringWithCanonicalMapping isEqualToString:@"Caffè & spaces.md"]);
        assert([[evaluate(preview.webView, @"document.querySelector('h1').textContent") description]
            isEqualToString:@"Caffè & preview"]);
        assert([evaluate(preview.webView, @"document.querySelectorAll('table td').length") intValue] == 2);
        assert([evaluate(preview.webView, @"document.querySelectorAll('pre .hl-k').length") intValue] > 0);
        assert([evaluate(preview.webView, @"document.querySelector('input').disabled") boolValue]);
        assert(![evaluate(preview.webView, @"document.body.isContentEditable") boolValue]);
        assert([evaluate(preview.webView, @"typeof document.body.dataset.executed === 'undefined'") boolValue]);
        id width = evaluate(preview.webView, @"document.querySelector('img').naturalWidth");
        assert([width intValue] == 32);
        assert([evaluate(preview.webView, @"getComputedStyle(document.body).backgroundColor") isEqualToString:@"rgb(255, 255, 255)"]);
        assert([[NSData dataWithContentsOfFile:path] isEqualToData:before]);
        __block BOOL captured = NO;
        [preview.webView takeSnapshotWithConfiguration:nil completionHandler:^(NSImage *image, NSError *snapshotError) {
            assert(image && !snapshotError);
            NSBitmapImageRep *bitmap = [NSBitmapImageRep imageRepWithData:image.TIFFRepresentation];
            NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            assert([png writeToFile:@"build/preview-test.png" atomically:YES]);
            captured = YES;
        }];
        assert(wait_until(^BOOL { return captured; }));
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        assert([preview showDocument:[NSURL fileURLWithPath:path] error:&error]);
        assert(wait_until(^BOOL { return preview.loaded || preview.loadError != nil; }));
        assert(!preview.loadError);
        assert([evaluate(preview.webView, @"getComputedStyle(document.body).backgroundColor") isEqualToString:@"rgb(27, 27, 31)"]);
        preview.themeName = @"github-light";
        assert([preview showDocument:[NSURL fileURLWithPath:path] error:&error]);
        assert(wait_until(^BOOL { return preview.loaded || preview.loadError != nil; }));
        assert(!preview.loadError);
        assert([evaluate(preview.webView, @"getComputedStyle(document.body).backgroundColor") isEqualToString:@"rgb(255, 255, 255)"]);
        [preview.window close];
        assert(![preview showDocument:[NSURL fileURLWithPath:dir] error:&error]);
        assert(![preview showDocument:[NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@"missing.md"]] error:&error]);
        test_metadata();
        test_open_events(dir, path);
        assert([fm removeItemAtPath:dir error:NULL]);
        puts("all native preview tests passed");
    }
    return 0;
}
