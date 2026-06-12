# Building Obsidian

## Windows local build

Install:

- Git
- CMake 3.22 or newer
- Visual Studio 2022 Community, or Build Tools for Visual Studio 2022
- Visual Studio workload: Desktop development with C++

Then run:

```bat
git clone <your-repo-url>
cd Obsidian
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Expected outputs:

```text
build/Obsidian_artefacts/Release/VST3/Obsidian.vst3
build/Obsidian_artefacts/Release/Standalone/Obsidian.exe
```

## GitHub Actions build

This repo includes `.github/workflows/windows-build.yml`.

After pushing to GitHub, open the repository, go to **Actions**, choose **Windows Build**, and run it manually or let it run on push. The compiled files will appear under the workflow artefacts.

## Notes

JUCE is fetched automatically by CMake on the first configure step, so the first build requires internet access.
