import { useRef, useEffect } from 'react'
import Globe from './Globe.jsx'
import moonMapUrl from '../assets/moon-map-2k.jpg'
import SectionIcon from './SectionIcon.jsx'
import { moonMap } from '../data/backgroundPhoto.js'
import './MoonGlobeWindow.css'

// A pop-up window holding the Moon globe. It uses the browser's built-in <dialog> element,
// which gives us a dimmed backdrop, the Escape key to close, and keyboard focus kept inside.
function MoonGlobeWindow({ open, onClose }) {
  const dialogRef = useRef(null)

  useEffect(() => {
    const dialog = dialogRef.current
    if (open && !dialog.open) {
      dialog.showModal()
      dialog.querySelector('canvas')?.focus() // so the arrow keys work right away
    }
    if (!open && dialog.open) dialog.close()
  }, [open])

  return (
    <dialog
      ref={dialogRef}
      className="globe-window"
      aria-labelledby="globe-title"
      onClose={onClose}
      onClick={(event) => {
        // A click on the dark area around the window lands on the dialog itself, so close.
        if (event.target === event.currentTarget) onClose()
      }}
    >
      {/* The globe is only built while the window is open, so it never runs in the background. */}
      {open && (
        <div className="globe-body">
          <div className="panel-heading">
            <h2 id="globe-title">
              <SectionIcon name="moon" />
              Moon globe
            </h2>
            <button className="button globe-close" onClick={onClose}>
              Close
            </button>
          </div>

          <Globe mapUrl={moonMapUrl} name="the Moon" />

          <p className="globe-note">
            Decorative reference only. This globe does not show where the robot is, and the fictional mission area
            from the exploration map is not marked on it.
          </p>
          <p className="globe-credit">
            Moon map: {moonMap.sourceName},{' '}
            <a href={moonMap.sourceUrl} target="_blank" rel="noreferrer">
              {moonMap.credit}
            </a>
            . {moonMap.note}
          </p>
        </div>
      )}
    </dialog>
  )
}

export default MoonGlobeWindow
