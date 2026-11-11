#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Automated test script for Transmission Qt startup issue
.DESCRIPTION
    Builds the project, runs the executable, captures logs, and reports results
#>

param(
    [string]$BuildDir = "D:\transmission-main\build",
    [string]$SourceDir = "D:\transmission-main",
    [string]$LogDir = "D:\transmission logging",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Write-Log($message) {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Write-Host "[$timestamp] $message"
}

function Test-Startup {
    Write-Log "=== Starting Automated Startup Test ==="
    
    # Clean up old logs
    if (Test-Path "$LogDir\transmission.log") {
        Remove-Item "$LogDir\transmission.log" -Force
        Write-Log "Removed old log file"
    }
    
    # Step 1: Build the project
    Write-Log "Step 1: Building project..."
    $buildResult = & cmake --build $BuildDir --target Transmission-qt --config Release 2>&1
    $buildExitCode = $LASTEXITCODE
    
    if ($buildExitCode -ne 0) {
        Write-Log "BUILD FAILED with exit code $buildExitCode"
        Write-Log "Build output:"
        $buildResult | ForEach-Object { Write-Log "  $_" }
        return @{ Success = $false; Stage = "Build"; ExitCode = $buildExitCode; Output = $buildResult }
    }
    Write-Log "Build succeeded"
    
    # Step 2: Run the executable
    $exePath = "$BuildDir\qt\Transmission-qt.exe"
    if (-not (Test-Path $exePath)) {
        Write-Log "ERROR: Executable not found at $exePath"
        return @{ Success = $false; Stage = "Run"; Error = "Executable not found" }
    }
    
    Write-Log "Step 2: Running executable..."
    Write-Log "Executable: $exePath"
    Write-Log "Log directory: $LogDir"
    
    # Start the process and capture output
    $process = Start-Process -FilePath $exePath -ArgumentList "--portable" -WindowStyle Hidden -PassThru
    
    # Wait for process to exit or timeout
    $waitResult = $process | Wait-Process -Timeout $TimeoutSeconds -ErrorAction SilentlyContinue
    
    if ($process.HasExited) {
        $exitCode = $process.ExitCode
        Write-Log "Process exited with code: $exitCode"
    } else {
        Write-Log "Process did not exit within $TimeoutSeconds seconds - killing"
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $exitCode = -1
    }
    
    # Step 3: Read and analyze logs
    Write-Log "Step 3: Analyzing logs..."
    
    if (Test-Path "$LogDir\transmission.log") {
        $logContent = Get-Content "$LogDir\transmission.log" -Raw
        Write-Log "Log file size: $([System.IO.FileInfo]::new("$LogDir\transmission.log").Length) bytes"
        
        # Check for key checkpoints
        $checkpoints = @(
            "APP-CTOR start",
            "APP-CTOR setApplicationName done",
            "APP-CTOR loadTranslations done", 
            "APP-CTOR initUnits done",
            "APP-CTOR setWindowIcon done",
            "APP-CTOR config dir check",
            "APP-CTOR config dir exists/mkpath done",
            "APP-CTOR first_time check done",
            "APP-CTOR accessibility done",
            "APP-CTOR Session created",
            "APP-CTOR TorrentModel created",
            "APP-CTOR MainWindow created",
            "APP-CTOR WatchDir created",
            "MainWindow isVisible=",
            "first_time =",
            "Calling session_->start()",
            "Calling window_->openSession()",
            "CHECKPOINT Application created, calling exec()",
            "CHECKPOINT exec() returned:"
        )
        
        foreach ($cp in $checkpoints) {
            if ($logContent -match [regex]::Escape($cp)) {
                Write-Log "  ✓ Found: $cp"
            } else {
                Write-Log "  ✗ MISSING: $cp"
            }
        }
        
        # Check for TOGGLE-WINDOWS logs
        if ($logContent -match "TOGGLE-WINDOWS") {
            Write-Log "  ✓ Found TOGGLE-WINDOWS logs"
            $toggleLogs = $logContent -split "`n" | Where-Object { $_ -match "TOGGLE-WINDOWS" }
            foreach ($line in $toggleLogs) {
                Write-Log "    $line"
            }
        } else {
            Write-Log "  ✗ No TOGGLE-WINDOWS logs found"
        }
        
        # Check for Qt warnings/errors
        if ($logContent -match "QtMsg") {
            Write-Log "  Qt messages found:"
            $qtMsgs = $logContent -split "`n" | Where-Object { $_ -match "QtMsg" }
            foreach ($line in $qtMsgs) {
                Write-Log "    $line"
            }
        }
        
        # Check for exceptions
        if ($logContent -match "EXCEPTION") {
            Write-Log "  ⚠ EXCEPTIONS FOUND:"
            $exMsgs = $logContent -split "`n" | Where-Object { $_ -match "EXCEPTION" }
            foreach ($line in $exMsgs) {
                Write-Log "    $line"
            }
        }
        
        # Save full log for inspection
        $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $archiveLog = "$LogDir\transmission_${timestamp}.log"
        Copy-Item "$LogDir\transmission.log" $archiveLog -Force
        Write-Log "Full log archived to: $archiveLog"
        
        return @{
            Success = ($exitCode -eq 0);
            Stage = "Run";
            ExitCode = $exitCode;
            LogFile = "$LogDir\transmission.log";
            ArchiveLog = $archiveLog;
            CheckpointsFound = $checkpoints.Count
        }
    } else {
        Write-Log "ERROR: No log file generated at $LogDir\transmission.log"
        return @{ Success = $false; Stage = "Run"; Error = "No log file" }
    }
}

# Run the test
$result = Test-Startup

Write-Log "=== Test Result ==="
Write-Log "Success: $($result.Success)"
Write-Log "Stage: $($result.Stage)"
if ($result.ExitCode) { Write-Log "ExitCode: $($result.ExitCode)" }
if ($result.Error) { Write-Log "Error: $($result.Error)" }

# Exit with appropriate code
exit (if ($result.Success) { 0 } else { 1 })