# QuickOpenNpp v0.2

Native Windows Notepad++ plugin that switches among **already open** documents.

## What's new

- Resizable popup, with saved location and size.
- Two aligned, resizable columns: **File name** and **Folder**.
- Saved column widths, maintained between Notepad++ sessions.
- Correctly rendered Unicode text without a garbled dash separator.
- Escape and close button return focus to Notepad++.
- Clicking outside closes the popup without stealing focus from the app clicked.
- The shortcut `Ctrl+Alt+Shift+S` is unchanged.
- Fuzzy filtering and up/down + Enter navigation are unchanged.

Settings are stored in `QuickOpenNpp.ini` in the user plugin configuration directory, obtained from Notepad++ via `NPPM_GETPLUGINSCONFIGDIR`.

## Build online (GitHub Actions)

Upload these source files to the **root** of your GitHub repository, keeping `.github/workflows/build.yml` nested as shown:

```
CMakeLists.txt
QuickOpenNpp.cpp
README.md
.github/workflows/build.yml
```

Each commit triggers a GitHub Actions workflow that builds x64 and Win32 artifacts. Select the DLL matching your Notepad++ architecture.

## Build locally

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Install

1. Close Notepad++ entirely.
2. Replace the old `QuickOpenNpp.dll` in the *existing* plugin directory (`plugins\QuickOpenNpp\`).
3. Restart Notepad++.
4. Press `Ctrl+Alt+Shift+S`.

The popup can be resized using its edges and corners. Drag a column header divider to resize columns. Each close saves sizes and position. Press Escape to dismiss and return focus to Notepad++.

## Notes

This version is a source-level update; it has not been compiled or runtime-tested on Windows in this environment. The GitHub Actions build is the intended verification step.
