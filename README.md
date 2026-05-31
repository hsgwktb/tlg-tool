# TLG/PNG Converter

A Windows desktop application for converting between TLG and PNG image formats, featuring a modern ImGui-based GUI.

## Features

- **Single Conversion**: Convert single PNG ↔ TLG files
- **Batch Conversion**: Convert entire folders of images
- **Modern GUI**: Clean, dark-themed interface built with ImGui
- **No Dependencies Required**: Static-linked executable, runs directly

## System Requirements

- Windows 10/11 (64-bit)
- DirectX 11 support

## Download

Download the latest release from the releases page and extract the ZIP file.

## Usage

1. Double-click `tlg-tool-gui.exe` to launch
2. Choose conversion mode:
   - **Single Convert**: Convert one file at a time
   - **Batch Convert**: Convert all files in a folder

### Single Conversion

1. Select input file using "Browse" button
2. Output path is auto-filled (same directory, different extension)
3. Click "Convert" to start conversion

### Batch Conversion

1. Select input folder containing PNG or TLG files
2. Select output folder (defaults to input folder)
3. Choose conversion direction:
   - PNG → TLG
   - TLG → PNG
4. Click "Convert All" to batch convert

## Build from Source

### Prerequisites

- Visual Studio 2022 Build Tools
- Windows SDK 10.0.19041.0
- libpng and zlib libraries

### Build Commands

Run the build script:

```powershell
powershell -ExecutionPolicy Bypass -File build_imgui.ps1
```

The executable will be created at `tlg-tool-gui.exe`.

## Technical Details

- **GUI Framework**: ImGui with DirectX 11 backend
- **Image Libraries**: libpng, zlib
- **Format Support**:
  - TLG0, TLG5, TLG6 (read)
  - TLG6 (write)
  - PNG (standard)

## License

This project includes code from:
- [png2tlg](https://github.com/nickvn/tvgtool)
- [tlg2png](https://github.com/nickvn/tvgtool)
- [ImGui](https://github.com/ocornut/imgui)

## Changelog

### v1.0
- Initial release
- PNG ↔ TLG conversion
- Single and batch conversion modes
- Modern dark-themed GUI