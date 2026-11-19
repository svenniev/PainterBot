# Goal

- project introduction
- onboarding
- initiating AGENTS.md file
- kickoff of desktop allpication codebase

# Project description

- this is a medium sized project
- The goal is to physically paint paintings from images with actual painting kit, acryl or water based paint on a real canvas
- The project consists of 3 parts:
    - desktop application that creates toolpath from a selected image and communicates with a hardware, supervising the robotics of a painting process
    - Electronics hardware - designed in KiCAD. Motion controller, paint mixing, brush cleaning, painting progress monitoring and analysis
    - Embedded firmware - simple controls
- This will be a 3D-printer or plotter-like project. 

# Tasks

## 1. Initiate and set AGENTS.md file

- Initiate and set AGENTS.md file: 'C:\Meerkat\PainterBot\docs\specs\AGENTS.md' for this project.
- Create a dense description
- Set this as AGENTS.md document for this codex session

## 2. Kickoff of desktop app

- Create a working desktop app with early functionality: 'PainterBot Application'
- App directory: 'C:\Meerkat\PainterBot\application'
- Use modern c++ with cmake
- Initiate cmake: 'C:\Meerkat\PainterBot\application\CMakeLists.txt'
- I prefer fetchcontent for fetching libraries
- Use wxWidgets for UI
- Create testable code and tests ('C:\Meerkat\PainterBot\application\tests')
- active document data shall live in a viewmodel: 'C:\Meerkat\PainterBot\application\src\viewmodel\viewmodel.h', 'C:\Meerkat\PainterBot\application\src\viewmodel\viewmodel.cpp'
- implement undo stack. Every action performed through an action interface: 'C:\Meerkat\PainterBot\application\src\utils\IAction\IAction.h', having 'Do' and 'Undo' methods
    - IAction also enforces possible execution of actions through commands - must have a command processor and thus AI agents can to anything in the app that a user can on the UI
- Prepare cmake integration into visual studio code. I wish to have release builds shortcuts in the 'Run and Debug' pane in vscode of the desktop app.

## 2.1 Main UI of the desktop application

- Create a menu system
- Main UI is split into tabs:
    - Prepare - where the user loads, edits the reference image and designs the toolpaths
    - Plot - This is the UI used while a painting is being executed

## 2.2 Loading reference image

- Implement functionality for loading a reference image - PNG, JPG, BMP, etc...
- Create a UI control that can display the image loaded and the quantized image prepared for painting

## 2.3 MVP algorythm for image preparation

- Create an MVP algorythm for preparing an image for painting
- Research known ways of creating paintbrush toolpaths for this use case
- There will be multiple algorythms tested. Make a strategy pattern around. Maybe use 'C:\Meerkat\PainterBot\application\src\algo\IAlgorythm.h'
- Create a summary of potential algorythms: 'C:\Meerkat\PainterBot\docs\algo\highlevel_summary.md'
- Implement one algorythm that found most promising
- Prepare the UI for executing this and showing results

## 2.4 GIT

- Initiate the GIT repository for this project
- Set up gitignore, etc..

# Expected outcome

- I want to be able to launch a release build of 'PainterBot Application'
- I want top be able to load a reference image: "C:\Meerkat\PainterBot\application\tests\res\above-clouds-mountains-peak-sunrise-moon-winter-cold-scenic-3440x1440-4569.jpg"
- I want to set the size of the painting. The maximum size that will fit within the plotter shall currently be hard-coded: 1000mm x 550mm - later it will be in config
- I want to be able to configure color levels, etc.. whatever can be defined
- I want to click a 'Prepare' button. It shall create segments from the image and order them back to forth, in order as it will be painted
- It shall be displayed in the UI, how the final image will be created and there shall be a slider, which when slided, shall display in order, how the picture will be painted

# Notes

- for now, focus only on an MVP. One brush size, there are cyan, magenta, yellow, black and white acryl paints. Any ratio can be created.
- Design the UI to be high-performance. Do not let it be limited by slow rendering, calculations getting stuck on UI thread, etc..