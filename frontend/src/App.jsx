import { useState, useEffect, useRef } from 'react'
import './App.css'
import {
  mission,
  mapArea,
  robot,
  travelPath,
  moonWorld,
  statusOptions,
  obstacleStatusOptions,
  initialSamples,
  initialObstacles,
} from './data/sampleMission.js'
import { useMissionClock, formatElapsed } from './hooks/useMissionClock.js'
import { useMediaQuery } from './hooks/useMediaQuery.js'
import MissionHeader from './components/MissionHeader.jsx'
import MobileTabs from './components/MobileTabs.jsx'
import FindingsList from './components/FindingsList.jsx'
import MissionMap from './components/MissionMap.jsx'
import SelectionBar from './components/SelectionBar.jsx'
import SampleInspector from './components/SampleInspector.jsx'
import CameraPanel from './components/CameraPanel.jsx'
import ActivityStrip from './components/ActivityStrip.jsx'
import DriveBar from './components/DriveBar.jsx'
import LidarScope, { LidarLegend } from './components/LidarScope.jsx'
import PhotosPage from './components/PhotosPage.jsx'
import CameraPage from './components/CameraPage.jsx'
import MoonGlobeWindow from './components/MoonGlobeWindow.jsx'
import SectionIcon from './components/SectionIcon.jsx'
import { backgroundPhoto } from './data/backgroundPhoto.js'
import { useProwlLink, pageToken } from './live/useProwlLink.js'
import { useDrive } from './live/useDrive.js'
import { buildLiveMission } from './live/liveMission.js'
import { labelsFor, connectionText } from './live/labels.js'
import { useNow } from './live/useNow.js'

const DRIVE_ERROR_S = 6 // how long a drive error from the robot stays on screen

// The robot's drive token is remembered in this browser, so driving stays unlocked after a reload.
function readToken() {
  try {
    return localStorage.getItem('prowl-drive-token') || ''
  } catch {
    return ''
  }
}

// The robot's Camera and Photos pages have their own addresses (#camera, #photos), so the browser's Back button and
// links work. Everything else is the dashboard (the map page).
const ROBOT_PAGES = [
  { id: 'dashboard', label: 'Map', icon: 'map' },
  { id: 'camera', label: 'Camera', icon: 'camera' },
  { id: 'photos', label: 'Photos', icon: 'scan' },
]
const pageFromAddress = () => {
  const name = window.location.hash.slice(1)
  return name === 'camera' || name === 'photos' ? name : 'dashboard'
}

