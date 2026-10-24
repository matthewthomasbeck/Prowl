import { useState, useEffect } from 'react'

// Answers "does the screen match this CSS media query right now?" and updates if the window is resized.
// Example: useMediaQuery('(min-width: 720px)') is true on tablets and bigger.
export function useMediaQuery(query) {
  const [matches, setMatches] = useState(() => window.matchMedia(query).matches)

  useEffect(() => {
    const list = window.matchMedia(query)
    const update = (event) => setMatches(event.matches)
    list.addEventListener('change', update)
    return () => list.removeEventListener('change', update)
  }, [query])

  return matches
}
