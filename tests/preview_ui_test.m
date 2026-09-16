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

/* Verify semantic rendering, local resources, themes, safety and no file writes. */
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        [NSApp finishLaunching];
        install_menu();
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
        GlancePreview *preview = [GlancePreview new];
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
        assert([preview applicationShouldTerminateAfterLastWindowClosed:NSApp]);
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
        assert([fm removeItemAtPath:dir error:NULL]);
        puts("all native preview tests passed");
    }
    return 0;
}