function App() {
  // App owns the data that more than one panel needs. Panels get it as props.
  // The demo data below is used until a robot (or the simulator) connects through the backend.
  const [samples, setSamples] = useState(initialSamples)
  const [obstacles, setObstacles] = useState(initialObstacles) // simulated obstacles, separate from the samples
  const [selectedId, setSelectedId] = useState(initialSamples[0].id)
  const [queue, setQueue] = useState([])
  const [tab, setTab] = useState('map') // phone only: 'map', 'findings' or 'review'
  const [drawerOpen, setDrawerOpen] = useState(false) // iPad only: is the details drawer open?
  const [globeOpen, setGlobeOpen] = useState(false) // is the Moon globe window open?

  // Color-blind mode is remembered in this browser, so it is still on next time. If the browser blocks
  // storage (for example in a private window), it simply starts off each time.
  const [colorBlind, setColorBlind] = useState(() => {
    try {
      return localStorage.getItem('prowl-color-blind') === 'on'
    } catch {
      return false
    }
  })

  // The page's palette is chosen by an attribute on <html>. The CSS in index.css does the rest.
  useEffect(() => {
    document.documentElement.dataset.palette = colorBlind ? 'colorblind' : 'default'
    try {
      localStorage.setItem('prowl-color-blind', colorBlind ? 'on' : 'off')
    } catch {
      // storage is blocked: nothing to do
    }
  }, [colorBlind])

  // ---- Live data from the Prowl backend, or straight from the robot (see src/live/) ----
  // Once a robot or the simulator has connected, everything on screen comes from it instead of the demo data.
  const [token, setToken] = useState(readToken)
  const link = useProwlLink(token)
  // Connected straight to the robot's own server (edge/src/prowl_dashboard_bridge) rather than the backend. Only then
  // are the remote control, the LiDAR scope, the live camera and the Photos page shown.
  const onRobot = !!link.session && link.capabilities.drive === 'hold'
  const drive = useDrive(link)

  // Connected to the robot, the page drops its most expensive effects (see "Connected straight to the robot" in
  // index.css): the blurred glass behind every panel and the looping animations. With the robot's live readings,
  // those made the browser re-draw the whole blurred page several times a second, which is what made it lag.
  useEffect(() => {
    if (onRobot) document.documentElement.dataset.link = 'robot'
    else delete document.documentElement.dataset.link
  }, [onRobot])
  const [page, setPage] = useState(pageFromAddress)
  const [noteDrafts, setNoteDrafts] = useState({}) // live notes still being typed on this page, by finding id
  const noteTimers = useRef({})
  const live = link.session ? buildLiveMission(link, noteDrafts) : null
  const labels = labelsFor(live ? (onRobot ? 'bridge' : live.mode) : 'demo')
  const connection = connectionText({
    mode: live?.mode,
    connection: link.connection,
    session: link.session,
    robotConnected: link.robotConnected,
  })

  // What the panels show: the live mission, or the demo data
  const shown = live || { mission, samples, obstacles, robot, path: travelPath, scanMap: null, queue }

  const demoElapsed = useMissionClock(mission.elapsedStartSeconds)
  const now = useNow()
  const driveError =
    link.lastError?.forType === 'drive' && now - link.lastError.at < DRIVE_ERROR_S ? link.lastError.error : null
  const topicAges = link.status?.topic_age_s
  const elapsedSeconds = live ? Math.max(0, Math.floor(now - live.startedAt)) : demoElapsed
  const isDrawerMode = useMediaQuery('(min-width: 720px) and (max-width: 1099px)')

  // A sample and an obstacle are both "items you can select". The selection can be either kind.
  // A live mission can start with nothing found yet, so the selection can also be empty.
  const items = [...shown.samples, ...shown.obstacles]
  const selectedItem = items.find((item) => item.id === selectedId) || items[0] || null
  const currentId = selectedItem?.id ?? null
  const isObstacle = selectedItem?.kind === 'obstacle'

  // A note typed here is shown right away, and sent to the backend once typing pauses.
  // When the backend's copy catches up, the draft is no longer needed. (Like the map's fly-to, this adjusts
  // state while rendering when the thing it depends on, the reviews, has changed, so no effect is needed.)
  const [seenReviews, setSeenReviews] = useState(link.reviews)
  if (seenReviews !== link.reviews) {
    setSeenReviews(link.reviews)
    const caughtUp = Object.keys(noteDrafts).filter((id) => link.reviews[id]?.note === noteDrafts[id])
    if (caughtUp.length > 0) {
      const next = { ...noteDrafts }
      caughtUp.forEach((id) => delete next[id])
      setNoteDrafts(next)
    }
  }

  // Has this item's report been queued, and has it changed since?
  const latestQueued = [...shown.queue].reverse().find((entry) => entry.sampleId === currentId)
  let queueState = 'none'
  if (latestQueued && selectedItem) {
    const unchanged = latestQueued.status === selectedItem.status && latestQueued.note === selectedItem.note
    queueState = unchanged ? 'queued' : 'changed'
  }

  // ---- Keyboard focus for the iPad drawer: move into it when it opens, back to the button when it closes ----
  const openButtonRef = useRef(null)
  const inspectorHeadingRef = useRef(null)
  const drawerWasOpen = useRef(false)

  useEffect(() => {
    if (!isDrawerMode) return
    if (drawerOpen) {
      inspectorHeadingRef.current?.focus()
    } else if (drawerWasOpen.current) {
      openButtonRef.current?.focus()
    }
    drawerWasOpen.current = drawerOpen
  }, [drawerOpen, isDrawerMode])

  // The browser's Back and Forward buttons move between the dashboard and the Photos page.
  useEffect(() => {
    const follow = () => setPage(pageFromAddress())
    window.addEventListener('hashchange', follow)
    window.addEventListener('popstate', follow)
    return () => {
      window.removeEventListener('hashchange', follow)
      window.removeEventListener('popstate', follow)
    }
  }, [])

  // Pressing Escape closes the drawer, wherever the keyboard focus is.
  useEffect(() => {
    if (!isDrawerMode || !drawerOpen) return
    function closeOnEscape(event) {
      if (event.key === 'Escape') setDrawerOpen(false)
    }
    document.addEventListener('keydown', closeOnEscape)
    return () => document.removeEventListener('keydown', closeOnEscape)
  }, [drawerOpen, isDrawerMode])

  // ---- Things the panels can ask App to do ----
  // Replaces one item with a changed copy. React needs a new list, so we never edit the old one.
  // The id belongs to either the samples or the obstacles, so we try both lists; only one will match.
  function updateItem(id, changes) {
    const apply = (list) => list.map((item) => (item.id === id ? { ...item, ...changes } : item))
    setSamples(apply)
    setObstacles(apply)
  }

  function changeNote(id, note) {
    if (!live) return updateItem(id, { note })
    setNoteDrafts((drafts) => ({ ...drafts, [id]: note }))
    clearTimeout(noteTimers.current[id])
    noteTimers.current[id] = setTimeout(() => {
      delete noteTimers.current[id]
      link.send({ type: 'review', finding_id: id, note })
    }, 400)
  }

  function changeStatus(id, status) {
    if (!live) return updateItem(id, { status })
    link.send({ type: 'review', finding_id: id, status })
  }

  // Live: the backend saves the report in the mission log, shared with every dashboard. Nothing goes to Earth.
  // Demo: a local queue only. This adds a record to a list on this page; nothing is sent.
  function queueReport(id) {
    if (live) {
      if (noteTimers.current[id]) {
        // a note is still waiting to be sent: send it first, so the report includes it
        clearTimeout(noteTimers.current[id])
        delete noteTimers.current[id]
        link.send({ type: 'review', finding_id: id, note: noteDrafts[id] ?? '' })
      }
      link.send({ type: 'report', finding_id: id })
      return
    }
    const item = items.find((entry) => entry.id === id)
    const latest = [...queue].reverse().find((entry) => entry.sampleId === id)
    // Same item, same status, same note: it is already queued, so do not add it again.
    if (latest && latest.status === item.status && latest.note === item.note) return

    const record = {
      sampleId: id,
      status: item.status, // a snapshot of what the report said when it was queued
      note: item.note,
      queuedAt: formatElapsed(elapsedSeconds),
    }
    setQueue([...queue, record])
  }

  function saveToken(value) {
    setToken(value)
    try {
      localStorage.setItem('prowl-drive-token', value)
    } catch {
      // storage is blocked: the token works until the page is reloaded
    }
  }

  function openPage(id) {
    if (id === page) return
    if (id === 'dashboard') {
      window.history.pushState(null, '', window.location.pathname + window.location.search)
      setPage('dashboard')
    } else {
      window.location.hash = id
    }
  }

  // "Open details" opens the drawer on iPad, or jumps to the Review tab on a phone.
  function openDetails() {
    if (isDrawerMode) {
      setDrawerOpen(true)
    } else {
      setTab('review')
    }
  }

  return (
    <div className="app">
      <MissionHeader
        mission={shown.mission}
        elapsedText={formatElapsed(elapsedSeconds)}
        clockLabel={labels.clockLabel}
        connection={connection}
        onOpenGlobe={() => setGlobeOpen(true)}
        pages={onRobot ? ROBOT_PAGES : null}
        page={page}
        onPage={openPage}
        colorBlind={colorBlind}
        onToggleColorBlind={() => setColorBlind((current) => !current)}
      />
      <MoonGlobeWindow open={globeOpen} onClose={() => setGlobeOpen(false)} />
      <MobileTabs
        activeTab={page === 'dashboard' ? tab : page}
        onChange={(id) => {
          if (id === 'camera' || id === 'photos') return openPage(id)
          openPage('dashboard')
          setTab(id)
        }}
        extraTabs={onRobot ? ROBOT_PAGES.slice(1) : []}
      />

      {page === 'photos' ? (
        <PhotosPage
          available={onRobot && link.capabilities.photos}
          changed={link.photosChanged}
          token={pageToken(token)}
          onBack={() => openPage('dashboard')}
        />
      ) : page === 'camera' ? (
        <CameraPage
          available={onRobot && link.capabilities.camera}
          still={link.cameraStill}
          cameraAge={topicAges?.camera ?? null}
          onOpenPhotos={() => openPage('photos')}
          onBack={() => openPage('dashboard')}
        />
      ) : (
        <main className="workspace" data-tab={tab}>
          <FindingsList
            samples={shown.samples}
            obstacles={shown.obstacles}
            selectedId={currentId}
            onSelect={setSelectedId}
            labels={labels}
          />
          <MissionMap
            area={mapArea}
            world={moonWorld}
            zoneName={shown.mission.name}
            path={shown.path}
            robot={shown.robot}
            samples={shown.samples}
            obstacles={shown.obstacles}
            selectedId={currentId}
            onSelect={setSelectedId}
            scanMap={shown.scanMap}
            unitCm={live?.unitCm}
            labels={labels}
            scope={onRobot ? { view: <LidarScope link={link} scanAge={topicAges?.scan ?? null} />, legend: <LidarLegend /> } : null}
          />
          {selectedItem && <SelectionBar sample={selectedItem} onOpen={openDetails} buttonRef={openButtonRef} />}

          {isDrawerMode && drawerOpen && (
            <div className="drawer-backdrop" onClick={() => setDrawerOpen(false)} aria-hidden="true"></div>
          )}
          {selectedItem ? (
            <SampleInspector
              sample={selectedItem}
              statusOptions={isObstacle ? obstacleStatusOptions : statusOptions}
              queueState={queueState}
              isDrawer={isDrawerMode}
              isOpen={drawerOpen}
              headingRef={inspectorHeadingRef}
              onClose={() => setDrawerOpen(false)}
              onChangeNote={changeNote}
              onChangeStatus={changeStatus}
              onQueue={queueReport}
              labels={labels}
            />
          ) : (
            // A live mission with nothing found yet: an empty Investigation panel instead of a crash
            <aside className={drawerOpen ? 'panel inspector open' : 'panel inspector'} aria-label="Sample inspector">
              <div className="panel-heading">
                <h2 ref={inspectorHeadingRef} tabIndex={-1}>
                  <SectionIcon name="scan" />
                  Investigation
                </h2>
              </div>
              <p className="empty-inspector">{labels.emptyFindings} Points of interest and obstacles appear here.</p>
            </aside>
          )}

          <CameraPanel
            message={labels.camera}
            robot={
              onRobot && link.capabilities.camera
                ? {
                    still: link.cameraStill,
                    cameraAge: topicAges?.camera ?? null,
                    onOpenCamera: () => openPage('camera'),
                    onOpenPhotos: () => openPage('photos'),
                  }
                : null
            }
          />
          {/* The robot has no detector yet, so its activity log would always be empty: the map gets the room instead */}
          {!onRobot && <ActivityStrip samples={shown.samples} obstacles={shown.obstacles} queue={shown.queue} labels={labels} />}
        </main>
      )}

      {/* The remote control, under every page while connected to the robot. It stays the same element whichever page
          is open (it's always in this spot), so switching pages doesn't switch the keyboard driving off. */}
      {onRobot && (
        <DriveBar
          drive={drive}
          canCommand={link.canCommand}
          onToken={saveToken}
          error={driveError}
          link={link}
          topicAges={topicAges}
          motors={link.status?.motors}
        />
      )}

      {/* The page background is a real NASA image. Say so, give credit, and say what it is NOT. */}
      <footer className="photo-credit">
        Background image: {backgroundPhoto.description}, {backgroundPhoto.credit} (
        <a href={backgroundPhoto.sourceUrl} target="_blank" rel="noreferrer">
          {backgroundPhoto.sourceName}
        </a>
        ). Decorative only: not robot footage and not the mapped location.
      </footer>
    </div>
  )
}

export default App
