import './StatusBadge.css'

// Each status has its own color class and its own small symbol,
// so it can still be told apart without relying on color.
const statusInfo = {
  'Needs analysis': { className: 'status-needs', symbol: '○' },
  'Prioritize sampling': { className: 'status-prioritize', symbol: '▲' },
  Reviewed: { className: 'status-reviewed', symbol: '✓' },
  // statuses for obstacles
  New: { className: 'status-needs', symbol: '!' },
  Noted: { className: 'status-reviewed', symbol: '•' },
  Cleared: { className: 'status-reviewed', symbol: '✓' },
}

function StatusBadge({ status }) {
  const info = statusInfo[status]
  return (
    <span className={`status-badge ${info.className}`}>
      <span aria-hidden="true">{info.symbol}</span>
      {status}
    </span>
  )
}

export default StatusBadge
