#!/usr/bin/env node

/**
 * OTA Preparation Script for Twenty-Four Times
 * 
 * This script:
 * 1. Ensures the data/ directory exists
 * 2. Copies the pixel firmware binary to data/firmware.bin
 * 3. Reports the firmware size
 * 
 * Usage:
 *   npm run ota:prepare
 *   npm run ota:upload  (prepare + upload to master)
 */

const fs = require('fs');
const path = require('path');

// Paths
const SOURCE_PATH = path.join(__dirname, '..', '.pio', 'build', 'pixel_s3', 'firmware.bin');
const DATA_DIR = path.join(__dirname, '..', 'data');
const DEST_PATH = path.join(DATA_DIR, 'firmware.bin');

console.log('=== OTA Firmware Preparation ===\n');

// Check if source firmware exists
if (!fs.existsSync(SOURCE_PATH)) {
  console.error('Error: Pixel firmware not found!');
  console.error(`Expected location: ${SOURCE_PATH}`);
  console.error('\nPlease build the pixel firmware first:');
  console.error('  pio run -e pixel_s3');
  process.exit(1);
}

// Get firmware info
const stats = fs.statSync(SOURCE_PATH);
const sizeKB = (stats.size / 1024).toFixed(2);

console.log(`Found pixel firmware: ${sizeKB} KB`);

// Create data directory if it doesn't exist
if (!fs.existsSync(DATA_DIR)) {
  console.log('Creating data/ directory...');
  fs.mkdirSync(DATA_DIR, { recursive: true });
}

// Copy firmware
console.log('Copying firmware to data/firmware.bin...');
fs.copyFileSync(SOURCE_PATH, DEST_PATH);

console.log('\n✓ OTA firmware ready!');
console.log(`  Location: ${DEST_PATH}`);
console.log(`  Size: ${sizeKB} KB`);
console.log('\nNext step: Upload to master with:');
console.log('  npm run ota:upload');

