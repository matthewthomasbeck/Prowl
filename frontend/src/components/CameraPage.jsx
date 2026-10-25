import { useEffect, useRef, useState } from 'react'
import './CameraPage.css'
import SectionIcon from './SectionIcon.jsx'

// ---- The Camera page: the robot's live camera, and taking full-resolution photos ----
// Robot only (the dashboard bridge). It's a page of its own, so the video only plays while someone is looking at it,
// and the map page doesn't have to draw it too.
//   available: is the dashboard connected to the robot with a camera?
//   still: the newest photo, from useProwlLink (cameraStill), whoever took it.
//   cameraAge: seconds since the robot's last video frame (null = the camera driver never sent one).
//   onOpenPhotos(): open the Photos page. onBack(): back to the map page.
//
// The video is pulled one frame at a time from /api/camera/frame.jpg: the next one is asked for only once the last
// one is on screen. The bridge also has an MJPEG stream (/api/camera/video.mjpg), but a stream keeps sending
// whether or not the browser keeps up, so on a slow link, a busy computer or through the dashboard's web server
// the frames pile up on the way and the picture falls seconds behind. Pulling never gets more than one frame behind.

const FRAME_URL = '/api/camera/frame.jpg'
const FRAME_MS = 1000 / 15 // at most 15 frames a second, like the robot's own stream
const RETRY_MS = 1000 // after a failed frame (no video yet, or the robot can't be reached)
const STALE_S = 2

// Shows the robot's video on a canvas and calls onRate(frames per second) once a second.
function LiveVideo({ onRate }) {
  const canvasRef = useRef(null)
  const onRateRef = useRef(onRate)
  useEffect(() => {
    onRateRef.current = onRate
  })

  useEffect(() => {
    const canvas = canvasRef.current
    // "bitmaprenderer" shows a decoded picture without copying it; the 2D canvas is the fallback
    const renderer = canvas.getContext('bitmaprenderer')
    const context = renderer ? null : canvas.getContext('2d')
    let stopped = false
    let fetching = false // a frame is on its way, or the next one is waiting for its turn
    let timer = null
    let controller = null
    let pending = null // a frame that arrived while the last one was still being decoded
    let decoding = false
    let frames = 0
    let counted = performance.now()

    // Decodes and shows frames one at a time. If several arrive meanwhile, only the newest is shown.
    async function show(blob) {
      pending = blob
      if (decoding) return
      decoding = true
      while (pending && !stopped) {
        const next = pending
        pending = null
        try {
          // Decoded away from the page's main thread, so the controls stay responsive
          const bitmap = await createImageBitmap(next)
          if (stopped) {
            bitmap.close()
            break
          }
          if (canvas.width !== bitmap.width || canvas.height !== bitmap.height) {
            canvas.width = bitmap.width
            canvas.height = bitmap.height
          }
          if (renderer) {
            renderer.transferFromImageBitmap(bitmap)
          } else {
            context.drawImage(bitmap, 0, 0)
            bitmap.close()
          }
          frames++
        } catch {
          // not a picture (e.g. cut off): skip it
        }
      }
      decoding = false
      const now = performance.now()
      if (now - counted >= 1000) {
        onRateRef.current?.(Math.round((frames * 1000) / (now - counted)))
        frames = 0
        counted = now
      }
    }

    // Asks for the next frame as soon as the last one has arrived (not once it's on screen), so the robot's
    // answer and the decoding overlap; at most FRAME_MS apart.
    async function fetchFrame() {
      timer = null
      if (stopped || document.hidden) {
        fetching = false // a hidden tab asks for nothing; it starts again when shown
        return
      }
      fetching = true
      const started = performance.now()
      let wait = RETRY_MS
      try {
        controller = new AbortController()
        const response = await fetch(FRAME_URL, { cache: 'no-store', signal: controller.signal })
        if (response.ok) {
          const blob = await response.blob()
          if (stopped) return
          show(blob)
          wait = Math.max(0, FRAME_MS - (performance.now() - started))
        }
      } catch {
        if (stopped) return
      }
      timer = setTimeout(fetchFrame, wait)
    }

    function onVisibility() {
      if (!document.hidden && !fetching) fetchFrame()
    }

    fetchFrame()
    document.addEventListener('visibilitychange', onVisibility)
    return () => {
      stopped = true
      clearTimeout(timer)
      controller?.abort()
      document.removeEventListener('visibilitychange', onVisibility)
    }
  }, [])

  return <canvas ref={canvasRef} className="camera-canvas" role="img" aria-label="The robot's live camera" />
}

