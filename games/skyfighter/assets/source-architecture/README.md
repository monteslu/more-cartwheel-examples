# Architectural material atlas

Generated with the built-in imagegen tool on 2026-10-17. Source: `facade-atlas-v1.png`.

Prompt:

Use case: photorealistic-natural. Asset type: seamless game texture atlas, flat orthographic albedo, square 2048 by 2048. Create one precisely arranged 2 by 2 atlas of four architectural wall textures, each quadrant filling its exact square with no gutters: top left weathered warm red Kremlin brickwork, small detailed bricks, subtle mortar; top right pale cream and white neoclassical palace facade, three full floors of tall regularly spaced arched dark-glass windows with white carved trim and restrained pale yellow plaster; bottom left grey-beige snowy European city apartment facade with five floors of aligned dark windows and realistic stone details; bottom right dark blue-grey modern high-rise facade, eight regular rows of glass windows and dark metal mullions with occasional warm interior lights. Every quadrant is a perfectly frontal flat rectangular wall face, no perspective, no roof, no sky, no ground, no people, no text. Photographic material detail and crisp architecture. Uniform diffuse overcast lighting with no baked directional shadows, no vignette or gradients, no plaid or checkerboard. Edges of each quadrant should wrap horizontally as a continuous wall. This is a texture used on actual three dimensional game buildings, not a picture of buildings.

The generated source is 1254 x 1254. The four quadrants are extracted and converted to 512 x 512 runtime JPEGs with ffmpeg. Final game assets: `assets/textures/kremlin-brick.jpg`, `palace-facade.jpg`, `city-stone.jpg`, `city-glass.jpg`.
