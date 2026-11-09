/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Window layer for macOS (ui:CocoaUI): an NSView that shows the panel's pixels and
 * forwards mouse events to panel.h. Objective-C, included from ui_lv2.c only on Apple
 * platforms; the Makefile compiles ui_lv2.c with -x objective-c there.
 * Manual reference counting (no ARC), so it builds with any clang setting. */
#ifndef WINDOW_COCOA_H
#define WINDOW_COCOA_H

#import <Cocoa/Cocoa.h>

struct UI {
    Panel panel;
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
    NSView* view;
    NSTimer* timer;
};

/* Objective-C class names are global to the process, so this one is unique to the plugin. */
@interface DavidianDuo57View : NSView {
@public
    UI* ui;
}
@end

@implementation DavidianDuo57View

- (BOOL)isFlipped { return YES; } /* y down, like the panel */
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { (void)e; return YES; }

- (void)drawRect:(NSRect)dirty
{
    CGContextRef ctx;
    CGColorSpaceRef cs;
    CGDataProviderRef dp;
    CGImageRef img;
    (void)dirty;
    if (!ui)
        return;
    if (ui->panel.dirty)
        panel_draw(&ui->panel);
    ctx = [[NSGraphicsContext currentContext] CGContext];
    cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    dp = CGDataProviderCreateWithData(NULL, ui->panel.px, sizeof(uint32_t) * PANEL_W * PANEL_H, NULL);
    /* 0x00RRGGBB words, little-endian: BGRX bytes. */
    img = CGImageCreate(PANEL_W, PANEL_H, 8, 32, PANEL_W * 4, cs,
                        (CGBitmapInfo)kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst,
                        dp, NULL, false, kCGRenderingIntentDefault);
    if (ctx && img) {
        /* The view is flipped, so flip back for CGContextDrawImage. */
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0, PANEL_H);
        CGContextScaleCTM(ctx, 1, -1);
        CGContextDrawImage(ctx, CGRectMake(0, 0, PANEL_W, PANEL_H), img);
        CGContextRestoreGState(ctx);
    }
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

static NSPoint view_point(NSView* v, NSEvent* e)
{
    return [v convertPoint:[e locationInWindow] fromView:nil];
}

static int shift_held(NSEvent* e)
{
    return ([e modifierFlags] & NSEventModifierFlagShift) != 0;
}

- (void)mouseDown:(NSEvent*)e
{
    NSPoint p = view_point(self, e);
    if (!ui)
        return;
    panel_mouse_down(&ui->panel, (float)p.x, (float)p.y, [e clickCount] == 2);
    ui_redraw(ui);
}

- (void)mouseDragged:(NSEvent*)e
{
    NSPoint p = view_point(self, e);
    if (!ui)
        return;
    panel_mouse_move(&ui->panel, (float)p.x, (float)p.y, shift_held(e));
    ui_redraw(ui);
}

- (void)mouseUp:(NSEvent*)e
{
    (void)e;
    if (!ui)
        return;
    panel_mouse_up(&ui->panel);
    ui_redraw(ui);
}

- (void)scrollWheel:(NSEvent*)e
{
    NSPoint p = view_point(self, e);
    /* Shift turns a vertical wheel into a horizontal one on macOS. */
    CGFloat d = [e scrollingDeltaY] != 0 ? [e scrollingDeltaY] : [e scrollingDeltaX];
    if (!ui || d == 0)
        return;
    if ([e hasPreciseScrollingDeltas])
        d /= 10.0; /* trackpads report pixels, not notches */
    panel_wheel(&ui->panel, (float)p.x, (float)p.y, (float)d, shift_held(e));
    ui_redraw(ui);
}

- (void)tick:(NSTimer*)t
{
    (void)t;
    if (ui)
        ui_redraw(ui);
}

@end

static int window_open(UI* ui, void* parent)
{
    DavidianDuo57View* v = [[DavidianDuo57View alloc] initWithFrame:NSMakeRect(0, 0, PANEL_W, PANEL_H)];
    if (!v)
        return 0;
    v->ui = ui;
    ui->view = v;
    if (parent)
        [(NSView*)parent addSubview:v];
    /* Picks up host updates; common modes keep it running during mouse tracking. */
    ui->timer = [[NSTimer timerWithTimeInterval:1.0 / 30.0 target:v selector:@selector(tick:)
                                       userInfo:nil repeats:YES] retain];
    [[NSRunLoop currentRunLoop] addTimer:ui->timer forMode:NSRunLoopCommonModes];
    [v setNeedsDisplay:YES];
    return 1;
}

static void window_close(UI* ui)
{
    if (ui->timer) {
        [ui->timer invalidate]; /* drops the timer's hold on the view */
        [ui->timer release];
        ui->timer = nil;
    }
    if (ui->view) {
        ((DavidianDuo57View*)ui->view)->ui = NULL;
        [ui->view removeFromSuperview];
        [ui->view release];
        ui->view = nil;
    }
}

static void* window_widget(UI* ui) { return (void*)ui->view; }

static void ui_redraw(UI* ui)
{
    if (ui->panel.dirty && ui->view)
        [ui->view setNeedsDisplay:YES];
}

/* The view's own timer drives redraws, so idle has nothing else to do. */
static int window_idle(UI* ui)
{
    ui_redraw(ui);
    return 0;
}

#endif
