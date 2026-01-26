const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');

const FIRMWARE_PATH = path.join(__dirname, '../.pio/build/pixel_s3/firmware.bin');
const PORT = 3000;

// ANSI color codes for terminal
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  cyan: '\x1b[36m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m'
};

// Get local IP address on the WiFi interface
function getLocalIP() {
  const interfaces = os.networkInterfaces();
  const ips = [];

  for (let [name, iface] of Object.entries(interfaces)) {
    for (let alias of iface) {
      if (alias.family === 'IPv4' && !alias.internal) {
        ips.push({ name, ip: alias.address });
      }
    }
  }

  return ips;
}

// Track active connections
const activeConnections = new Set();
let totalServed = 0;

const server = http.createServer((req, res) => {
  if (req.url === '/firmware.bin') {
    // Check if firmware exists
    if (!fs.existsSync(FIRMWARE_PATH)) {
      console.log(`${colors.yellow}⚠️  Firmware not found: ${FIRMWARE_PATH}${colors.reset}`);
      res.writeHead(404);
      res.end('Firmware not found');
      return;
    }

    const stat = fs.statSync(FIRMWARE_PATH);
    const clientIP = req.socket.remoteAddress;
    const connectionId = `${clientIP}:${req.socket.remotePort}`;

    activeConnections.add(connectionId);
    totalServed++;

    console.log(`${colors.green}📥 [${totalServed}] Download started: ${clientIP}${colors.reset}`);
    console.log(`   Active connections: ${activeConnections.size}`);

    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': stat.size,
      'Connection': 'close'
    });

    const stream = fs.createReadStream(FIRMWARE_PATH);
    stream.pipe(res);

    stream.on('end', () => {
      activeConnections.delete(connectionId);
      console.log(`${colors.cyan}✅ [${totalServed}] Download complete: ${clientIP}${colors.reset}`);
      console.log(`   Active connections: ${activeConnections.size}`);
    });

    stream.on('error', (err) => {
      activeConnections.delete(connectionId);
      console.log(`${colors.yellow}❌ [${totalServed}] Download failed: ${clientIP} - ${err.message}${colors.reset}`);
    });

  } else {
    res.writeHead(404);
    res.end('Not Found');
  }
});

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.log(`${colors.yellow}\n❌ Port ${PORT} is already in use!${colors.reset}`);
    console.log(`   Try: netstat -ano | findstr :${PORT}`);
    process.exit(1);
  } else {
    console.error(`${colors.yellow}Server error: ${err.message}${colors.reset}`);
  }
});

server.listen(PORT, () => {
  console.clear();
  console.log(`${colors.bright}${colors.magenta}`);
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║         Twenty-Four Times OTA Server Running! 🚀          ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log(colors.reset);

  // Check firmware exists
  if (!fs.existsSync(FIRMWARE_PATH)) {
    console.log(`${colors.yellow}⚠️  WARNING: Firmware not found!${colors.reset}`);
    console.log(`   Expected: ${FIRMWARE_PATH}`);
    console.log(`   Run: ${colors.bright}npm run ota:build${colors.reset} first\n`);
  } else {
    const stat = fs.statSync(FIRMWARE_PATH);
    console.log(`${colors.green}✅ Firmware ready: ${(stat.size / 1024).toFixed(1)} KB${colors.reset}`);
    console.log(`   ${FIRMWARE_PATH}\n`);
  }

  // Display all network interfaces
  const ips = getLocalIP();
  console.log(`${colors.bright}📡 Server URLs:${colors.reset}`);

  if (ips.length === 0) {
    console.log(`${colors.yellow}   No network interfaces found!${colors.reset}`);
  } else {
    ips.forEach(({ name, ip }) => {
      const isMasterAP = ip.startsWith('192.168.4.');
      const prefix = isMasterAP ? `${colors.bright}${colors.green}` : '  ';
      const suffix = isMasterAP ? ` ${colors.cyan}<-- Use this one!${colors.reset}` : colors.reset;
      console.log(`${prefix}   http://${ip}:${PORT}/firmware.bin${suffix}`);
      console.log(`${prefix}   (${name})${colors.reset}`);
    });
  }

  console.log(`\n${colors.bright}${colors.yellow}📋 Instructions:${colors.reset}`);
  console.log(`   ${colors.bright}1.${colors.reset} Connect this computer to WiFi: ${colors.cyan}"TwentyFourTimes"${colors.reset} / ${colors.cyan}"clockupdate"${colors.reset}`);
  console.log(`   ${colors.bright}2.${colors.reset} Your IP should be: ${colors.cyan}192.168.4.2${colors.reset} (verify above)`);
  console.log(`   ${colors.bright}3.${colors.reset} On master, tap: ${colors.cyan}OTA → Start Server → Send Update${colors.reset}`);
  console.log(`   ${colors.bright}4.${colors.reset} Watch the downloads below!`);
  console.log(`\n${colors.bright}Waiting for pixel connections...${colors.reset}\n`);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log(`\n\n${colors.yellow}Shutting down OTA server...${colors.reset}`);
  console.log(`Total downloads served: ${totalServed}`);
  server.close(() => {
    console.log(`${colors.green}Server stopped.${colors.reset}\n`);
    process.exit(0);
  });
});
