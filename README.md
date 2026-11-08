# QuickOpenNpp v0.4 — Theme-consistent results and headers

This release updates v0.3. It preserves `Ctrl+Alt+Shift+S`, fuzzy filtering, keyboard navigation, resize/column-width persistence, and **Plugins → QuickOpenNpp → Configuration...**.

## What's new

- Owner-drawn column header cells now use the chosen appearance palette.
- The header area after the rightmost column is painted with the same palette.
- Selected rows use custom background and foreground colors, including the unused area to the right of the columns.
- Search and list controls have subtle borders derived from the palette instead of bright Windows 3D edges.
- Header dividers remain draggable and saved column widths continue to work.

## Limitations

The Win32 system title bar and native scrollbars are not fully custom-drawn. Theme changes are picked up when reopening the popup. This source has **not been built or tested on Windows** in this environment.

## Update existing GitHub repository

Replace the root `QuickOpenNpp.cpp` with the file in this archive, commit the change, and open **Actions → Build QuickOpenNpp**. Keep the existing `CMakeLists.txt` if it already links `user32`, `gdi32`, `comctl32`, `comdlg32`, and `uxtheme`.

Or upload all extracted project files, preserving `.github/workflows/build.yml`.

## Install

After successful GitHub Actions build, download the artifact matching Notepad++'s architecture (Win32 or x64), close Notepad++, and replace `plugins\QuickOpenNpp\QuickOpenNpp.dll` in your Notepad++ installation. Restart Notepad++.

If a build fails, capture the first compiler/linker error from the build job.
