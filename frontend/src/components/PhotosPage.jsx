import { useEffect, useRef, useState } from 'react'
import './PhotosPage.css'
import SectionIcon from './SectionIcon.jsx'

// ---- Mission photos: every full-resolution photo the robot's camera has taken ----
// The photos are kept on the robot (its dashboard bridge saves each one), so every dashboard sees the same list, and
// they are still there after a reload or a restart. The list comes from GET /api/camera/photos and is
// loaded again whenever a photo is taken or deleted (changed goes up).
//   available: is the dashboard connected to the robot? changed: from useProwlLink (photosChanged).
//   token: the drive token, which deleting needs if the robot has one. onBack(): back to the dashboard.
// Pictures can show people near the robot, so each one can be deleted.

function when(t) {
  return new Date(t * 1000).toLocaleString([], {
    day: 'numeric',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  })
}

function megabytes(bytes) {
  return `${(bytes / 1e6).toFixed(1)} MB`
}

function PhotosPage({ available, changed, token, onBack }) {
  const [photos, setPhotos] = useState(null) // null while loading
  const [problem, setProblem] = useState(null)
  const [openName, setOpenName] = useState(null) // the photo shown in the viewer
  const [reload, setReload] = useState(0)

  useEffect(() => {
    if (!available) return
    let cancelled = false
    fetch('/api/camera/photos')
      .then((response) => response.json())
      .then((answer) => {
        if (cancelled) return
        if (!answer.ok) throw new Error(answer.error || 'the robot said no')
        setPhotos(answer.photos)
        setProblem(null)
      })
      .catch((error) => {
        if (!cancelled) setProblem(`Couldn't load the photos: ${error.message}.`)
      })
    return () => {
      cancelled = true
    }
  }, [available, changed, reload])

  const list = photos || []
  const index = list.findIndex((photo) => photo.name === openName)
  const open = index >= 0 ? list[index] : null

  async function remove(photo) {
    const headers = token ? { Authorization: `Bearer ${token}` } : {}
    const response = await fetch(photo.url, { method: 'DELETE', headers }).catch(() => null)
    const answer = response ? await response.json().catch(() => ({})) : {}
    if (!answer.ok) return answer.error || 'the robot could not be reached'
    // The next photo along takes its place in the viewer, or the viewer closes after the last one
    const next = list[index + 1] || list[index - 1] || null
    setOpenName(next ? next.name : null)
    setReload((count) => count + 1)
    return null
  }

  let body
  if (!available) {
    body = <p className="photos-empty">Photos are kept on the robot. They appear here when the dashboard is connected to it.</p>
  } else if (problem) {
    body = <p className="photos-empty photos-problem">⚠ {problem}</p>
  } else if (!photos) {
    body = <p className="photos-empty">Loading the photos…</p>
  } else if (list.length === 0) {
    body = <p className="photos-empty">No photos yet. Take one with “Take photo” on the Camera page.</p>
  } else {
    body = (
      <ul className="photo-grid">
        {list.map((photo) => (
          <li key={photo.name}>
            <button className="photo-tile" onClick={() => setOpenName(photo.name)} aria-label={`Photo taken ${when(photo.t)}`}>
              <img src={photo.url} alt="" loading="lazy" decoding="async" />
              <span className="photo-caption">
                <span className="mono">{when(photo.t)}</span>
              </span>
            </button>
          </li>
        ))}
      </ul>
    )
  }

  return (
    <main className="photos-page">
      <section className="panel photos-panel">
        <div className="panel-heading">
          <h2>
            <SectionIcon name="camera" />
            Mission photos
          </h2>
          <span className="tag">{photos ? `Robot camera · ${list.length} ${list.length === 1 ? 'photo' : 'photos'}` : 'Robot camera'}</span>
          <button className="button photos-back" onClick={onBack}>
            Back to the map
          </button>
        </div>
        <p className="panel-note">
          Full-resolution photos from the robot's camera, kept on the robot, newest first. They are what the camera saw,
          not detections: nothing in them has been identified. Photos can show people near the robot, so delete the ones
          you don't need.
        </p>
        {body}
      </section>

      <PhotoViewer
        photo={open}
        position={index + 1}
        count={list.length}
        onClose={() => setOpenName(null)}
        onStep={(step) => {
          const next = list[index + step]
          if (next) setOpenName(next.name)
        }}
        onDelete={remove}
      />
    </main>
  )
}

// One photo, big, in the browser's built-in <dialog> window (like the Moon globe). Arrow keys step through them.
function PhotoViewer({ photo, position, count, onClose, onStep, onDelete }) {
  const dialogRef = useRef(null)
  const [confirming, setConfirming] = useState(false)
  const [problem, setProblem] = useState(null)
  // A different photo starts with no question and no error (adjusting state while rendering, like the map's fly-to)
  const [seenName, setSeenName] = useState(photo?.name)
  if (photo?.name !== seenName) {
    setSeenName(photo?.name)
    setConfirming(false)
    setProblem(null)
  }

  useEffect(() => {
    const dialog = dialogRef.current
    if (photo && !dialog.open) dialog.showModal()
    if (!photo && dialog.open) dialog.close()
  }, [photo])

  function handleKey(event) {
    if (event.key === 'ArrowLeft') onStep(-1)
    else if (event.key === 'ArrowRight') onStep(1)
  }

  return (
    <dialog
      ref={dialogRef}
      className="photo-window"
      aria-labelledby="photo-title"
      onClose={onClose}
      onKeyDown={handleKey}
      onClick={(event) => {
        if (event.target === event.currentTarget) onClose()
      }}
    >
      {photo && (
        <div className="photo-body">
          <div className="panel-heading">
            <h2 id="photo-title">
              <SectionIcon name="camera" />
              Photo {position} of {count}
            </h2>
            <button className="button photo-close" onClick={onClose}>
              Close
            </button>
          </div>

          <div className="photo-frame">
            <img src={photo.url} alt={`Photo from the robot's camera, taken ${when(photo.t)}`} />
          </div>

          <dl className="photo-meta">
            <div>
              <dt className="field-label">Taken</dt>
              <dd className="mono">{when(photo.t)}</dd>
            </div>
            <div>
              <dt className="field-label">Size</dt>
              <dd className="mono">
                {photo.width} × {photo.height} · {megabytes(photo.bytes)}
              </dd>
            </div>
          </dl>

          <div className="photo-actions">
            <button className="button" onClick={() => onStep(-1)} disabled={position <= 1} aria-label="Newer photo">
              ← Newer
            </button>
            <button className="button" onClick={() => onStep(1)} disabled={position >= count} aria-label="Older photo">
              Older →
            </button>
            <a className="button" href={photo.url} target="_blank" rel="noreferrer">
              Full size
            </a>
            <a className="button" href={photo.url} download={`${photo.name}.jpg`}>
              Download
            </a>
            {confirming ? (
              <span className="photo-confirm" role="group" aria-label="Delete this photo?">
                Delete for everyone?
                <button
                  className="button photo-delete"
                  onClick={async () => {
                    const error = await onDelete(photo)
                    setConfirming(false)
                    setProblem(error)
                  }}
                >
                  Yes, delete
                </button>
                <button className="button" onClick={() => setConfirming(false)}>
                  No
                </button>
              </span>
            ) : (
              <button className="button photo-delete" onClick={() => setConfirming(true)}>
                Delete
              </button>
            )}
          </div>
          {problem && (
            <p className="photos-problem" role="status">
              ⚠ Not deleted: {problem}.
            </p>
          )}
        </div>
      )}
    </dialog>
  )
}

export default PhotosPage
