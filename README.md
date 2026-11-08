# QuickOpenNpp

A lightweight, native Windows Notepad++ plugin. Press **Ctrl+Alt+Shift+S**, type a fuzzy filename query, select a result with Up/Down, and press Enter to switch tabs. Escape closes the popup.

## Build (Windows)

Prerequisites: Visual Studio 2022 with **Desktop development with C++**, Windows SDK, CMake.

Open a **x64 Native Tools Command Prompt for VS 2022** and run in this directory:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The result is `build\Release\QuickOpenNpp.dll`.

For 32-bit Notepad++, use `-A Win32` instead of `-A x64` and make sure the plugin architecture matches Notepad++.

## Install

1. Exit Notepad++.
2. Find your Notepad++ installation's `plugins` directory, typically `C:\Program Files\Notepad++\plugins`.
3. Create `plugins\QuickOpenNpp\` and copy `QuickOpenNpp.dll` into it. Final path: `plugins\QuickOpenNpp\QuickOpenNpp.dll`.
4. Restart Notepad++.
5. Use **Plugins > QuickOpenNpp > Quick Open Documents** or press **Ctrl+Alt+Shift+S**.
6. If that shortcut is already assigned or overridden by your settings, configure **Settings > Shortcut Mapper > Plugin commands > Quick Open Documents**.

## Behavior

- Lists only already-open Notepad++ documents from both editor views.
- Includes unnamed/unsaved tabs if Notepad++ provides a buffer path/name.
- Deduplicates files cloned across editor views.
- Fuzzy case-insensitive subsequence match on **filename**; exact substring matches rank higher.
- Up/Down and Page Up/Page Down navigate; Enter switches; Escape cancels; double-click also switches.
- Uses Windows-native UI, no external runtime or network requests.

## Current limitations

- Simple Windows-native appearance; does not yet highlight matched characters.
- Uses tab order for equal-score matches; does not implement most-recently-used order.
- Not built or executed in this Linux environment; the Windows build and runtime behavior need validation on your Notepad++ installation.
- Popup width is fixed in physical pixels; DPI-aware scaling is a future enhancement.

## Notepad++ API used

`NPPM_GETNBOPENFILES`, `NPPM_GETBUFFERIDFROMPOS`, `NPPM_GETFULLPATHFROMBUFFERID`, and `NPPM_ACTIVATEDOC`.

Documentation: https://npp-user-manual.org/docs/plugin-communication/

Source license: MIT (local original code). This implementation uses the public plugin ABI declarations and does not embed third-party SDK files.

## Build online without installing Visual Studio (GitHub Actions)

1. Create a GitHub repository (it can be private).
2. Upload **all files in this project**, including the hidden `.github/workflows/build.yml` file, to the repository root. An easy method is to unpack the ZIP locally, then use GitHub's **Add file > Upload files** on the extracted contents; verify that `.github/workflows/build.yml` is present afterward. If your upload doesn't preserve hidden directories, create that workflow file via GitHub's **Add file > Create new file** and paste its contents.
3. Open your repository's **Actions** tab. Select **Build QuickOpenNpp** and **Run workflow**, or let the first push trigger it automatically.
4. Open the completed workflow run and download the **QuickOpenNpp-x64** artifact under **Artifacts** (or **QuickOpenNpp-Win32** for a 32-bit Notepad++ installation).
5. Extract the downloaded artifact ZIP and install the DLL as described above.

GitHub Actions builds on GitHub-hosted Windows computers, so you don't need Windows or Visual Studio on your own machine. The builds have **not been tested** here and might uncover source issues; check the job logs if a build fails.
