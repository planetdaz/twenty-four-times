#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Resets a COM port by disabling and re-enabling the USB device.

.DESCRIPTION
    This script helps resolve "port is busy" or "Access denied" errors when
    uploading to ESP32 devices. It can kill processes holding the port and
    reset the USB device without requiring a reboot.

.PARAMETER ComPort
    The COM port to reset (e.g., COM18). If not specified, lists available ports.

.PARAMETER KillProcesses
    If specified, attempts to kill common processes that might hold the port.

.PARAMETER Nuclear
    If specified, also restarts the USB controller and kills ALL Python processes.

.EXAMPLE
    .\reset-com-port.ps1 -ComPort COM18

.EXAMPLE
    .\reset-com-port.ps1 -ComPort COM18 -KillProcesses -Nuclear
#>

param(
    [string]$ComPort,
    [switch]$KillProcesses,
    [switch]$Nuclear
)

$ErrorActionPreference = "Stop"

function Write-ColorOutput($ForegroundColor, $Message) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor
    Write-Output $Message
    $host.UI.RawUI.ForegroundColor = $fc
}

# If no COM port specified, list available ports
if (-not $ComPort) {
    Write-ColorOutput Yellow "`nAvailable COM Ports:"
    Write-Output "===================="
    $ports = Get-PnpDevice -Class Ports -Status OK | Where-Object { $_.FriendlyName -match "COM\d+" }
    if ($ports) {
        $ports | ForEach-Object {
            Write-Output "  $($_.FriendlyName)"
        }
    } else {
        Write-Output "  No COM ports found"
    }
    Write-Output "`nUsage: .\reset-com-port.ps1 -ComPort COM18"
    Write-Output "       .\reset-com-port.ps1 -ComPort COM18 -KillProcesses"
    exit 0
}

Write-ColorOutput Cyan "`n=== COM Port Reset Tool ==="
Write-Output "Target: $ComPort`n"

# Kill common processes that might hold COM ports
if ($KillProcesses -or $Nuclear) {
    Write-ColorOutput Yellow "Killing processes that might hold COM ports..."
    $processNames = @("python", "python3", "esptool", "platformio", "pio", "arduino", "putty", "serial")
    foreach ($name in $processNames) {
        $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
        if ($procs) {
            foreach ($proc in $procs) {
                Write-Output "  Killing $($proc.ProcessName) (PID: $($proc.Id))"
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
    Start-Sleep -Seconds 1
}

# Nuclear option - restart USB serial driver
if ($Nuclear) {
    Write-ColorOutput Red "`n=== NUCLEAR MODE ==="
    Write-Output "Restarting USB Serial driver..."

    # Stop and restart the usbser service
    $service = Get-Service -Name "usbser" -ErrorAction SilentlyContinue
    if ($service) {
        Write-Output "  Stopping usbser service..."
        Stop-Service -Name "usbser" -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        Write-Output "  Starting usbser service..."
        Start-Service -Name "usbser" -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }

    # Only cycle the specific target port, not all ports
    Write-Output "  Cycling target COM port device..."
    $targetDevice = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match $ComPort }
    if ($targetDevice) {
        Write-Output ("    Disabling: " + $targetDevice.FriendlyName)
        Disable-PnpDevice -InstanceId $targetDevice.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        Write-Output ("    Enabling: " + $targetDevice.FriendlyName)
        Enable-PnpDevice -InstanceId $targetDevice.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    } else {
        Write-ColorOutput Yellow "  Target device not found - may need to unplug/replug USB"
    }

    # Wait for device to come back
    Write-Output "  Waiting for device to initialize..."
    Start-Sleep -Seconds 5

    Write-ColorOutput Green "Nuclear reset complete. Checking ports..."
    $ports = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue
    if ($ports) {
        $ports | ForEach-Object { Write-Output ("  " + $_.FriendlyName) }
    } else {
        Write-ColorOutput Yellow "  No ports available yet - try unplugging and replugging USB"
    }
    Write-Output ""
}

# Find the device (skip if Nuclear already handled it)
if (-not $Nuclear) {
    Write-Output "Looking for device on $ComPort..."
    $device = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match $ComPort }

    if (-not $device) {
        # Also check for disabled devices
        $device = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match $ComPort }
        if ($device -and $device.Status -eq "Error") {
            Write-ColorOutput Yellow "Device found but in error state. Attempting to re-enable..."
        } elseif (-not $device) {
            Write-ColorOutput Red "No device found for $ComPort"
            Write-Output "The device may be disconnected. Try unplugging and replugging the USB cable."
            exit 1
        }
    }

    Write-Output ("Found: " + $device.FriendlyName)
    Write-Output ("Instance ID: " + $device.InstanceId)
    Write-Output ""

    # Disable the device
    Write-ColorOutput Yellow "Disabling device..."
    try {
        Disable-PnpDevice -InstanceId $device.InstanceId -Confirm:$false
        Write-ColorOutput Green "  Device disabled"
    } catch {
        Write-ColorOutput Red ("  Failed to disable: " + $_)
        exit 1
    }

    Start-Sleep -Seconds 2

    # Re-enable the device
    Write-ColorOutput Yellow "Re-enabling device..."
    try {
        Enable-PnpDevice -InstanceId $device.InstanceId -Confirm:$false
        Write-ColorOutput Green "  Device enabled"
    } catch {
        Write-ColorOutput Red ("  Failed to enable: " + $_)
        exit 1
    }

    Start-Sleep -Seconds 2
}

# Verify the device is back
$device = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match $ComPort }
if ($device) {
    Write-ColorOutput Green "[OK] Success! $ComPort should now be available."
    Write-Output "  You can now retry your upload."
} else {
    Write-ColorOutput Yellow "[WARN] Device may have been assigned a different COM port or is not ready."
    Write-Output "  Try unplugging and replugging the USB cable."
    Write-Output "  Available ports:"
    $allPorts = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue
    if ($allPorts) {
        $allPorts | ForEach-Object { Write-Output ("    " + $_.FriendlyName) }
    } else {
        Write-Output "    (none)"
    }
}

