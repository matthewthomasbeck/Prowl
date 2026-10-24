import './MissionHeader.css'
import SectionIcon from './SectionIcon.jsx'

function MissionHeader({ mission, elapsedText, onOpenGlobe, colorBlind, onToggleColorBlind }) {
  return (
    <header className="topbar">
      <div className="brand">
        <span className="brand-mark" aria-hidden="true"></span>
        <h1>
          Prowl <span className="divider">|</span> Material Reconnaissance
        </h1>
      </div>

      {/* Small label above a larger value, with thin lines between them */}
      <div className="topbar-info">
        <p className="segment">
          <span className="segment-label">Mission</span>
          <span className="segment-value">{mission.name}</span>
        </p>
        <p className="segment">
          <span className="segment-label">Simulated mission time</span>
          <span className="segment-value mono">{elapsedText}</span>
        </p>
        <button className="button header-button" onClick={onOpenGlobe}>
          <SectionIcon name="moon" />
          Moon globe
        </button>
        {/* aria-pressed tells screen readers this is an on/off switch. The words ON and OFF mean it never relies on color. */}
        <button className="button header-button" onClick={onToggleColorBlind} aria-pressed={colorBlind}>
          <SectionIcon name="eye" />
          Color-blind mode
          <span className={colorBlind ? 'switch-state on' : 'switch-state'}>{colorBlind ? 'On' : 'Off'}</span>
        </button>
        <p className="sim-label">
          <span className="sim-icon" aria-hidden="true"></span>
          Simulation — robot not connected
        </p>
      </div>
    </header>
  )
}

export default MissionHeader
