import './SectionIcon.css'

// Small line icons for section titles. They are drawn with SVG shapes in code, not image files.
const icons = {
  map: (
    <>
      <path d="M3 6l6-2 6 2 6-2v14l-6 2-6-2-6 2z" />
      <path d="M9 4v14M15 6v14" />
    </>
  ),
  camera: (
    <>
      <rect x="3" y="7" width="18" height="12" rx="2" />
      <path d="M8 7l1.5-2h5L16 7" />
      <circle cx="12" cy="13" r="3.2" />
    </>
  ),
  scan: (
    <>
      <path d="M4 8V5a1 1 0 011-1h3M16 4h3a1 1 0 011 1v3M20 16v3a1 1 0 01-1 1h-3M8 20H5a1 1 0 01-1-1v-3" />
      <path d="M4 12h16" />
    </>
  ),
  eye: (
    <>
      <path d="M2 12s4-6.5 10-6.5S22 12 22 12s-4 6.5-10 6.5S2 12 2 12z" />
      <circle cx="12" cy="12" r="2.8" />
    </>
  ),
  moon: (
    <>
      <circle cx="12" cy="12" r="8" />
      <circle cx="9" cy="10" r="1.3" />
      <circle cx="14.5" cy="14.5" r="1.8" />
    </>
  ),
}

function SectionIcon({ name }) {
  return (
    <svg className="section-icon" viewBox="0 0 24 24" aria-hidden="true">
      {icons[name]}
    </svg>
  )
}

export default SectionIcon
