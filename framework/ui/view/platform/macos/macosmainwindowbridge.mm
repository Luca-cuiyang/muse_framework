/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "macosmainwindowbridge.h"

#include <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include <QWindow>
#include <cstdio>

using namespace muse::ui;

static NSWindow* nsWindowForQWindow(QWindow* qWindow)
{
    if (!qWindow) {
        return nullptr;
    }

    NSView* nsView = (__bridge NSView*)reinterpret_cast<void*>(qWindow->winId());
    NSWindow* nsWindow = [nsView window];
    return nsWindow;
}

static NSApplicationPresentationOptions dbstudioFullScreenPresentationOptions(
    id, SEL, NSWindow*, NSApplicationPresentationOptions proposedOptions)
{
    proposedOptions &= ~(NSApplicationPresentationAutoHideMenuBar
                         | NSApplicationPresentationAutoHideDock
                         | NSApplicationPresentationAutoHideToolbar);
    return proposedOptions | NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock;
}

static void installFullScreenPresentationBehavior(NSWindow* window)
{
    id delegate = window.delegate;
    if (!delegate) {
        return;
    }

    static id installedDelegate = nil;
    if (delegate == installedDelegate) {
        return;
    }

    static unsigned long delegateClassIndex = 0;
    char className[64];
    snprintf(className, sizeof(className), "DBStudioFullScreenWindowDelegate%lu", ++delegateClassIndex);

    Class delegateClass = object_getClass(delegate);
    Class fullScreenDelegateClass = objc_allocateClassPair(delegateClass, className, 0);
    if (!fullScreenDelegateClass) {
        return;
    }

    class_addMethod(fullScreenDelegateClass,
                    @selector(window:willUseFullScreenPresentationOptions:),
                    (IMP)dbstudioFullScreenPresentationOptions,
                    "Q@:@@Q");
    objc_registerClassPair(fullScreenDelegateClass);
    object_setClass(delegate, fullScreenDelegateClass);
    installedDelegate = delegate;
}

MacOSMainWindowBridge::MacOSMainWindowBridge(QObject* parent)
    : MainWindowBridge(parent)
{
}

MacOSMainWindowBridge::~MacOSMainWindowBridge()
{
    NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];

    if (m_enterFullScreenObserver) {
        [notificationCenter removeObserver:(__bridge id)m_enterFullScreenObserver];
        [(__bridge id)m_enterFullScreenObserver release];
    }
    if (m_exitFullScreenObserver) {
        [notificationCenter removeObserver:(__bridge id)m_exitFullScreenObserver];
        [(__bridge id)m_exitFullScreenObserver release];
    }

    [[NSApplication sharedApplication] setPresentationOptions:NSApplicationPresentationDefault];
}

void MacOSMainWindowBridge::init()
{
    MainWindowBridge::init();

    uiConfiguration()->applyPlatformStyle(m_window);

    uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
        uiConfiguration()->applyPlatformStyle(m_window);
    });

    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return;
    }

    installFullScreenPresentationBehavior(nsWindow);

    NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];
    id enterObserver = [notificationCenter addObserverForName:NSWindowDidEnterFullScreenNotification
                                                      object:nsWindow
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification*) {
        setFullScreenPresentationOptions(true);
    }];
    id exitObserver = [notificationCenter addObserverForName:NSWindowDidExitFullScreenNotification
                                                     object:nsWindow
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification*) {
        setFullScreenPresentationOptions(false);
    }];
    m_enterFullScreenObserver = [enterObserver retain];
    m_exitFullScreenObserver = [exitObserver retain];
}

bool MacOSMainWindowBridge::fileModified() const
{
    //! NOTE QWindow misses an API for this, so we'll do it ourselves.
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return false;
    }

    return [nsWindow isDocumentEdited];
}

void MacOSMainWindowBridge::toggleFullScreen()
{
    installFullScreenPresentationBehavior(nsWindowForQWindow(m_window));
    MainWindowBridge::toggleFullScreen();
}

void MacOSMainWindowBridge::activate()
{
    MainWindowBridge::activate();

    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return;
    }

    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
    [nsWindow makeKeyAndOrderFront:nil];
    [nsWindow makeFirstResponder:nsWindow.contentView];
}

void MacOSMainWindowBridge::setFileModified(bool modified)
{
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return;
    }

    if ([nsWindow isDocumentEdited] == modified) {
        return;
    }

    [nsWindow setDocumentEdited:modified];
    emit fileModifiedChanged();
}

void MacOSMainWindowBridge::setFullScreenPresentationOptions(bool fullScreen)
{
    NSApplication* application = [NSApplication sharedApplication];
    NSWindow* nsWindow = nsWindowForQWindow(m_window);

    if (!fullScreen) {
        [nsWindow setTitleVisibility:NSWindowTitleVisible];
        [NSMenu setMenuBarVisible:YES];
        [application setPresentationOptions:NSApplicationPresentationDefault];
        return;
    }

    [nsWindow setTitleVisibility:NSWindowTitleHidden];
    [NSMenu setMenuBarVisible:NO];
    NSApplicationPresentationOptions options = [application presentationOptions];
    options &= ~(NSApplicationPresentationAutoHideMenuBar
                 | NSApplicationPresentationAutoHideDock
                 | NSApplicationPresentationAutoHideToolbar);
    options |= NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock;
    [application setPresentationOptions:options];
}
