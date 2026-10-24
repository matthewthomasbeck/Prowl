import './CameraPanel.css'
import SectionIcon from './SectionIcon.jsx'

function CameraPanel() {
  return (
    <section className="panel camera-panel">
      {/* A small empty "screen" with a camera icon drawn in SVG (not an image file) */}
      <div className="camera-screen" aria-hidden="true">
        <svg className="camera-icon" viewBox="0 0 24 24">
          <rect x="3" y="7" width="18" height="12" rx="2" />
          <path d="M8 7l1.5-2h5L16 7" />
          <circle cx="12" cy="13" r="3.2" />
        </svg>
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

export default CameraPanel
