import './SelectionBar.css'
import StatusBadge from './StatusBadge.jsx'

// Shown on iPad and phone, where the sample details are not always on screen.
// "Open details" opens the drawer on iPad, or switches to the Review tab on a phone.
function SelectionBar({ sample, onOpen, buttonRef }) {
  return (
    <div className="selection-bar">
      <div className="selection-info">
        <span className="field-label">Selected</span>
        <span className="mono selection-id">{sample.id}</span>
        <span className="selection-title">{sample.title}</span>
        <StatusBadge status={sample.status} />
      </div>
      <button ref={buttonRef} className="button" onClick={onOpen}>
        Open details
      </button>
    </div>
  )
}

export default SelectionBar
