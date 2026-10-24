import { useRef } from 'react'
import './FindingsList.css'
import StatusBadge from './StatusBadge.jsx'
import SectionIcon from './SectionIcon.jsx'
import { labelsFor } from '../live/labels.js'

function FindingsList({ samples, obstacles, selectedId, onSelect, labels = labelsFor('demo') }) {
  const needReview = samples.filter((sample) => sample.status !== 'Reviewed').length
  const reviewed = samples.length - needReview
  const newObstacles = obstacles.filter((obstacle) => obstacle.status === 'New').length
  const obstacleHeading = useRef(null)

  return (
    <section className="panel findings-panel">
      <div className="panel-heading">
        <h2>
          <SectionIcon name="target" />
          Points of interest
        </h2>
        <span className="tag tag-review">{needReview} need review</span>
      </div>
      <p className="panel-note">{labels.findingsNote}</p>

      {/* Real progress, counted from the statuses: one segment per sample, lit when it is marked Reviewed */}
      <div className="progress">
        <span className="field-label">Review progress</span>
        <span className="progress-bar" aria-hidden="true">
          {samples.map((sample) => (
            <span
              key={sample.id}
              className={sample.status === 'Reviewed' ? 'progress-segment done' : 'progress-segment'}
            ></span>
          ))}
        </span>
        <span className="progress-count">
          {reviewed} / {samples.length}
        </span>
      </div>

      {/* A one-line summary that is always visible. Clicking it scrolls the Obstacles list into view. */}
      <button
        className="obstacle-jump"
        onClick={() => obstacleHeading.current?.scrollIntoView({ block: 'start' })}
      >
        <SectionIcon name="warning" />
        <span>{labels.obstaclesJump}</span>
        <span className="obstacle-jump-count">
          {newObstacles} new <span aria-hidden="true">↓</span>
        </span>
      </button>

      {/* Both lists scroll together inside the panel if there is not enough room */}
      <div className="findings-scroll">
        <ul className="findings-list">
          {samples.length === 0 && <li className="findings-empty">{labels.emptyFindings}</li>}
          {samples.map((sample) => (
            <li key={sample.id}>
              <button
                className={sample.id === selectedId ? 'finding selected' : 'finding'}
                onClick={() => onSelect(sample.id)}
                aria-pressed={sample.id === selectedId}
              >
                <span className="finding-top">
                  <span className="finding-id mono">{sample.id}</span>
                  <StatusBadge status={sample.status} />
                </span>
                <span className="finding-title" title={sample.title}>
                  {sample.title}
                </span>
              </button>
            </li>
          ))}
        </ul>

        {/* Obstacles are a separate, shorter list. They are simulated, like everything on this screen. */}
        <h3 ref={obstacleHeading} className="obstacle-heading field-label">
          <SectionIcon name="warning" />
          Obstacles
          <span className="obstacle-sim">{labels.obstaclesBadge}</span>
        </h3>
        <ul className="findings-list">
          {obstacles.length === 0 && <li className="findings-empty">{labels.emptyObstacles}</li>}
          {obstacles.map((obstacle) => (
            <li key={obstacle.id}>
              <button
                className={obstacle.id === selectedId ? 'finding selected' : 'finding'}
                onClick={() => onSelect(obstacle.id)}
                aria-pressed={obstacle.id === selectedId}
              >
                <span className="finding-top">
                  <span className="finding-id mono">{obstacle.id}</span>
                  <StatusBadge status={obstacle.status} />
                </span>
                <span className="finding-title" title={obstacle.title}>
                  {obstacle.title}
                </span>
              </button>
            </li>
          ))}
        </ul>
      </div>
    </section>
  )
}

export default FindingsList
