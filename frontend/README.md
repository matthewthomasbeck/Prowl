# Prowl: Material Reconnaissance (frontend)

The React frontend for **Prowl**, built for GeauxHack (theme: "Life in Space"). Prowl is a small wheeled robot with a camera and LiDAR that scouts possible materials in a mock Moon area. It is styled like a sci-fi exploration game's mission screen. It helps an Earth-side reviewer see what was observed, where it was found, why it might matter for people living off Earth, and what to investigate next.

> **Demo data until a robot connects.** On its own, the dashboard shows made-up data: every map position, sample, time and note is invented, and material names are **simulated and unverified**. When the Prowl backend is running and the rover (or the simulator) connects, the same screens show its live data instead, and every label says where the data comes from. Nothing is ever sent to Earth.

This is the `frontend/` folder of the Prowl team repository. The backend is in `../backend`, and the message formats are in [../backend/PROTOCOL.md](../backend/PROTOCOL.md). See **Live data** below.

## Run it

You need Node.js. In this folder:

```bash
npm install
npm run dev
```

In Windows PowerShell, if `npm` is blocked by a script policy message, use `npm.cmd run dev` instead.

Open the address it prints (usually http://localhost:5173/). To check the code, run `npm run lint` and `npm run build`.

## Live data

Run the backend and the simulator (see [../backend/README.md](../backend/README.md)), then `npm run dev`. The header changes from "Simulation — robot not connected" to "Simulator connected — live", and:

- **The map** gets the rover's LiDAR map drawn over the ground photo: bright outlines are things the LiDAR hit, a faint green tint is ground it has seen, and see-through means not seen yet. The robot and its traveled path are its real position.
- **Points of interest** are what the camera flagged (`P-001`, …) and **obstacles** are what the LiDAR outlined (`O-001`, …). Their text says they are detector guesses, never test results.
- **Review status, notes and "Queue report for Earth review"** are saved on the backend and appear on every open dashboard, during the run and after it. Queued reports say "Saved to mission log", never "Delivered to Earth".
- **Labels follow the source.** Simulator data says "simulator" everywhere; real rover data says "camera detection" or "LiDAR obstacle". The demo data keeps its "simulated" labels.
- **When the rover drops**, the header says "offline — showing last data". After the run, it says "Run ended — showing its final data".

During `npm run dev`, `/ws` and `/api` are forwarded to a backend on this computer (`http://localhost:8000`). To use the EC2 server instead, create a file called `.env.local` in this folder containing `PROWL_BACKEND=http://<ec2-public-ip>:8000`. For the demo, the backend serves the built dashboard itself at `http://<ec2-public-ip>:8000/`.

## Background image and credit

The page background is **NASA's color map of the whole Moon**, made from Lunar Reconnaissance Orbiter camera data. It is a flat map rather than a photograph, so the dark seas and craters of the whole Moon are visible and it is clearly the Moon. (Two closer-up pictures were tried first, an Apollo 16 photo and a Mars photo, but they were too zoomed in to recognize.) It is real NASA imagery, not AI-generated, and the same file is used by the Moon globe.

- **Credit:** NASA's Scientific Visualization Studio
- **Source:** NASA CGI Moon Kit, https://svs.gsfc.nasa.gov/4720
- **Usage:** NASA's visualization studio says its content is public domain unless noted. NASA also asks you to credit it, not to imply NASA endorses this project, and not to use NASA logos. NASA describes the image as "optimized for aesthetics, not science." Full notes and the date checked are in [src/assets/CREDITS.md](src/assets/CREDITS.md).
- **On phones** the tall screen shows a narrower slice of the map, so less of the Moon is visible than on a laptop.
- **It is decoration only.** It is not live robot footage and not the mapped location. The page says so under the dashboard. The exploration map is a separate, solid dark screen so the two are never confused.

The sections have no boxes or outlines. Their content floats straight on the photo, like the header, with only a thin line under each section title. Behind each section a soft layer gently blurs and dims the photo, and it fades out at its edges so no edge shows. That keeps the text readable while the photo still shows through. Settings at the top of `src/index.css` control it: `--glass` (tint), `--glass-dim` (how much the photo behind is darkened), `--glass-blur` (how much the photo behind is blurred; kept small so the photo stays sharp) and `--edge` (the thin lines). The exploration map is a separate solid screen with its own frame.

## Obstacles (simulated)

Besides the material samples (P-001 to P-003), the map can show **obstacles**: things in the robot's way. They have their own IDs (O-001, O-002), their own short list under the points of interest, and a different map marker: a white warning triangle with the ID, so they differ from the sample pins by shape and not only by color.

- **They are made up.** The two obstacles in `src/data/sampleMission.js` are labeled "simulated" everywhere. Nothing is detected by a real robot or sensor, and the dashboard does not claim that the camera or LiDAR can find obstacles. The real data will come from your teammates.
- **A person adds the note.** Select an obstacle (from the map or the list) and the inspector opens with the same editable "Reviewer note" as the samples. Its statuses are New, Noted and Cleared, and "Queue report for Earth review" works the same way: it only adds a local "Queued locally — demo only" entry. The inspector says "Why it matters" instead of "Possible value" for an obstacle.
- **Adding more:** add another item to `initialObstacles` in the data file, with a new unique ID (like O-003) and a position inside the survey zone. In a real version, new obstacles would arrive from the robot or the backend instead.
- **Counting:** obstacles do not count toward the Review progress bar, which counts only samples. An "Obstacles (simulated) — 1 new" line stays visible at the top of the list and jumps to the obstacle list.

## Accessibility

- **Color-blind mode:** the "Color-blind mode" button in the header swaps the two accent colors. Green (robot and selection) becomes sky blue, and amber (needs review) becomes orange. These come from the Okabe-Ito palette, which stays easy to tell apart for the common kinds of color blindness. In a simulation of red-green color blindness, the difference between the two accents measured about 33 to 41 in the default colors and about 100 to 111 in this mode (where about 20 starts to look the same). The switch is remembered in this browser.
- **Color is never the only clue:** every status has a symbol and a word (○ Needs analysis, ▲ Prioritize sampling, ✓ Reviewed). The selected pin is filled and bold with a targeting frame. In color-blind mode, pins that still need review also get a dashed outline, and the switch itself says ON or OFF.
- **Keyboard:** everything can be reached with the keyboard, with a visible focus ring. The map and the Moon globe respond to the arrow keys and + / −. Escape closes the iPad drawer and the Moon window.
- **Screen readers:** buttons and pins have descriptive labels, the on/off switch reports its state, and the review messages are announced when they change.
- **Reduced motion:** if your device is set to reduce motion, the animations (the targeting frame glide, the scan-in reveal, the pulses, the globe's slow spin) are turned off.
- **Touch:** buttons and tabs are at least 44 pixels tall on phones and iPads, apart from the sample pins on the phone map, which are 32 pixels; the same samples can also be chosen from the Findings tab.
- **Text:** the main text measures at least 10:1 against the picture behind it, and the smallest gray text stays above 4.5:1.

## The Moon globe

The "Moon globe" button in the header opens a window with an interactive globe of the Moon. You can drag it to turn it, and scroll, pinch, double-click or use the + and − buttons to zoom (up to 6×), a bit like a map app. Arrow keys and the + / − keys work too, and "Reset view" puts it back.

- **How it is made:** there is no 3D library. `MoonGlobe.jsx` draws the sphere by hand on a `<canvas>`. For every pixel it works out which spot on the Moon it shows and copies that spot's color from a flat NASA map of the Moon. `MoonGlobeWindow.jsx` puts it in the browser's built-in `<dialog>` window, which gives the dimmed backdrop, the Escape key and keyboard focus for free.
- **The map image:** NASA's Scientific Visualization Studio "CGI Moon Kit" color map (`moon-map-2k.jpg`), used unchanged. NASA says it is "optimized for aesthetics, not science." Credit and links are in [src/assets/CREDITS.md](src/assets/CREDITS.md) and in the window itself.
- **Close-ups get soft.** The map is only 2048 pixels wide, so past about 3× there is no more detail to show. The window says so.
- **It is decoration only.** The globe does not show where the robot is, and the fictional mission area is not marked on it. The elevation maps from the same NASA kit are not used.

## The game-screen idea

| In the game | In Prowl |
|---|---|
| The game world | The exploration map. It fills the screen, and the other panels float over its sides. |
| The player's rover | The green robot marker, with a slow pulse ring |
| Points of interest | The sample pins P-001 to P-003, also listed on the left |
| Investigation / inventory panel | The "Investigation" panel (the sample inspector) |
| Quest or event log | "Mission activity", newest event first |

## What it shows

- **Top bar (no box):** "Prowl | Material Reconnaissance", then Mission, a clearly labeled simulated mission time, and a "Simulation — robot not connected" indicator
- **Exploration map (center):** the Moon itself, which you can zoom in and out of like a map app. It starts on the made-up "Survey Zone A", drawn over a sharp NASA photo of lunar ground (Apollo 16, credited under the map), with the robot, its traveled path (just a line, no shaded area around it), a ruler of local grid numbers, and selectable sample pins. **Zoom out** and the view widens to NASA's map of the whole Moon, with a dashed outline and a "Survey Zone A" marker showing where the zone is (click the marker to zoom back in). The survey zone is made up and is not a real place on the Moon. It is labeled "Simulated map — local coordinates." There is no elevation or terrain-depth information. **Controls:** drag to pan; scroll, pinch or double-click to zoom (centered on the cursor or fingers); the + / − buttons, "Zone" (back to the survey zone) and "Moon" (the whole Moon); or the arrow keys, + / − and 0. The zoom label counts 1.0× at the survey zone, down to 0.1× for the whole Moon and up to 4.0× close up. Pins and the robot appear once you are close enough, and selecting a sample from the list flies the map to its pin.
- **Points of interest (left):** P-001 candidate water-bearing material, P-002 loose surface material, P-003 unknown material, each with a review status, plus a review-progress bar. The bar is real: it counts how many samples are marked Reviewed. It is the only "meter" on the screen, because every other number would have to be invented.
- **Rover camera (left):** "Camera offline — awaiting robot connection." There is no video.
- **Investigation (right):** sample ID, discovery time, local coordinates, then Observed, Possible value, Still unknown and Next step. It also has an editable reviewer note, a review status menu and a "Queue report for Earth review" button. Nothing says the camera or LiDAR can confirm what a material is.
- **Mission activity (bottom):** discovery events and local queue activity. Queued reports read "Queued locally — demo only." The interface never says "Delivered to Earth."

## How selecting feels

Clicking a pin or a point of interest selects the same sample everywhere. A targeting frame glides to the pin and a ring pings out, the list row lights up, and the investigation panel "scans in" with a thin sweep line and its details fading in one after another. Queueing a report makes the confirmation text land with a small stamp. Changing the status or note and queueing again shows "Queue updated report." Queueing the same unchanged report twice is blocked. If your device is set to reduce motion, all animations are turned off.

## Screen sizes

- **Desktop (1100px and wider):** the map is the whole world, with points of interest and camera floating on the left and the investigation panel on the right. On a laptop-height window everything fits, and long panels scroll inside themselves.
- **iPad (720px to 1099px):** a large map with points of interest and camera beside it. "Open details" slides the investigation panel in as a drawer. Escape, the Close button or tapping outside closes it.
- **Phone (under 720px):** Map, Findings and Review tabs. "Open details" on the Map tab jumps to Review.

## How the code fits together

```
src/
  data/sampleMission.js      All the made-up data, used until a robot connects
  live/useProwlLink.js       The WebSocket connection to the backend, and the latest live state
  live/liveMission.js        Turns live data into the same shapes as sampleMission.js (and the finding text)
  live/liveMap.js            Recolors the LiDAR map image for the exploration map
  live/labels.js             The wording for demo, simulator and robot data
  live/useNow.js             The clock for the live mission time
  data/backgroundPhoto.js    Credit text for the background photo
  assets/                    The background photo and its CREDITS.md
  hooks/useMissionClock.js   The simulated mission clock
  hooks/useMediaQuery.js     Tells App whether the screen is iPad-sized
  App.jsx                    Keeps the shared state and passes it down to the panels
  App.css                    The three layouts, and the angular panel shape used everywhere
  components/
    MissionHeader.jsx        Top bar
    MobileTabs.jsx           Phone tabs
    FindingsList.jsx         Points of interest list
    MissionMap.jsx           The map, rover, path, targeting frame and clickable pins
    SelectionBar.jsx         "Selected" bar with Open details (iPad and phone)
    SampleInspector.jsx      The investigation panel (a drawer on iPad)
    CameraPanel.jsx          The offline camera panel
    ActivityStrip.jsx        Discovery and local queue activity
    StatusBadge.jsx          Status label with a symbol, so it never relies on color alone
```

Data flow: `App.jsx` holds the state: the list of samples, which sample is selected, the local queue, the phone tab and whether the iPad drawer is open. When live data is available, `App.jsx` passes that to the panels instead, and review actions are sent to the backend rather than kept on the page. It passes what each panel needs down as props. When something happens (a click, a typed note, a status change), the panel calls a function that `App.jsx` gave it, `App.jsx` updates its state, and React redraws every panel that uses that data.

Look and feel: colors live as variables in `src/index.css`. Green marks the rover and the active selection, and amber marks findings that still need review. The borderless sections use CSS `backdrop-filter` (blur and dim) with a `mask-image` fade (explained in a comment in `App.css`). The map texture is made with an SVG filter, and the small section icons are SVG shapes drawn in code (`SectionIcon.jsx`). The futuristic details are tiny spaced-out labels, outlined uppercase status chips, glowing accent lines and a slow pulse on the simulation dot. The only image files are two real NASA images: the whole-Moon map (page background and globe) and an Apollo 16 photo of lunar ground (under the exploration map).

## Starter files and third-party material

- **Starter (boilerplate):** the project was created from the official Vite React starter (`npm create vite@latest -- --template react`). Its basic files (`index.html`, `vite.config.js`, `src/main.jsx`, `.oxlintrc.json`, and the favicon) come from that starter. Everything else was written for this project.
- **Libraries:** only React and React DOM, plus Vite and Oxlint for building and checking. There is no UI library and no 3D library.
- **External APIs:** none. The dashboard only talks to the team's own Prowl backend (`../backend`). The two NASA images are plain files bundled with the app.
- **Images:** two real NASA images, credited on screen and in [src/assets/CREDITS.md](src/assets/CREDITS.md). No images or video were generated by AI.
- **Design inspiration:** one public design shot was looked at for general mood only (a dark palette, thin lines, spacing). No code, layout or assets from it were copied.

## AI assistance

**All of the code in this folder is AI-assisted.** An AI coding assistant (Claude Code, by Anthropic) helped write this interface. It wrote and edited the React components, CSS and sample data, and explained the code in small steps. It also viewed one public design shot for general inspiration (a dark charcoal palette, thin technical borders, spacing and hierarchy). No layout, code or images from it were copied, and the game-style layout and animations are original. No images or video were generated. The background and the globe use a real NASA map, and the exploration map uses a real NASA photo. The assistant found them, checked them against NASA's usage guidelines, and cropped and resized the photo (see above). The globe's drawing code was written by the assistant, with no 3D library; everything else is made with React, SVG and CSS. The project owner reviews, runs, edits and explains the code. All data is made up.

The live-data connection (`src/live/`, and the small changes in `App.jsx`, the components and `vite.config.js` that use it) was also written with an AI assistant (Claude, by Anthropic) for the team, along with the backend in `../backend`.

Update this section if the way AI is used changes.