function CameraPage({ available, still, cameraAge, onOpenPhotos, onBack }) {
  const [capture, setCapture] = useState({ state: 'idle', text: '' }) // 'idle', 'busy', 'done' or 'failed'
  const [rate, setRate] = useState(null) // frames a second actually shown here

  async function takePhoto() {
    setCapture({ state: 'busy', text: 'Taking a photo… the video pauses for a moment.' })
    try {
      const response = await fetch('/api/camera/capture', { method: 'POST' })
      const answer = await response.json().catch(() => ({}))
      if (answer.ok) setCapture({ state: 'done', text: '✓ Photo saved on the robot.' })
      else if (response.status === 409) setCapture({ state: 'failed', text: 'Someone else is taking a photo. Try again in a moment.' })
      else setCapture({ state: 'failed', text: answer.error ? `No photo: ${answer.error}.` : 'No photo: the camera did not answer.' })
    } catch {
      setCapture({ state: 'failed', text: 'No photo: the robot could not be reached.' })
    }
  }

  if (!available) {
    return (
      <main className="camera-page">
        <section className="panel camera-view">
          <div className="panel-heading">
            <h2>
              <SectionIcon name="camera" />
              Rover camera
            </h2>
            <button className="button camera-back" onClick={onBack}>
              Back to the map
            </button>
          </div>
          <p className="camera-page-empty">The live camera appears here when the dashboard is connected to the robot.</p>
        </section>
      </main>
    )
  }

  const never = cameraAge === null || cameraAge === undefined
  const stale = !never && cameraAge > STALE_S
  let tag = '● Live'
  if (never) tag = 'Offline'
  else if (stale) tag = '⚠ Stalled'
  const stillTime = still ? new Date(still.t * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) : null

  return (
    <main className="camera-page">
      <section className="panel camera-view">
        <div className="panel-heading">
          <h2>
            <SectionIcon name="camera" />
            Rover camera
          </h2>
          {rate !== null && !never && <span className="camera-rate mono">{rate} fps</span>}
          <span className={never || stale ? 'tag' : 'tag camera-tag-live'}>{tag}</span>
        </div>
        <div className="camera-screen-live">
          <LiveVideo onRate={setRate} />
          <span className="map-corner corner-tl" aria-hidden="true"></span>
          <span className="map-corner corner-tr" aria-hidden="true"></span>
          <span className="map-corner corner-bl" aria-hidden="true"></span>
          <span className="map-corner corner-br" aria-hidden="true"></span>
          {(never || stale) && (
            <p className="camera-overlay">
              {never ? 'Camera offline: the camera driver is not running.' : `Video stalled: no frame for ${Math.round(cameraAge)} s.`}
            </p>
          )}
        </div>
      </section>

      <section className="panel camera-side">
        <div className="panel-heading">
          <h2>
            <SectionIcon name="scan" />
            Photos
          </h2>
        </div>
        <button className="button button-primary camera-take" onClick={takePhoto} disabled={capture.state === 'busy' || never}>
          {capture.state === 'busy' ? 'Taking photo…' : 'Take photo'}
        </button>
        <p className={`camera-capture camera-capture-${capture.state}`} role="status">
          {capture.text || 'Full resolution (4608 × 2592), kept on the robot. The video pauses for about a second.'}
        </p>

        <p className="field-label">Newest photo</p>
        {still ? (
          <button className="camera-newest" onClick={onOpenPhotos} aria-label={`Newest photo, taken at ${stillTime}. Open the Photos page`}>
            <img src={still.photo?.url ?? still.url} alt="" decoding="async" />
            <span className="mono">{stillTime}</span>
          </button>
        ) : (
          <p className="camera-capture">None yet.</p>
        )}
        <button className="button" onClick={onOpenPhotos}>
          All photos
        </button>
      </section>
    </main>
  )
}

export default CameraPage
