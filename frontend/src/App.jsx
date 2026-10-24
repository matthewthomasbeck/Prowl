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
import { backgroundPhoto } from './data/backgroundPhoto.js'

function App() {
  // App owns the data that more than one panel needs. Panels get it as props.
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

  const elapsedSeconds = useMissionClock(mission.elapsedStartSeconds)
  const isDrawerMode = useMediaQuery('(min-width: 720px) and (max-width: 1099px)')

  // A sample and an obstacle are both "items you can select". The selection can be either kind.
  const items = [...samples, ...obstacles]
  const selectedItem = items.find((item) => item.id === selectedId)
  const isObstacle = selectedItem.kind === 'obstacle'

  // Has this item's report been queued, and has it changed since?
  const latestQueued = [...queue].reverse().find((entry) => entry.sampleId === selectedId)
  let queueState = 'none'
  if (latestQueued) {
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
    updateItem(id, { note })
  }

  function changeStatus(id, status) {
    updateItem(id, { status })
  }

  // Local demo queue only. This adds a record to a list on this page; nothing is sent.
  function queueReport(id) {
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
        mission={mission}
        elapsedText={formatElapsed(elapsedSeconds)}
        onOpenGlobe={() => setGlobeOpen(true)}
        colorBlind={colorBlind}
        onToggleColorBlind={() => setColorBlind((current) => !current)}
      />
      <MoonGlobeWindow open={globeOpen} onClose={() => setGlobeOpen(false)} />
      <MobileTabs activeTab={tab} onChange={setTab} />

      <main className="workspace" data-tab={tab}>
        <FindingsList
          samples={samples}
          obstacles={obstacles}
          selectedId={selectedId}
          onSelect={setSelectedId}
        />
        <MissionMap
          area={mapArea}
          world={moonWorld}
          zoneName={mission.name}
          path={travelPath}
          robot={robot}
          samples={samples}
          obstacles={obstacles}
          selectedId={selectedId}
          onSelect={setSelectedId}
        />
        <SelectionBar sample={selectedItem} onOpen={openDetails} buttonRef={openButtonRef} />

        {isDrawerMode && drawerOpen && (
          <div className="drawer-backdrop" onClick={() => setDrawerOpen(false)} aria-hidden="true"></div>
        )}
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
        />

        <CameraPanel />
        <ActivityStrip samples={samples} obstacles={obstacles} queue={queue} />
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
