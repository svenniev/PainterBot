# Goal

- rethinking the algorythm creating the image preparation

# Preface

- This is a direct continuation of 'C:\Meerkat\PainterBot\docs\specs\001_onboarding.md'
- In the preceeding specification, all tasks are executed as expected, except the image preparation algorythm - there I would like to do it completely different

# Objective

- Redesign / create a new image preparation algorythm
- Currently, it creates a low resolution texture from the image and calculates averag pixel color and creates patches of pixels within a threshold
- Expected workflow:
    - The algorythm creates polygons of solid color, as vector graphics of similar colors
    - Currently, there is a limit of maximum 32 colors - extend it to 1024
    - Find an algorythm that creates infinite resolution polygon pours from the loaded image
    - Sequence the begotten polygons to establish a drawing order:
        - Largest solid polygons go bottom layers (likely in the future this will be painted with thick brush - no need to support it for now)
        - smaller features going on top
        - for similar colors (e.g. different shades of the same color), I would like to have them painted in a 'from dimmest to brightest' order
    - The aim is to perserve insane details down to the tiniest features to make stunning paintings

# Tasks

## 1. Control to show the vector graphic patches

- Create a new UI control that can display with adequate performance thousands of filled, solid color polygons
- Use whatever technology You find most convenient. I would use modern OpenGL with buffers and some 3rd party library to create triangulated polygons from the created patches.

## 2. Redesign the image prepartion aglorythm

- Redesign the algorythm, according to the workflow described in '# Objective'
- No need to keep the existing algorythm, overwrite that.
