# Credits

Where the images come from, what the project started from, and how AI assistance was used.

## Images

There are three image files in this project, all in `frontend/src/assets/`. All are real NASA imagery. None
is AI-generated.

### moon-map-2k.jpg (page background, the zoomed-out exploration map, and the interactive Moon globe)

- **What it is:** a color map of the whole Moon, made from Lunar Reconnaissance Orbiter Camera (LROC) data. It is a flat, "unrolled" map. The page background and the exploration map (when zoomed out) show it as it is, and the Moon globe window wraps it around a sphere.
- **Name:** "CGI Moon Kit", color map `lroc_color_2k.jpg` (2048 x 1024 pixels, 458 KB)
- **Credit:** NASA's Scientific Visualization Studio
- **Source:** https://svs.gsfc.nasa.gov/4720
- **Changes made:** none to the file. On screen the page background is darkened by a CSS overlay.
- **Note from NASA:** the image is "optimized for aesthetics, not science." It is used here only as decoration. It marks no robot position and not the fictional mission area. The elevation (displacement) maps from the same kit are not used.

### moon-ground.jpg (the ground under the exploration map)

- **What it is:** an Apollo 16 photograph of the Moon's far side, an oblique view of the rim of Guyot Crater, taken from lunar orbit in April 1972 with a Hasselblad camera.
- **Title:** "Oblique view of rim of Guyot crater on lunar farside as seen by Apollo 16"
- **Image ID:** AS16-121-19407
- **Credit:** NASA (Johnson Space Center, Apollo 16)
- **Source:** NASA Image and Video Library, https://images.nasa.gov/details/as16-121-19407
- **Original file used:** `as16-121-19407~orig.jpg` from images-assets.nasa.gov (4108 x 4122 pixels, 2.0 MB)
- **Changes made:** cropped to a 3:2 section (the same shape as the survey zone) and resized to 2400 x 1600 pixels as a JPEG. No other edits.
- **Why it is here:** when you zoom in on the exploration map, this sharper photo fades in over the whole-Moon map inside the survey zone, like a map app loading finer detail. It is decoration only. The survey zone is fictional, so it is not a real place on the Moon, and the robot is not really there. The map says so under its legend.

### earth-map-4k.jpg (the GPS page's Earth globe)

- **What it is:** a cloud-free color map of the whole Earth, from MODIS satellite data, with land relief and sea-floor shading. It is a flat, "unrolled" map, which the GPS page wraps around a globe.
- **Name:** "Blue Marble: Next Generation with Topography and Bathymetry", December 2004, `world.topo.bathy.200412.3x5400x2700.jpg` (5400 x 2700 pixels, 2.6 MB)
- **Credit:** NASA Earth Observatory (produced by Reto Stöckli, NASA Goddard Space Flight Center)
- **Source:** https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography-bathymetry/
- **Changes made:** resized to 4096 x 2048 pixels as a JPEG (1.1 MB). No other edits.
- **Why it is here:** unlike the Moon images, this map is used with real data: the GPS page marks the robot's real position, from its GPS, on it. The map itself is from 2004 and shows no weather and nothing built since.

### Earlier images that are no longer used

A Curiosity rover panorama of Mars (NASA/JPL-Caltech/MSSS, image PIA20840) was the first page background. It was removed.

### Usage terms checked

- **NASA Scientific Visualization Studio** (https://svs.gsfc.nasa.gov/help/): states that its content is in the public domain unless noted. The Moon Kit page asks for credit to "NASA's Scientific Visualization Studio."
- **Blue Marble: Next Generation** (https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/): "Anyone using or republishing Blue Marble: Next Generation please credit 'NASA Earth Observatory.'" The GPS page does, under the globe. Checked on 2026-10-25.
- **NASA media usage guidelines** (https://www.nasa.gov/nasa-brand-center/images-and-media/): NASA content is generally not subject to copyright in the United States. Educational and informational use, including personal websites, is allowed without permission when NASA is credited. Do not imply that NASA endorses this project, and do not use the NASA logo or insignia. Images marked as copyrighted by a third party are not covered. The Apollo 16 photo has no people, logos or third-party copyright notice.
- If this project is ever used commercially or for promotion, re-check NASA's current guidelines first.

Checked on 2026-10-24.

## Starter files and libraries

- The dashboard was created from the official Vite React starter (`npm create vite@latest -- --template
  react`). Its basic files (`index.html`, `vite.config.js`, `src/main.jsx`, `.oxlintrc.json` and the favicon)
  come from that starter. Everything else was written for this project.
- Libraries, used as they are: React and React DOM, with Vite and Oxlint for building and checking (no UI
  library and no 3D library); and on the robot, ROS2 Jazzy, rclnodejs and ws for the bridge, libcamera,
  TurboJPEG, the pigpio client library and ncurses.
- One public design shot was looked at for the dashboard's general mood (a dark palette, thin lines,
  spacing). No code, layout or assets were copied from it.

## AI assistance

Most of the code and these docs were written with an AI coding assistant (Claude Code, by Anthropic), with
team members directing the work, reviewing it and testing it on the robot. No code was copied or adapted from
other projects: the drivers are written from the hardware datasheets and API documentation. For the
dashboard, the assistant wrote and edited the React components, the CSS and the globe's drawing code, found
the NASA images, checked them against NASA's usage guidelines, cropped and resized the Apollo 16 photo, and
resized the Blue Marble map. No images or video were generated.

Keep this section up to date as the project changes.
