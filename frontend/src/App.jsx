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
import MoonGlobeWindow from './components/MoonGlobeWindow.jsx'
import SectionIcon from './components/SectionIcon.jsx'
import { backgroundPhoto } from './data/backgroundPhoto.js'
import { useProwlLink } from './live/useProwlLink.js'
import { buildLiveMission } from './live/liveMission.js'
import { labelsFor, connectionText } from './live/labels.js'
import { useNow } from './live/useNow.js'

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

  // ---- Live data from the Prowl backend (see src/live/) ----
  // Once a robot or the simulator has connected, everything on screen comes from it instead of the demo data.
  const link = useProwlLink()
  const [noteDrafts, setNoteDrafts] = useState({}) // live notes still being typed on this page, by finding id
  const noteTimers = useRef({})
  const live = link.session ? buildLiveMission(link, noteDrafts) : null
  const labels = labelsFor(live ? live.mode : 'demo')
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
        colorBlind={colorBlind}
        onToggleColorBlind={() => setColorBlind((current) => !current)}
      />
      <MoonGlobeWindow open={globeOpen} onClose={() => setGlobeOpen(false)} />
      <MobileTabs activeTab={tab} onChange={setTab} />

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

        <CameraPanel message={labels.camera} />
        <ActivityStrip samples={shown.samples} obstacles={shown.obstacles} queue={shown.queue} labels={labels} />
      </main>

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
