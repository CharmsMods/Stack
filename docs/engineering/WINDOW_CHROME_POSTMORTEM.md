# Window Chrome Postmortem

This note records the windowing failure that affected Stack's popout windows,
file dialogs, shutdown acknowledgement, and custom program bar work on Windows.
It exists to prevent future changes from reintroducing the same class of bug.

## Final Direction

Stack's main window must remain a normal decorated Windows top-level window.

The supported custom titlebar direction is:

- keep `GLFW_DECORATED` enabled for the main GLFW window
- keep native minimize, maximize, close, resize, snap, and system activation behavior
- use Windows App SDK `AppWindowTitleBar` to extend client content into the titlebar area
- render Stack's visual program bar and background image in that extended client area
- reserve the native caption button area for Windows
- register only real interactive app controls as non-client passthrough regions
- when WinRT passthrough regions are unavailable, keep the extended titlebar
  active and use Stack's native WndProc click fallback for titlebar controls
- use owned native dialogs and owned popout windows

This preserves normal Windows z-order and activation behavior while allowing the
top band to visually match Stack's app surface.

When wallpaper/background-image surfaces are active, the rendered program bar in
the extended titlebar must stay transparent. The background image is already
drawn underneath the whole app surface; painting `chromeSurface` over the titlebar
creates a separate visible strip, and drawing a hard separator at the bottom of
that transparent titlebar creates a visible seam.

## What Went Wrong

The earlier custom chrome path made the main GLFW window undecorated with
`GLFW_DECORATED=false` and then tried to rebuild titlebar behavior manually with
Win32 non-client handling.

That was the first clearly reproducible broken layer. Once the main window was
undecorated:

- File Explorer/common file dialogs could open behind Stack.
- Some dialog experiments made the app flash, freeze, or become unresponsive.
- Native drag, resize, minimize, maximize, and close behavior disappeared unless
  fully reimplemented.
- The main window behaved too much like a borderless/fullscreen game window on
  the affected PC.
- The close acknowledgement surface could fail to visibly present before teardown.
- Popout z-order issues were initially suspected, but native chrome proved the
  main window was the real source of the compositing/activation problem.

The important diagnostic result was that popouts, file dialogs, and closing all
worked again when the main window returned to native decorated chrome.

## What Not To Do

Do not use `GLFW_DECORATED=false` for Stack's production main window on Windows.

Do not build future titlebar work on top of an undecorated main window unless the
project deliberately replaces the entire GLFW/native-dialog/native-viewport model
with a different, fully owned windowing architecture.

Do not try to fix dialog z-order by repeatedly pulsing `HWND_TOPMOST`, raising
partially created dialog HWNDs from hooks, disabling the owner window, or making
dialogs ownerless. Those paths either did not fix the issue or introduced worse
failures.

Do not draw custom minimize, maximize, and close buttons while native caption
buttons are active. The native buttons should remain the source of truth for
system behavior.

Do not treat a visual titlebar problem as only a paint/styling problem. On
Windows, titlebars, non-client hit testing, ownership, foreground activation,
snap layouts, modal dialogs, and z-order are connected system behaviors.

## What Works

The working approach keeps the main HWND ordinary from Windows' point of view.

Stack now uses native decorated chrome as the baseline. On Windows builds with
the local Microsoft.WindowsAppSDK package available, the normal build enables the
AppWindow titlebar bridge. The bridge extends Stack's client content into the
native titlebar area without removing native window authority.

The program bar remains Stack-rendered UI. The difference is that it is rendered
inside an extended native titlebar region rather than inside a manually
undecorated window. Windows still owns the system caption buttons, resize frame,
activation rules, snap behavior, and modal dialog ownership.

Only actual Stack controls in that titlebar band are registered as passthrough
regions. Empty titlebar space remains draggable by Windows. This is why the tab
icons and settings button can be clickable while the rest of the bar still drags
like a normal titlebar.

On the affected PC, `InputNonClientPointerSource::GetForWindowId()` can fail
with `0x80070005` after `ExtendsContentIntoTitleBar(true)` has already succeeded.
That is a partial-success state, not a full fallback. Treating it as inactive
causes the app to draw under the titlebar while laying out controls as if the
titlebar were normal, which puts the settings icon under the Windows caption
buttons and prevents icon actuation. In this state Stack must keep AppWindow
titlebar layout active and install the native WndProc click fallback.

File dialogs use the main HWND as owner. Before opening, Stack releases cursor
capture and clears accidental topmost state from the owner if present. The
dialog path should stay simple and native.

Popouts are owned by the main window, so they stay above Stack without becoming
globally always-on-top.

## Build Behavior

The normal Windows build enables the AppWindow titlebar bridge by default:

```powershell
.\build.cmd
```

`tools\build_stack.ps1` explicitly configures:

```text
STACK_ENABLE_APPWINDOW_TITLEBAR=ON
```

If the local package exists at:

```text
_workspace\deps\Microsoft.WindowsAppSDK\2.2.0
```

the build passes it to CMake automatically. CMake also searches the user's NuGet
cache and the local `_workspace\deps` package locations.

If the Windows App SDK compile package is missing, Stack still builds and falls
back to the normal native titlebar/client program bar behavior. The fallback
must remain functional.

`STACK_DISABLE_APPWINDOW_TITLEBAR=1` can be used as a narrow troubleshooting
escape hatch. It should not become a regular user-facing mode.

## Diagnostics

Trace flags remain available but are off by default:

- `STACK_MAIN_WINDOW_TRACE=1`
- `STACK_FILE_DIALOG_TRACE=1`
- `STACK_DETACHED_PREVIEW_TRACE=1`
- `STACK_SHUTDOWN_TRACE=1`
- `STACK_APPWINDOW_TITLEBAR_TRACE=1`

Use these when debugging z-order, activation, file dialog ownership, popout
ownership, or shutdown presentation. Do not leave trace launch scripts in the
repo root; run traces intentionally from a developer shell.

## Regression Checklist

After any future titlebar, program bar, window style, dialog, popout, or shutdown
change, verify:

- the main HWND is not topmost
- the main window remains decorated/resizable from Windows' point of view
- native drag, resize, minimize, maximize, close, and snap layouts work
- the settings button and tab icons in the titlebar band are clickable
- empty titlebar space drags the window
- Add Image opens the file picker above Stack
- background image picker and project import/export dialogs open above Stack
- preview popout opens above Stack without becoming globally always-on-top
- closing Stack visibly presents the closing surface before teardown
- fallback without AppWindow titlebar support still works

## Decision Rule

If a future change requires arbitrary visuals in the exact system titlebar area,
try `AppWindowTitleBar` first. If it cannot support the desired interaction,
prefer reducing the visual ambition over returning to an undecorated main window.

The affected-PC testing showed that reliable native window behavior is more
important than fully custom non-client chrome for Stack.
