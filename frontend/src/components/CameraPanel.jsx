import './CameraPanel.css'
import SectionIcon from './SectionIcon.jsx'

// Before the robot answers: a small empty "screen" and a message. There is no video.
// Connected to the robot (the robot prop): a button to the Camera page, where the live video is, and the newest
// photo, which opens the Photos page. The video isn't played here, so the map page stays light.
//   robot: { still, cameraAge, onOpenCamera, onOpenPhotos } or null
//     still: the newest photo, from useProwlLink (cameraStill). cameraAge: seconds since the last video frame
//     (null = the camera driver never sent one).

const STALE_S = 2

function CameraPanel({ robot = null }) {
  if (!robot) {
    return (
      <section className="panel camera-panel">
        {/* A small empty "screen" with a camera icon drawn in SVG (not an image file) */}
        <div className="camera-screen" aria-hidden="true">
          <CameraIcon />
        </div>

        <div className="camera-text">
          <h2 className="field-label">
            <SectionIcon name="camera" />
            Rover camera
          </h2>
          <p className="camera-message">Camera offline — awaiting robot connection.</p>
        </div>
      </section>
    )
  }

  const { still, cameraAge, onOpenCamera, onOpenPhotos } = robot
  const never = cameraAge === null || cameraAge === undefined
  const stale = !never && cameraAge > STALE_S
  let state = 'Video on the Camera page.'
  if (never) state = 'Camera offline: the camera driver is not running.'
  else if (stale) state = `⚠ Video stalled: no frame for ${Math.round(cameraAge)} s.`
  const stillTime = still ? new Date(still.t * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) : null

  return (
    <section className="panel camera-panel camera-robot">
      <div className="camera-row">
        <button className="camera-screen camera-open" onClick={onOpenCamera} aria-label="Open the Camera page">
          <CameraIcon />
        </button>
        <div className="camera-text">
          <h2 className="field-label">
            <SectionIcon name="camera" />
            Rover camera
          </h2>
          <p className="camera-message">{state}</p>
        </div>
      </div>
      <div className="camera-actions">
        <button className="button" onClick={onOpenCamera}>
          Open camera
        </button>
        {still && (
          <button className="camera-thumb" onClick={onOpenPhotos} aria-label={`Newest photo, taken at ${stillTime}. Open the Photos page`}>
            <img src={still.photo?.url ?? still.url} alt="" decoding="async" />
            <span className="camera-thumb-text">
              <span className="field-label">Newest photo</span>
              <span className="mono">{stillTime}</span>
            </span>
          </button>
        )}
      </div>
    </section>
  )
}

function CameraIcon() {
  return (
    <svg className="camera-icon" viewBox="0 0 24 24" aria-hidden="true">
      <rect x="3" y="7" width="18" height="12" rx="2" />
      <path d="M8 7l1.5-2h5L16 7" />
      <circle cx="12" cy="13" r="3.2" />
    </svg>
  )
}

export default CameraPanel
