// Keeps every full-resolution photo the camera takes, as JPEG files in one folder, so the dashboard can
// show them all on its photos page and they survive a restart. No ROS in here: index.js hands it each
// new still, and server.js serves the list and the files under /api/camera/photos/.
//
// Each photo is named after the time it was taken (UTC), e.g. prowl-20261024-221120-413.jpg. The camera
// driver re-sends its newest still to every new subscriber, so the same picture arrives again each
// time the bridge starts; the name is the same, so it's only saved once.

import { mkdirSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { unlink, writeFile } from 'node:fs/promises'
import { join } from 'node:path'

const NAME = /^prowl-\d{8}-\d{6}-\d{3}$/

// Width and height from a JPEG's start-of-frame header, or null if it isn't a JPEG.
export function jpegSize(jpeg) {
  if (jpeg.length < 4 || jpeg[0] !== 0xff || jpeg[1] !== 0xd8) return null
  let i = 2
  while (i + 4 <= jpeg.length) {
    if (jpeg[i] !== 0xff) return null
    const marker = jpeg[i + 1]
    if (marker === 0xff) {
      i += 1 // fill byte
      continue
    }
    // SOF0-SOF15 are 0xC0-0xCF, except DHT (C4), JPG (C8) and DAC (CC). After the length come the
    // sample precision (1 byte), then height and width (2 bytes each).
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
      if (i + 9 > jpeg.length) return null
      return { width: jpeg.readUInt16BE(i + 7), height: jpeg.readUInt16BE(i + 5) }
    }
    if (marker === 0xd9 || marker === 0xda) return null // end of image, or image data, with no frame header
    i += 2 + jpeg.readUInt16BE(i + 2)
  }
  return null
}

const round3 = (value) => Math.round(value * 1000) / 1000

// The file name (without .jpg) for a photo taken at t seconds (Unix time).
export function photoName(t) {
  const iso = new Date(Math.round(t * 1000)).toISOString() // 2026-10-24T22:11:20.413Z
  return `prowl-${iso.slice(0, 10).replace(/-/g, '')}-${iso.slice(11, 19).replace(/:/g, '')}-${iso.slice(20, 23)}`
}

// The time a photo was taken, from its name, or null if it isn't one of ours.
function timeFromName(name) {
  if (!NAME.test(name)) return null
  const [, day, clock, ms] = name.split('-')
  const iso = `${day.slice(0, 4)}-${day.slice(4, 6)}-${day.slice(6)}T${clock.slice(0, 2)}:${clock.slice(2, 4)}:${clock.slice(4)}.${ms}Z`
  return Date.parse(iso) / 1000
}

export class PhotoStore {
  // dir: the folder (created if missing). maxPhotos: past this many, the oldest is deleted.
  constructor({ dir, maxPhotos, log }) {
    this.dir = dir
    this.maxPhotos = maxPhotos
    this.log = log
    this.photos = new Map() // name -> {name, t, width, height, bytes}, oldest first
    this.writing = new Map() // name -> jpeg, while its file is still being written
    mkdirSync(dir, { recursive: true })
    const found = []
    for (const file of readdirSync(dir)) {
      const name = file.endsWith('.jpg') ? file.slice(0, -4) : null
      const t = name && timeFromName(name)
      if (t === null || !name) continue
      const path = join(dir, file)
      const size = jpegSize(readFileSync(path))
      found.push({ name, t, width: size?.width ?? null, height: size?.height ?? null, bytes: statSync(path).size })
    }
    found.sort((a, b) => a.t - b.t).forEach((photo) => this.photos.set(photo.name, photo))
  }

  get count() {
    return this.photos.size
  }

  // Saves a new still. Returns its entry (see list()), straight away; the file is written in the background.
  add(jpeg, t) {
    const name = photoName(t)
    if (this.photos.has(name)) return this.entry(this.photos.get(name))
    const size = jpegSize(jpeg)
    const photo = { name, t: round3(t), width: size?.width ?? null, height: size?.height ?? null, bytes: jpeg.length }
    this.photos.set(name, photo)
    this.writing.set(name, jpeg)
    writeFile(this.path(name), jpeg)
      .catch((err) => {
        this.log.error(`couldn't save photo ${name}: ${err.message}`)
        this.photos.delete(name)
      })
      .finally(() => this.writing.delete(name))
    while (this.photos.size > this.maxPhotos) {
      const oldest = this.photos.keys().next().value
      this.log.warn(`more than max_photos (${this.maxPhotos}) photos: deleting the oldest, ${oldest}`)
      this.remove(oldest)
    }
    return this.entry(photo)
  }

  // The saved photo taken at t, or null (never saved, or deleted since).
  find(t) {
    const photo = this.photos.get(photoName(t))
    return photo ? this.entry(photo) : null
  }

  // Every photo, newest first: [{name, t, width, height, bytes, url}]
  list() {
    return [...this.photos.values()].reverse().map((photo) => this.entry(photo))
  }

  // The JPEG of one photo, or null if there's no such photo.
  read(name) {
    if (!this.photos.has(name)) return null
    return this.writing.get(name) ?? readFileSync(this.path(name))
  }

  // Deletes one photo. Returns false if there was no such photo.
  remove(name) {
    if (!this.photos.has(name)) return false
    this.photos.delete(name)
    unlink(this.path(name)).catch((err) => {
      if (err.code !== 'ENOENT') this.log.warn(`couldn't delete photo ${name}: ${err.message}`)
    })
    return true
  }

  path(name) {
    return join(this.dir, `${name}.jpg`)
  }

  entry(photo) {
    return { ...photo, url: `/api/camera/photos/${photo.name}.jpg` }
  }
}
