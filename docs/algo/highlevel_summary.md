# Image-to-brush toolpaths

## MVP strategy

PainterBot uses color quantization and serpentine scanlines. This fits a plotter with one fixed brush width and yields a deterministic sequence that can be previewed as soon as the plan is ready.

1. Fit the image proportionally into the canvas and center it on a grid of 4 mm brush cells. Round to whole cells and area-average the source pixels that contribute to each cell.
2. Cluster the samples with deterministic k-means and a configurable palette limit of 2–32 colors. Associate each used color with normalized fractions for cyan, magenta, yellow, black, and white.
3. Divide the grid into four-connected regions of the same palette color. Paint larger regions first. For equal sizes, paint regions touching the image edge first, then use discovery order. This approximates a background-to-detail sequence using region size and position.
4. Within each region, paint horizontal runs, alternating direction on successive grid rows. A run starts and ends at cell centers in millimetres from the canvas's top-left corner. A one-cell run is a dab. Separate runs imply a lifted brush during travel.
5. Assign each cell its stroke number. At any slider position, show the cells covered by strokes completed so far. The completed frame matches the quantized image. The preview models nominal square cells and full paint coverage. Actual brush tips, paint coverage, and edges will depend on the painting setup.

The 1000 × 550 mm limit bounds the grid to 250 × 137 cells. After downsampling, planning and preview work depend on the grid and palette size. The strategy is independent of wxWidgets behind `IAlgorythm`.

## Other strategies considered

| Strategy | Fit for PainterBot |
| --- | --- |
| Region contours with angled interior fills | Orient the strokes to the shape or to an artist-specified direction field. This can improve the appearance of a single-brush painting, but requires clipping strokes to a brush-width inset and handling thin shapes or holes. Gülzow and colleagues' e-David research also uses camera feedback to fill residual gaps left by real strokes. [e-David research](https://edavid.de/research/) |
| Gradient-guided curved strokes | Hertzmann follows local image structure and adds detail in coarse-to-fine passes. Curved strokes can better match mountains and clouds. The published approach uses multiple brush sizes, so a single-width adaptation would need to vary stroke length or density and account for overlaps. [Hertzmann, SIGGRAPH 1998](https://mrl.cs.nyu.edu/publications/painterly98/) |
| Semantic segmentation with depth | Estimate object masks and relative depth before laying down strokes. This can order a foreground subject after its background even when the subject is larger. It introduces model dependencies and planning errors from uncertain depth or masks. [Stroke-based Rendering and Planning for Robotic Performance of Artistic Drawing](https://arxiv.org/abs/2210.07590) |
| Vectorized semantic regions | Trace region outlines, then derive stroke orientation and shape from the region geometry. It offers more control than horizontal fills but needs a policy for converting the resulting strokes to the physical brush width. [Vectorized Region Based Brush Strokes for Artistic Rendering](https://arxiv.org/abs/2506.09969) |

The MVP mix ratios use RGB channel differences as a starting estimate. Matching acrylic colors on canvas will require measured swatches and a calibrated pigment model. Sochorová and Jamriška demonstrate a practical approach based on real pigment behavior. [Mixbox](https://scrtwpns.com/mixbox/)
