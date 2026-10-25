import { useState, useEffect } from 'react'
import './App.css'
import MissionHeader from './components/MissionHeader.jsx'
import MobileTabs from './components/MobileTabs.jsx'
import MissionMap from './components/MissionMap.jsx'
import CameraPanel from './components/CameraPanel.jsx'
import CameraPage from './components/CameraPage.jsx'
import PhotosPage from './components/PhotosPage.jsx'
import GpsPage from './components/GpsPage.jsx'
import DriveBar from './components/DriveBar.jsx'
import MoonGlobeWindow from './components/MoonGlobeWindow.jsx'
import { backgroundPhoto } from './data/backgroundPhoto.js'
import { useProwlLink, pageToken } from './live/useProwlLink.js'
import { useDrive } from './live/useDrive.js'
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

// The Camera, Photos and GPS pages have their own addresses (#camera, #photos, #gps), so the browser's Back button
// and links work. Everything else is the map page.
const PAGES = [
  { id: 'map', label: 'Map', icon: 'map' },
  { id: 'camera', label: 'Camera', icon: 'camera' },
  { id: 'photos', label: 'Photos', icon: 'scan' },
  { id: 'gps', label: 'GPS', icon: 'globe' },
]
const pageFromAddress = () => {
  const name = window.location.hash.slice(1)
  return PAGES.some((entry) => entry.id === name) ? name : 'map'
}

// The connection label in the header: what it says, and whether its dot shows "live".
function connectionText(link) {
  if (!link.session) {
    return { text: link.connection === 'offline' ? 'Robot not connected' : 'Connecting to the robot…', live: false }
  }
  if (link.connection === 'online' && link.robotConnected) return { text: 'Robot connected — live', live: true }
  return { text: 'Robot offline — showing last data', live: false }
}

function App() {
  // App owns the data that more than one panel needs. Panels get it as props.
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

  // ---- The robot (see src/live/) ----
  const [token, setToken] = useState(readToken)
  const link = useProwlLink(token)
  const drive = useDrive(link)
  const connected = link.session !== null // the robot has answered at least once
  const [page, setPage] = useState(pageFromAddress)
  const now = useNow()
  const driveError =
    link.lastError?.forType === 'drive' && now - link.lastError.at < DRIVE_ERROR_S ? link.lastError.error : null
  const topicAges = link.status?.topic_age_s
  const elapsedSeconds = connected ? Math.max(0, Math.floor(now - link.session.started_at)) : null

  // The browser's Back and Forward buttons move between the pages.
  useEffect(() => {
    const follow = () => setPage(pageFromAddress())
    window.addEventListener('hashchange', follow)
    window.addEventListener('popstate', follow)
    return () => {
      window.removeEventListener('hashchange', follow)
      window.removeEventListener('popstate', follow)
    }
  }, [])

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
    if (id === 'map') {
      window.history.pushState(null, '', window.location.pathname + window.location.search)
      setPage('map')
    } else {
      window.location.hash = id
    }
  }

  return (
    <div className="app">
      <MissionHeader
        missionName={link.session?.name ?? null}
        elapsedSeconds={elapsedSeconds}
        connection={connectionText(link)}
        onOpenGlobe={() => setGlobeOpen(true)}
        pages={PAGES}
        page={page}
        onPage={openPage}
        colorBlind={colorBlind}
        onToggleColorBlind={() => setColorBlind((current) => !current)}
      />
      <MoonGlobeWindow open={globeOpen} onClose={() => setGlobeOpen(false)} />
      <MobileTabs pages={PAGES} page={page} onPage={openPage} />

      {page === 'gps' ? (
        <GpsPage available={connected} link={link} gpsAge={topicAges?.gps ?? null} onBack={() => openPage('map')} />
      ) : page === 'photos' ? (
        <PhotosPage
          available={connected}
          changed={link.photosChanged}
          token={pageToken(token)}
          onBack={() => openPage('map')}
        />
      ) : page === 'camera' ? (
        <CameraPage
          available={connected}
          still={link.cameraStill}
          cameraAge={topicAges?.camera ?? null}
          onOpenPhotos={() => openPage('photos')}
          onBack={() => openPage('map')}
        />
      ) : (
        <main className="workspace">
          <MissionMap zoneName={link.session?.name ?? null} link={link} scanAge={topicAges?.scan ?? null} />
          <CameraPanel
            robot={
              connected
                ? {
                    still: link.cameraStill,
                    cameraAge: topicAges?.camera ?? null,
                    onOpenCamera: () => openPage('camera'),
                    onOpenPhotos: () => openPage('photos'),
                  }
                : null
            }
          />
        </main>
      )}

      {/* The remote control, under every page once the robot has answered. It stays the same element whichever page
          is open (it's always in this spot), so switching pages doesn't switch the keyboard driving off. */}
      {connected && (
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
