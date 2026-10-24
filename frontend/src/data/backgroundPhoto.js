// Facts about the NASA Moon map, kept here so the on-screen credits and the README agree.
// It is DECORATION. It is not robot footage, and it does not show the (fictional) mapped location.

// The page background. It is the same map the interactive Moon globe uses.
export const backgroundPhoto = {
  description: 'a map of the whole Moon (LROC color map)',
  credit: "NASA's Scientific Visualization Studio",
  sourceName: 'CGI Moon Kit',
  sourceUrl: 'https://svs.gsfc.nasa.gov/4720',
}

// The picture of lunar ground under the exploration map. It is a real photo, but the mission area drawn on it
// is fictional, so it is NOT a real place and the robot is not really there.
export const mapGround = {
  description: 'Apollo 16 photo of the Moon (far side, near Guyot crater)',
  credit: 'NASA',
  sourceName: 'NASA Image and Video Library, AS16-121-19407',
  sourceUrl: 'https://images.nasa.gov/details/as16-121-19407',
}

// The map that is wrapped around the interactive Moon globe.
export const moonMap = {
  credit: "NASA's Scientific Visualization Studio",
  sourceName: 'CGI Moon Kit (LROC color map)',
  sourceUrl: 'https://svs.gsfc.nasa.gov/4720',
  note: 'Optimized for looks, not for science.',
}
