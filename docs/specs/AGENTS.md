# PainterBot project instructions

PainterBot turns a reference image into a physical painting using plotter-style motion, a brush, and paint on canvas. The C++ desktop application plans and previews strokes, then will supervise execution. KiCad electronics will handle motion and the paint, cleaning, and monitoring peripherals. Embedded firmware will expose low-level controls. Keep planning, UI, and eventual machine communication separate.

- The desktop application lives in `application/`. Use C++20, CMake, and wxWidgets. Pin dependencies fetched through CMake. On Windows, configure and build with the `windows-release` preset from `application/`, then run CTest with that preset.
- Keep active document data in `src/viewmodel/`. Route document changes through the command processor and undoable `IAction` implementations. The wxWidgets UI and command-line runner use the same processor. Keep view navigation and preview seeking out of document history.
- Keep image planning independent of wxWidgets behind `IAlgorythm`. Use millimetres for canvas dimensions and stroke positions. Keep the ordered vector plan deterministic. Derive the polygon preview and fixed-brush toolpaths from its shared geometry.
- The onboarding MVP supports a canvas up to 1000 × 550 mm, one fixed brush width, and conceptual ratios for cyan, magenta, yellow, black, and white acrylic paints. Plot previews execution. The motion protocol, real paint calibration, and hardware integration belong to later work.
- Run decoding and paint planning off the GUI thread. Trace at source resolution. Cache vector fills per viewport and keep physical simulation work proportional to the brush grid. Use the committed sample image in `application/tests/res/` for end-to-end coverage and keep generated build output out of Git.

## Local entry points

- In VS Code, select **PainterBot Application (Release)** or **PainterBot Application with sample (Release)** under **Run and Debug**. The shortcuts configure and build the app before launching it.
- From `application/`, run `cmake --preset windows-release`, `cmake --build --preset windows-release`, and `ctest --preset windows-release`. The first configure downloads wxWidgets. The executables are under `out/windows-release/Release/`.
- Pass lines to `PainterBotCommand.exe` on UTF-8 standard input to run an independent session. It accepts `load "PATH"`, `prepare [WIDTH_MM HEIGHT_MM COLORS]`, `tab prepare|plot`, `seek STROKE_COUNT`, `seek-patch PATCH_COUNT`, `undo`, and `redo`. Query `vectors` for the visible polygon sequence as SVG, `status` for counts, `plan` for ordered CSV strokes and paint fractions, or `preview` for the current brush grid as an ASCII PPM image.
