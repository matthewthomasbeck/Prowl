import { test } from 'node:test'
import assert from 'node:assert/strict'
import { crc32, inflateSync } from 'node:zlib'
import { encodeGrayPng } from '../bridge/png.js'

// Splits a PNG into its chunks, checking each chunk's CRC on the way.
function readChunks(png) {
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
  const chunks = []
  let at = 8
  while (at < png.length) {
    const length = png.readUInt32BE(at)
    const type = png.toString('ascii', at + 4, at + 8)
    const data = png.subarray(at + 8, at + 8 + length)
    assert.equal(png.readUInt32BE(at + 8 + length), crc32(png.subarray(at + 4, at + 8 + length)), `${type} CRC`)
    chunks.push({ type, data })
    at += 12 + length
  }
  return chunks
}

test('writes IHDR, IDAT and IEND with valid CRCs', () => {
  const chunks = readChunks(encodeGrayPng(3, 2, new Uint8Array(6)))
  assert.deepEqual(chunks.map((c) => c.type), ['IHDR', 'IDAT', 'IEND'])
  const header = chunks[0].data
  assert.equal(header.readUInt32BE(0), 3)
  assert.equal(header.readUInt32BE(4), 2)
  assert.deepEqual([...header.subarray(8)], [8, 0, 0, 0, 0])
  assert.equal(chunks[2].data.length, 0)
})

test('stores the rows top to bottom, each after a filter byte of 0', () => {
  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6])
  const idat = readChunks(encodeGrayPng(3, 2, pixels))[1].data
  assert.deepEqual([...inflateSync(idat)], [0, 1, 2, 3, 0, 4, 5, 6])
})

test('rejects a pixel count that does not match the size', () => {
  assert.throws(() => encodeGrayPng(3, 2, new Uint8Array(5)), /bad image size/)
  assert.throws(() => encodeGrayPng(0, 0, new Uint8Array(0)), /bad image size/)
})
