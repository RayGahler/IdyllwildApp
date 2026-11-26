#import <AppKit/AppKit.h>

#if __has_include(<AppKit/NSGlassEffectView.h>)
#import <AppKit/NSGlassEffectView.h>
#define IDYLLWILD_HAS_LIQUID_GLASS 1
#endif

#include <QGuiApplication>
#include <QWidget>

void applyNativeLiquidGlass(QWidget *widget)
{
    // Only the Cocoa platform returns an NSView from QWidget::winId().
    if (!widget || !widget->isVisible()
        || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return;

    const WId nativeId = widget->winId();
    if (!nativeId)
        return;
    NSView *qtView = (__bridge NSView *)reinterpret_cast<void *>(nativeId);
    NSWindow *window = qtView.window;
    if (!window || !window.isVisible)
        return;

    NSView *content = window.contentView;
    if (!content || [content isKindOfClass:NSVisualEffectView.class])
        return;
#if defined(IDYLLWILD_HAS_LIQUID_GLASS)
    if (@available(macOS 26.0, *)) {
        if ([content isKindOfClass:NSGlassEffectView.class])
            return;
    }
#endif

    NSView *glass = nil;
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
#if defined(IDYLLWILD_HAS_LIQUID_GLASS)
    if (@available(macOS 26.0, *)) {
        NSGlassEffectView *liquidGlass = [[NSGlassEffectView alloc] initWithFrame:content.bounds];
        liquidGlass.style = NSGlassEffectViewStyleRegular;
        liquidGlass.cornerRadius = 0;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
        if (@available(macOS 27.0, *))
            liquidGlass.effectIsInteractive = YES;
#endif
        liquidGlass.contentView = content;
        glass = liquidGlass;
    } else {
#endif
        NSVisualEffectView *vibrancy = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
        vibrancy.material = NSVisualEffectMaterialSidebar;
        vibrancy.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        vibrancy.state = NSVisualEffectStateFollowsWindowActiveState;
        vibrancy.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [vibrancy addSubview:content];
        glass = vibrancy;
#if defined(IDYLLWILD_HAS_LIQUID_GLASS)
    }
#endif

    glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = glass;
}
