#!/usr/bin/env node

/**
 * Version Bump Script for Twenty-Four Times Firmware
 * 
 * Usage:
 *   npm run version:bump [major|minor]
 * 
 * Examples:
 *   npm run version:bump minor  -> 1.0 becomes 1.1
 *   npm run version:bump major  -> 1.0 becomes 2.0
 */

const fs = require('fs');
const path = require('path');

// Files to update
const FILES = [
  'src/main.cpp',
  'src/master.cpp'
];

// Get bump type from command line (default to minor)
const bumpType = process.argv[2] || 'minor';

if (!['major', 'minor'].includes(bumpType)) {
  console.error('Error: Bump type must be "major" or "minor"');
  process.exit(1);
}

// Read current version from main.cpp
function getCurrentVersion(filePath) {
  const content = fs.readFileSync(filePath, 'utf8');
  const majorMatch = content.match(/#define FIRMWARE_VERSION_MAJOR (\d+)/);
  const minorMatch = content.match(/#define FIRMWARE_VERSION_MINOR (\d+)/);
  
  if (!majorMatch || !minorMatch) {
    throw new Error(`Could not find version in ${filePath}`);
  }
  
  return {
    major: parseInt(majorMatch[1]),
    minor: parseInt(minorMatch[1])
  };
}

// Calculate new version
function bumpVersion(current, type) {
  if (type === 'major') {
    return { major: current.major + 1, minor: 0 };
  } else {
    return { major: current.major, minor: current.minor + 1 };
  }
}

// Update version in a file
function updateVersionInFile(filePath, newVersion) {
  let content = fs.readFileSync(filePath, 'utf8');
  
  content = content.replace(
    /#define FIRMWARE_VERSION_MAJOR \d+/,
    `#define FIRMWARE_VERSION_MAJOR ${newVersion.major}`
  );
  
  content = content.replace(
    /#define FIRMWARE_VERSION_MINOR \d+/,
    `#define FIRMWARE_VERSION_MINOR ${newVersion.minor}`
  );
  
  fs.writeFileSync(filePath, content, 'utf8');
}

// Main
try {
  const mainCppPath = path.join(__dirname, '..', FILES[0]);
  const currentVersion = getCurrentVersion(mainCppPath);
  const newVersion = bumpVersion(currentVersion, bumpType);
  
  console.log(`Bumping ${bumpType} version: ${currentVersion.major}.${currentVersion.minor} -> ${newVersion.major}.${newVersion.minor}`);
  
  // Update all files
  FILES.forEach(file => {
    const filePath = path.join(__dirname, '..', file);
    updateVersionInFile(filePath, newVersion);
    console.log(`  Updated ${file}`);
  });
  
  console.log('\nVersion bump complete!');
  console.log(`New version: ${newVersion.major}.${newVersion.minor}`);
  
} catch (error) {
  console.error('Error:', error.message);
  process.exit(1);
}

