# The dashboard (`frontend/`)

A React app, built with Vite, that shows what the robot sees and drives it. It was made for GeauxHack (theme:
"Life in Space"), so it's styled like a sci-fi game's mission screen for a rover scouting a mock Moon area. It
only ever talks to the robot's dashboard bridge ([protocol.md](protocol.md)); `./prowl` builds it and serves
it on the Pi ([setup.md](setup.md)).

## What it shows

**The header:** "Prowl | Material Reconnaissance", the mission (the bridge's `mission_name`), the time since
the robot started, the **Moon globe** and **Color-blind mode** buttons, and the connection: "Robot connected —
live", "Robot offline — showing last data" (the robot or its sensors went quiet), "Connecting to the robot…"
or "Robot not connected". Under it, tabs switch between three pages. The Camera and Photos pages have their
own addresses (`#camera`, `#photos`), so the browser's Back button works.

**The Map page** is the exploration map panel, which switches between two views (it starts on the scope):

- **LiDAR scope:** what the LiDAR sees right now, with the robot in the middle and forward up, distance rings,
  2, 4 and 8 m ranges, the last few scans fading out, and the distance to the nearest thing in front, behind
  and on each side (flagged under 0.4 m). It's not a map: the robot doesn't track its position.
- **Map:** a whole flat Moon you can zoom and pan like a map app, with the fictional survey zone on it. Close
  up, an Apollo 16 photo of lunar ground fades in inside the zone, with a grid and the robot in the middle
  (it doesn't track its position yet, and the map says so). Drag to pan; scroll, pinch or double-click to
  zoom; the + / −, Zone and Moon buttons; or the arrow keys, + / − and 0. It's decoration: the zone isn't a
  real place and the robot isn't there.

Beside it, the **Rover camera** panel opens the Camera page and shows the newest photo, which opens the
Photos page. The video isn't played on the Map page, so it stays light.

**The Camera page:** the live video, as big as fits, with the frame rate actually shown and a Live / Stalled /
Offline tag; **Take photo** (full resolution, 4608×2592, kept on the robot; the video pauses for about a
second); and the newest photo.

**The Photos page:** every photo the robot has kept, newest first, with a viewer (newer / older, full size,
download) and a **Delete** button, which deletes it for everyone. Photos can show people near the robot.

**The remote control**, along the bottom of every page once the robot has answered:

- Hold a button to move: forward and backward, slide left and right (the wheels are mecanum), turn left and
  right. Let go to stop. With **Keyboard** on: W S A D, Q E and the arrow keys; Space or X stops. Commands are
  always full speed, which is easiest on the wheels.
- **Stop** works for anyone, whoever is driving.
- A warning while the motors are in dry run or the motor driver isn't answering, and errors such as "another
  dashboard is driving".
- The **Robot drive token** box, when the robot has a token set ([setup.md](setup.md#the-drive-token)).
- What the robot reports back: the four wheel duties (seen from above), the IMU's turn rate and tilt (a bubble
  level), and whether the LiDAR, motors and IMU are sending.

**The Moon globe** button opens a window with a globe of the Moon, drawn by hand on a `<canvas>` (no 3D
library): for every pixel, `MoonGlobe.jsx` works out which spot on the Moon it shows and copies that spot's
color from NASA's flat Moon map. Drag to turn it; scroll, pinch, double-click or + / − to zoom (up to 6×;
the map is only 2048 pixels wide, so close-ups get soft). It turns slowly until touched, unless the device
asks for reduced motion. It's decoration too: it doesn't show where the robot is.

Until the robot answers, the header says it's connecting, the Camera and Photos pages say they need the
robot, and there's no remote control. If the connection drops, the dashboard keeps retrying by itself.

## Keeping up with the robot

The robot sends new readings several times a second, and a page that re-draws too much falls behind it. In
Firefox on the Pi, the dashboard first managed about half a frame a second; with these changes, about 15:

- **No blur and nothing that loops.** The panels are a plain dark tint over the background, without
  `backdrop-filter`: re-blurring the picture behind every panel for each new reading was the main cause.
- **The fast readings re-draw only what shows them.** LiDAR scans, wheel duties and IMU readings (5–10 a
  second) are kept outside React's state, and only the scope and the readouts subscribe to them
  (`useLiveValue` in `src/live/useProwlLink.js`).
- **The video is pulled one frame at a time** from `/api/camera/frame.jpg`, asking for the next as soon as
  the last one arrives (at most 15 a second), so it's never more than a frame behind however slow the link.
  `vite.config.js` keeps the proxy's connections to the robot open, or every frame would need a new one.
- **`./prowl` serves the built dashboard** (`vite preview`), which is much lighter in the browser than
  `npm run dev`.

## Screen sizes

- **Desktop (1100 px and wider):** the map panel fills the screen, with the camera panel floating over its
  lower left corner. On a laptop-height window everything fits without scrolling.
- **Tablet (720–1099 px):** the map, with the camera panel beside it.
- **Phone (under 720 px):** the page tabs sit under the header, and the Map page is just the map.

## Accessibility

- **Color-blind mode** swaps the two accent colors, green (the robot, what's active or live) and amber
  (warnings), for sky blue and orange from the Okabe-Ito palette, which stay easy to tell apart for the common
  kinds of color blindness. The switch is remembered in the browser.
- **Color is never the only clue:** warnings have a ⚠ and words, sensors show ✓, ⚠ or ○, and switches say
  ON or OFF.
- **Keyboard:** everything can be reached with the keyboard, with a visible focus ring. The map and the globe
  respond to the arrow keys and + / −. Escape closes the globe and the photo viewer.
- **Screen readers:** buttons have descriptive labels, the switches report their state, the LiDAR scope reads
  out the distance on each side, and the connection and driving status are announced when they change.
- **Reduced motion:** if the device asks for it, nothing slides or fades, and the globe doesn't spin.
- **Touch:** buttons and tabs get bigger on phones and tablets.

## How the code fits together

```
frontend/
  index.html, public/favicon.svg
  vite.config.js         forwards /ws and /api to the robot's bridge (PROWL_ROBOT)
  src/
    main.jsx             starts React
    App.jsx              the pages, and the state more than one panel needs
    App.css              the layouts for each screen size, and the panel look used everywhere
    index.css            the colors (and color-blind mode) and the page background
    live/useProwlLink.js the WebSocket connection to the robot, and its latest state
    live/useDrive.js     hold-to-drive: repeats the held command 10 times a second, lets go on release
    live/useNow.js       the clock for the header
    data/backgroundPhoto.js  credit text for the NASA images
    assets/              the two NASA images (see docs/credits.md)
    components/
      MissionHeader.jsx  the top bar and the page tabs
      MobileTabs.jsx     the page tabs on phones
      MissionMap.jsx     the exploration map, and the switch to the LiDAR scope
      LidarScope.jsx     the LiDAR around the robot
      CameraPanel.jsx    the camera panel on the Map page
      CameraPage.jsx     the live video and Take photo
      PhotosPage.jsx     every photo the robot kept, with a viewer
      DriveBar.jsx       the remote control, wheels, IMU and sensor health
      MoonGlobeWindow.jsx, MoonGlobe.jsx  the Moon globe
      SectionIcon.jsx    the small line icons, drawn in SVG
```

`useProwlLink` keeps the robot's latest state; `App.jsx` passes each panel what it needs as props, and the
panels call functions it gives them when something happens. Driving goes over the WebSocket (`useDrive`);
taking, listing and deleting photos are plain HTTP requests to `/api/camera/`.

The colors are variables at the top of `src/index.css`, including `--glass`, how dark the panels' tint is. The
panels have no boxes or outlines: behind each one, a soft dark layer fades out at its edges, so the content
floats on the background with only a thin line under each section title. The map and the scope are solid dark
"instrument screens" with their own frames, so the decorative background never looks like part of the map.

## Working on it from a laptop

You need Node.js 20.19 or newer. Point the dev server at the robot (with `./prowl` running on it):

```bash
cd frontend
npm install
echo 'PROWL_ROBOT=http://<robot address>:8000' > .env.local
npm run dev          # http://localhost:5173/, reloads as you edit
npm run lint         # oxlint
npm run build        # what ./prowl serves
```

Without `.env.local`, the dev server forwards to `http://localhost:8000`, the bridge on the same machine. Add
`?token=<drive_token>` to the address if the robot has a drive token. In Windows PowerShell, if `npm` is
blocked by a script policy, use `npm.cmd`. The Pi has no Node.js of its own: `./prowl` builds and serves the
dashboard with the dev container's.
